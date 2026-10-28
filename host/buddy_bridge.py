#!/usr/bin/env python3
"""Feeds the Claude buddy over USB or Bluetooth without the Claude desktop Hardware Buddy window.

Claude Code hooks (buddy_hook.py) tell this bridge what each session is doing;
an optional watcher notices sessions that have no hooks (anything that writes
~/.claude/projects/**/*.jsonl). The bridge turns that into the same JSON the
desktop app sends, writes it to the buddy, and returns the buddy's approve/deny to the hook that
is waiting for it.

The buddy is reached over USB when it is plugged in, otherwise over Bluetooth LE
(Nordic UART, the same service the desktop app uses). USB wins: if a cable shows
up while the bridge is on Bluetooth, it moves to USB.

Needs Python 3.8+. USB uses pyserial if it is installed, otherwise termios
(macOS and Linux). Bluetooth needs bleak (pip install bleak).
"""
import argparse, asyncio, atexit, signal, collections, glob, itertools, json, os, queue, socket, subprocess, sys, threading, time, uuid

TCP_HOST, TCP_PORT = "127.0.0.1", 47321
RUN_TIMEOUT = 300       # a "running" session with no events for this long counts as idle
TOOL_TIMEOUT = 1800     # same, while a tool call is in progress (hooks are silent until it ends)
STOP_GRACE = 5          # a finished turn often resumes at once (background task done, queued message)
WATCH_ACTIVE = 15       # a transcript written this recently counts as running
MAX_ENTRIES = 6
HB_LIMIT = 1000         # bytes: the buddy firmware reads heartbeats into a 1 KB line buffer


CTX_WINDOW = 200_000    # context window assumed for a session; 1M if its model says [1m] or it has used more
CTX_TAIL = 262144       # how much of a transcript's end to scan for the latest usage


def default_window():
    try:
        with open(os.path.expanduser("~/.claude/settings.json")) as f:
            if "[1m]" in str(json.load(f).get("model", "")): return 1_000_000
    except (OSError, ValueError):
        pass
    return CTX_WINDOW


def context_used(path):
    """Tokens in the context after the session's latest assistant message (input
    plus both cache counts), read from the end of its transcript. Sub-agent
    messages are skipped: they have their own context. None if not found."""
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END); size = f.tell()
            f.seek(max(0, size - CTX_TAIL)); tail = f.read().decode("utf-8", "replace")
    except OSError:
        return None
    for line in reversed(tail.splitlines()):
        if '"usage"' not in line: continue
        try: d = json.loads(line)
        except ValueError: continue            # the first line of the tail may be cut off
        u = (d.get("message") or {}).get("usage") if isinstance(d.get("message"), dict) else None
        if not u or d.get("isSidechain") or d.get("type") != "assistant": continue
        return sum(u.get(k) or 0 for k in ("input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens"))
    return None


LOG = collections.deque(maxlen=500)   # (seq, line) for the status window
_log_seq = itertools.count(1)


def log(*a):
    line = " ".join([time.strftime("%H:%M:%S")] + [str(x) for x in a])
    LOG.append((next(_log_seq), line))
    print(line, file=sys.stderr, flush=True)


# ───────────── serial ─────────────
class Serial:
    """Minimal line-based serial port: pyserial when available, else termios."""

    kind = "usb"

    def __init__(self, path, baud=115200):
        self.path, self.baud, self.f, self.ser, self.buf = path, baud, None, None, b""
        self.label = f"USB {path}"

    def open(self):
        try:
            import serial
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = self.path, self.baud, 0
            s.dtr = False
            s.rts = False
            s.open()
            self.ser = s
        except ImportError:
            import termios, tty
            fd = os.open(self.path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            tty.setraw(fd)
            attrs = termios.tcgetattr(fd)
            attrs[4] = attrs[5] = getattr(termios, "B%d" % self.baud)
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            self.f = fd

    def close(self):
        try:
            if self.ser: self.ser.close()
            if self.f is not None: os.close(self.f)
        except OSError:
            pass
        self.ser = self.f = None

    def write(self, data: bytes):
        # The board's USB receive buffer is small: a whole heartbeat sent at once
        # loses bytes and the line is dropped. One USB packet at a time is safe.
        for i in range(0, len(data), 64):
            self._write(data[i:i + 64])
            if i + 64 < len(data): time.sleep(0.02)

    def _write(self, data):
        if self.ser: self.ser.write(data)
        else:
            while data:
                try: n = os.write(self.f, data)
                except BlockingIOError: time.sleep(0.005); continue
                data = data[n:]

    def lines(self):
        """Yield complete lines received since the last call."""
        try:
            chunk = self.ser.read(4096) if self.ser else os.read(self.f, 4096)
        except BlockingIOError:
            chunk = b""
        self.buf += chunk or b""
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            yield line.strip().decode("utf-8", "replace")


def find_port():
    """First Espressif USB serial port (VID 0x303A); falls back to usbmodem names."""
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            if p.vid == 0x303A:
                return p.device
    except ImportError:
        pass
    for pat in ("/dev/cu.usbmodem*", "/dev/ttyACM*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    return None


class BleLink:
    """The buddy over Bluetooth LE: Nordic UART service, newline-delimited JSON.
    Same interface as Serial (open / lines / write / close). bleak runs in a
    background thread with its own event loop; this class is called from one
    plain thread. The first connection asks macOS to pair: type the 6-digit code
    the buddy shows on its screen."""
    kind = "ble"
    SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
    TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # buddy -> computer, notify
    RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # computer -> buddy, write

    def __init__(self, target=None):
        self.target = target          # name or address (part of it); None = the first buddy found
        self.path = self.label = None
        self.q = queue.Queue()
        self.buf = b""
        self.loop = self.client = None
        self.dead = False

    def open(self):
        self.loop = asyncio.new_event_loop()
        threading.Thread(target=self.loop.run_forever, daemon=True).start()
        try:
            self._call(self._connect(), 120)      # includes the time to type a pairing code
        except BaseException:
            self.close(); raise

    def _call(self, coro, timeout):
        return asyncio.run_coroutine_threadsafe(coro, self.loop).result(timeout)

    async def _connect(self):
        from bleak import BleakClient, BleakScanner
        want = (self.target or "").lower()

        def match(dev, adv):
            name = dev.name or adv.local_name or ""
            if want: return want in name.lower() or want == dev.address.lower()
            return name.startswith("Claude") and not name.startswith("Claudinho")   # Claudinho speaks another protocol

        dev = await self._already_connected(match) or await BleakScanner.find_device_by_filter(match, timeout=6)
        if not dev: raise OSError("no buddy in Bluetooth range")
        self.client = BleakClient(dev, disconnected_callback=lambda _c: setattr(self, "dead", True))
        await self.client.connect(timeout=20)
        await self.client.start_notify(self.TX, self._notify)   # first encrypted access: pairing happens here
        self.path = dev.address
        self.label = f"Bluetooth {dev.name or dev.address}"

    async def _already_connected(self, match):
        """macOS keeps a paired buddy connected on its own (e.g. a link a closed bridge left behind),
        and a connected buddy stops advertising, so a scan never sees it. Ask CoreBluetooth for
        connected devices that offer the Nordic UART service instead. macOS only; it reaches into
        bleak's CoreBluetooth backend, so any failure just means "scan as usual"."""
        if sys.platform != "darwin": return None
        try:
            from bleak import BleakScanner
            from bleak.backends.device import BLEDevice
            from CoreBluetooth import CBUUID
            manager = BleakScanner()._backend._manager
            await manager.wait_until_ready()
            nus = CBUUID.UUIDWithString_(self.SERVICE)
            for p in manager.central_manager.retrieveConnectedPeripheralsWithServices_([nus]):
                dev = BLEDevice(p.identifier().UUIDString(), p.name(), (p, manager))
                if match(dev, type("Adv", (), {"local_name": None})()): return dev
        except Exception:
            pass
        return None

    def _notify(self, _char, data):
        self.buf += bytes(data)
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            self.q.put(line.strip().decode("utf-8", "replace"))

    def lines(self):
        while True:
            try: yield self.q.get_nowait()
            except queue.Empty: break
        if self.dead: raise OSError("Bluetooth link lost")

    def write(self, data: bytes):
        if self.dead: raise OSError("Bluetooth link lost")
        self._call(self._write(data), 15)

    async def _write(self, data):
        n = max(20, min(180, (self.client.mtu_size or 23) - 3))
        for i in range(0, len(data), n):
            await self.client.write_gatt_char(self.RX, data[i:i + n], response=True)

    def close(self):
        loop, self.loop = self.loop, None
        if not loop: return
        try:
            if self.client and self.client.is_connected:
                asyncio.run_coroutine_threadsafe(self.client.disconnect(), loop).result(5)
        except Exception:
            pass
        loop.call_soon_threadsafe(loop.stop)


class ConnectedWatch:
    """Is a buddy in macOS's list of connected Bluetooth devices? macOS connects a paired buddy by
    itself when it comes into range, so asking is a cheap way to notice it while the Bluetooth scan
    is paused: no radio scan, just a query. macOS only (False elsewhere or on any error)."""
    def __init__(self, target=None):
        self.target = (target or "").lower()
        self.loop = self.manager = None

    def present(self):
        if sys.platform != "darwin": return False
        if not self.loop:
            self.loop = asyncio.new_event_loop()
            threading.Thread(target=self.loop.run_forever, daemon=True).start()
        try:
            return asyncio.run_coroutine_threadsafe(self._check(), self.loop).result(10)
        except Exception:
            return False

    async def _check(self):
        from bleak import BleakScanner
        from CoreBluetooth import CBUUID
        if not self.manager:
            self.manager = BleakScanner()._backend._manager
            await self.manager.wait_until_ready()
        for p in self.manager.central_manager.retrieveConnectedPeripheralsWithServices_([CBUUID.UUIDWithString_(BleLink.SERVICE)]):
            name = p.name() or ""
            if self.target and self.target in name.lower() or \
               not self.target and name.startswith("Claude") and not name.startswith("Claudinho"):
                return True
        return False


def ble_available():
    try:
        import bleak  # noqa: F401
        return True
    except ImportError:
        return False


# ───────────── state ─────────────
class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.sessions = {}      # id -> {"state": idle|running|waiting, "last": ts}
        self.watched = set()    # session ids seen only through the transcript watcher
        self.hooked = set()     # session ids reported by hooks; the watcher leaves these alone
        self.asks = set()       # session ids that send PermissionRequest (so their Notification is redundant)
        self.entries = []       # (text, session id), oldest first, newest last (what the firmware displays)
        self.slots = {}         # session id -> colour slot, in the order sessions were first seen
        self.cwds = {}          # session id -> project folder name, from the hook events
        self.paths = {}         # session id -> transcript file
        self.ctx_cache = {}     # session id -> ((mtime, size), tokens)
        self.window = default_window()
        self.pending = {}       # permission id -> {"tool","ti","hint","event","decision","sid"}
        self.msg = "idle"
        self.dirty = True
        self.link = None        # how the buddy is reached ("USB /dev/...", "Bluetooth Claude-1234"), None while looking
        self.link_kind = None   # "usb" or "ble"
        self.link_obj = None    # the open Serial/BleLink, so an exit can close it cleanly
        self.last_hb = 0        # when the last heartbeat went out
        self.labels = True      # tag entries with their session when several are open
        self.ble_paused = False # Bluetooth scanning is on hold because Claude Code is idle
        self.force_ble = False  # the user asked for Bluetooth: drop USB, scan even when idle, stay on Bluetooth

    def touch(self, sid, state, in_tool=False):
        s = self.sessions.setdefault(sid, {"state": "idle", "last": 0})
        s["state"], s["last"], s["in_tool"] = state, time.time(), in_tool
        s.pop("idle_at", None)      # any event cancels a pending Stop
        self.dirty = True

    def add_entry(self, text, sid=None):
        self.entries.append((text[:80], sid))
        self.entries = self.entries[-MAX_ENTRIES:]
        self.msg = text[:44]
        self.dirty = True

    def settle(self, sid, tool=None, ti=None):
        """Drop prompts the user already answered in the terminal. Claude Code
        leaves the hook waiting in that case, so the hook never hangs up."""
        for p in self.pending.values():
            if p["sid"] == sid and (tool is None or (p["tool"] == tool and p["ti"] == ti)):
                p["elsewhere"] = True; p["event"].set()

    def apply_event(self, ev):
        name, sid = ev.get("hook_event_name", ""), ev.get("session_id", "?")
        tool, ti = ev.get("tool_name", ""), ev.get("tool_input") or {}
        hhmm = time.strftime("%H:%M")
        with self.lock:
            self.hooked.add(sid); self.watched.discard(sid)
            self.slots.setdefault(sid, len(self.slots))
            if ev.get("cwd"): self.cwds[sid] = os.path.basename(ev["cwd"].rstrip("/")) or "/"
            if ev.get("transcript_path"): self.paths[sid] = ev["transcript_path"]
            if name in ("PostToolUse", "PostToolUseFailure"):
                self.settle(sid, tool, ti)          # approved: that tool ran
            elif name in ("UserPromptSubmit", "Stop", "StopFailure", "SessionEnd"):
                self.settle(sid)                    # the turn moved on: anything still asked is moot
            if name == "UserPromptSubmit":
                self.touch(sid, "running")
                self.add_entry(f"{hhmm} thinking...", sid)
            elif name == "PreToolUse":
                self.touch(sid, "running", in_tool=True)
                self.add_entry(f"{hhmm} {tool} {describe(tool, ti)}".strip(), sid)
            elif name in ("PostToolUse", "PostToolUseFailure", "SubagentStart", "SessionStart"):
                self.touch(sid, "running" if name != "SessionStart" else "idle")
            elif name in ("Stop", "SubagentStop", "StopFailure"):
                if name != "SubagentStop":
                    # Stay running for a moment: if the next turn starts right away,
                    # the buddy shouldn't flash idle. expire() finishes the Stop.
                    self.touch(sid, "running")
                    self.sessions[sid]["idle_at"] = (time.time() + STOP_GRACE, f"{hhmm} done")
            elif name == "SessionEnd":
                self.sessions.pop(sid, None)
                self.dirty = True
            elif name == "Notification":
                nt = ev.get("notification_type", "")
                if "permission" in nt or "permission" in str(ev.get("message", "")).lower():
                    # Arrives seconds late, often after the user already answered;
                    # PermissionRequest covers it for sessions that send one.
                    if sid not in self.asks: self.touch(sid, "waiting")
                elif "idle" in nt:
                    self.touch(sid, "idle")

    def busy(self):
        """True while any session is working or waiting for you (or a prompt is
        pending): the buddy has something to show."""
        with self.lock:
            return bool(self.pending) or any(s["state"] in ("running", "waiting") for s in self.sessions.values())

    def expire(self):
        now = time.time()
        with self.lock:
            for sid, s in list(self.sessions.items()):
                if "idle_at" in s and now >= s["idle_at"][0]:
                    s["state"] = "idle"; self.add_entry(s.pop("idle_at")[1], sid)
                limit = TOOL_TIMEOUT if s.get("in_tool") else RUN_TIMEOUT
                if s["state"] == "running" and now - s["last"] > limit:
                    s["state"] = "idle"; self.dirty = True
            for sid in [k for k, s in self.sessions.items() if now - s["last"] > 6 * 3600]:
                del self.sessions[sid]

    def label(self, sid):
        """Short name of a session: its project folder, with #2, #3 ... when two
        open sessions share a folder. Spaces become dashes so it stays one word."""
        base = (self.cwds.get(sid) or (sid or "?")[:6]).replace(" ", "-")[:10]
        twins = sorted((k for k in self.slots if (self.cwds.get(k) or (k or "?")[:6]).replace(" ", "-")[:10] == base),
                       key=self.slots.get)
        return base if len(twins) < 2 else f"{base[:8]}#{twins.index(sid) + 1}"

    def context_pct(self, sid):
        """Percent of the context window a session has used, or -1 if unknown.
        The transcript is only re-read when it has changed."""
        path = self.paths.get(sid)
        if not path: return -1
        try: st = os.stat(path); key = (st.st_mtime, st.st_size)
        except OSError: return -1
        if self.ctx_cache.get(sid, (None,))[0] != key:
            self.ctx_cache[sid] = (key, context_used(path))
        used = self.ctx_cache[sid][1]
        if used is None: return -1
        window = self.window if used <= self.window else 1_000_000
        return min(100, round(used * 100 / window))

    def heartbeat(self):
        with self.lock:
            run = sum(1 for s in self.sessions.values() if s["state"] == "running")
            wait = len({sid for sid, s in self.sessions.items() if s["state"] == "waiting"}
                       | {p["sid"] for p in self.pending.values()})
            hb = {"total": len(self.sessions), "running": run, "waiting": wait,
                  "msg": ("approve: " + next(iter(self.pending.values()))["tool"]) if self.pending else self.msg,
                  "entries": [t for t, _ in self.entries], "tokens": 0, "tokens_today": 0}
            if self.labels and len(self.sessions) > 1:
                # Parallel to entries: which session each line came from, and its colour slot.
                hb["sess"] = [self.label(sid) if sid else "" for _, sid in self.entries]
                hb["sc"] = [self.slots.get(sid, 0) % 6 for _, sid in self.entries]
            if self.labels:
                hb["cx"] = [self.context_pct(sid) if sid else -1 for _, sid in self.entries]   # context % of each line's session
            for pid, p in self.pending.items():
                hb["prompt"] = {"id": pid, "tool": p["tool"][:19], "hint": p["hint"][:43]}
                if self.labels and len(self.sessions) > 1:
                    hb["prompt"]["sess"] = self.label(p["sid"]); hb["prompt"]["sc"] = self.slots.get(p["sid"], 0) % 6
                break
            return fit(hb)


def fit(hb, limit=HB_LIMIT):
    """Keep the heartbeat under the firmware's line buffer: drop the optional
    extras first, then the oldest transcript lines."""
    def size(): return len(json.dumps(hb, separators=(",", ":")).encode())
    for k in ("cx", "sc", "sess"):
        if size() <= limit: return hb
        hb.pop(k, None)
    while size() > limit and len(hb["entries"]) > 1:
        hb["entries"].pop(0)
    return hb


def describe(tool, ti):
    for k in ("command", "file_path", "path", "pattern", "url", "description"):
        if isinstance(ti.get(k), str):
            return os.path.basename(ti[k]) if k in ("file_path", "path") else ti[k].replace("\n", " ")
    return ""


def command_mark(ti):
    """A piece of a Bash command that shows up in its shell's arguments once it
    runs (Claude Code passes the command as eval '...', with quotes escaped)."""
    first = (ti.get("command") or "").split("\n")[0]
    mark = max(first.split("'"), key=len).strip()[:80]
    return mark if len(mark) >= 8 else None


def command_running(mark):
    try:
        ps = subprocess.run(["ps", "-A", "-ww", "-o", "command="], capture_output=True, text=True, timeout=2)
    except (OSError, subprocess.SubprocessError):
        return False
    return mark in ps.stdout


# ───────────── hook server ─────────────
def listen():
    """The hook socket. Raises OSError if another bridge already has the port."""
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((TCP_HOST, TCP_PORT)); srv.listen(8)
    return srv


def serve(state, srv):
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=handle, args=(state, conn), daemon=True).start()


def handle(state, conn):
    try:
        conn.settimeout(5)
        data = b""
        while not data.endswith(b"\n"):
            chunk = conn.recv(65536)
            if not chunk: break
            data += chunk
        ev = json.loads(data.decode())
        extra = ev.get("tool_name") or ev.get("notification_type") or ev.get("message") or ""
        log("hook:", ev.get("hook_event_name"), str(extra)[:60], "session", str(ev.get("session_id", "?"))[:8])
        if ev.get("hook_event_name") != "PermissionRequest":
            state.apply_event(ev)
            conn.sendall(b'{"ok":true}\n')
            return
        pid = "req_" + uuid.uuid4().hex[:8]
        tool, ti = ev.get("tool_name", "?"), ev.get("tool_input") or {}
        sid = ev.get("session_id", "?")
        p = {"tool": tool, "ti": ti, "hint": describe(tool, ti) or tool, "event": threading.Event(),
             "decision": None, "sid": sid}
        with state.lock:
            state.pending[pid] = p; state.asks.add(sid); state.dirty = True
            state.slots.setdefault(sid, len(state.slots))
            if ev.get("cwd"): state.cwds[sid] = os.path.basename(ev["cwd"].rstrip("/")) or "/"
        conn.settimeout(None)
        start = time.time()
        log("approval requested:", tool, p["hint"][:60])
        # Wait for the buddy, an answer in the terminal (settle), or the hook giving up.
        # A Bash command approved in the terminal is visible as soon as it starts,
        # long before its PostToolUse.
        mark = command_mark(ti) if tool == "Bash" else None
        while not p["event"].wait(0.5):
            if mark and command_running(mark):
                p["elsewhere"] = True; break
            conn.setblocking(False)
            try:
                if conn.recv(1) == b"": break
            except BlockingIOError:
                pass
            except OSError:
                break
            conn.setblocking(True)
        with state.lock:
            state.pending.pop(pid, None); state.dirty = True
            # Answered here or in the terminal: the session isn't waiting any more,
            # though the permission Notification marked it so. The tool runs next.
            s = state.sessions.get(sid)
            if s and s["state"] == "waiting": state.touch(sid, "running", in_tool=True)
        log(f"approval closed after {time.time() - start:.1f}s:",
            "buddy " + p["decision"] if p["decision"] else
            "answered in the terminal" if p.get("elsewhere") else "hook gave up")
        try: conn.sendall((json.dumps({"decision": p["decision"] or "none"}) + "\n").encode())
        except OSError: pass
    except Exception as e:
        log("hook connection error:", e)
    finally:
        conn.close()


# ───────────── transcript watcher ─────────────
def watch(state, roots):
    logged = set()
    while True:
        now = time.time()
        seen = set()
        for root in roots:
            for path in glob.glob(os.path.join(os.path.expanduser(root), "**", "*.jsonl"), recursive=True):
                try:
                    if now - os.path.getmtime(path) < WATCH_ACTIVE:
                        seen.add(os.path.basename(path)[:-6])   # transcript name is the session id
                except OSError:
                    pass
        with state.lock:
            for sid in seen - logged:
                log("watcher: session", sid[:20], "is active (no hooks)")
            logged |= seen
            for sid in seen:
                # Hooked sessions manage their own state; only add the ones we have no hooks for.
                if sid not in state.hooked:
                    state.watched.add(sid)
                    s = state.sessions.setdefault(sid, {"state": "idle", "last": 0})
                    if s["state"] != "running": state.dirty = True
                    s["state"], s["last"] = "running", now
            for sid in list(state.watched):
                if sid not in seen and sid in state.sessions:
                    state.sessions[sid]["state"] = "idle"; state.dirty = True
        time.sleep(2)


# ───────────── main loop ─────────────
def start(state, srv, watch_roots=("~/.claude/projects",)):
    """Hook server and transcript watcher, in the background."""
    threading.Thread(target=serve, args=(state, srv), daemon=True).start()
    if watch_roots:
        threading.Thread(target=watch, args=(state, list(watch_roots)), daemon=True).start()
    log(f"hooks connect to {TCP_HOST}:{TCP_PORT}")


def pump(state, link, leave=None):
    """Talk to the buddy over one link until it fails (raises) or leave() says
    another link should take over (returns its reason, a string)."""
    last_hb = last_time = last_check = 0
    while True:
        now = time.time()
        for line in link.lines():
            if line.startswith("{"):
                try: msg = json.loads(line)
                except ValueError: continue
                if msg.get("cmd") == "permission":
                    p = state.pending.get(msg.get("id"))
                    if p:
                        p["decision"] = "deny" if msg.get("decision") == "deny" else "allow"
                        p["event"].set()
                        log("buddy answered:", p["decision"])
        if now - last_time > 300:
            off = -(time.altzone if time.localtime().tm_isdst > 0 else time.timezone)
            link.write((json.dumps({"time": [int(now), off]}) + "\n").encode()); last_time = now
        state.expire()
        if state.dirty or now - last_hb > 3:
            state.dirty = False
            link.write((json.dumps(state.heartbeat(), separators=(",", ":")) + "\n").encode())
            last_hb = state.last_hb = now
        if leave and now - last_check > 0.5:
            last_check = now
            reason = leave()
            if reason: return reason
        time.sleep(0.05)


def link_loop(state, port=None, baud=115200, transport="auto", ble_target=None, scan_idle=False):
    """Reach the buddy forever: USB when it is plugged in, otherwise Bluetooth
    (when allowed and bleak is installed). Moves back to USB as soon as a cable
    appears, and reopens whichever link goes away. While Claude Code is idle the
    Bluetooth search is paused (it keeps the radio busy) but macOS's list of connected
    devices is still polled, so a buddy that shows up is picked up at once; the
    search starts again the moment a session works or asks for you; scan_idle keeps it going regardless.
    A Bluetooth link that is already up is never dropped for being idle.
    state.force_ble (the window's button) is a one-shot request: USB is dropped,
    the Bluetooth search runs even while idle, and the flag clears once Bluetooth
    is up. The cable that was plugged in then is ignored until it is unplugged;
    plugging it in again moves the link back to USB."""
    use_usb, use_ble = transport in ("auto", "usb"), transport in ("auto", "ble")
    if use_ble and not ble_available():
        log("Bluetooth needs bleak (pip install bleak); using USB only")
        use_ble = False
        if not use_usb: return
    said = {}
    def once(key, text):
        if said.get(key) != text: said[key] = text; log(text)
    watch = ConnectedWatch(ble_target)
    usb_seen = [0.0, None]
    usb_hold = [False]                               # a cable was in when Bluetooth was forced: ignore it until it is pulled
    def usb_path():                                  # the serial port scan is not free: at most every 2 s
        if time.time() - usb_seen[0] > 2: usb_seen[:] = [time.time(), port or find_port()]
        return usb_seen[1]
    while True:
        forced = state.force_ble and use_ble
        path = (port or find_port()) if use_usb and not forced else None
        if path: link = Serial(path, baud)
        elif use_ble:
            idle = not scan_idle and not forced and not state.busy()
            if idle and watch.present():
                log("buddy is in the Bluetooth connected list: connecting")
            elif idle:
                if not state.ble_paused: state.ble_paused = True; log("Claude Code is idle: Bluetooth scanning paused (watching the connected list)")
                time.sleep(2); continue
            elif state.ble_paused: state.ble_paused = False; log("Claude Code is active: Bluetooth scanning")
            link = BleLink(ble_target)
        else:
            time.sleep(2); continue
        reason = None
        try:
            link.open()
            state.link, state.link_kind, state.link_obj = link.label, link.kind, link
            state.ble_paused = False
            said.clear()
            log("buddy on", link.label)
            if link.kind == "ble":
                if state.force_ble:
                    state.force_ble = False
                    usb_hold[0] = bool(use_usb and usb_path())
                    if usb_hold[0]: log("Bluetooth connected; USB is ignored until the cable is replugged")
                def leave():
                    cable = usb_path() if use_usb else None
                    if usb_hold[0]:
                        if not cable: usb_hold[0] = False
                        return None
                    return "USB is plugged in: switching from Bluetooth to USB" if cable else None
            else:
                usb_hold[0] = False
                leave = (lambda: "Bluetooth was forced: switching from USB to Bluetooth" if state.force_ble and use_ble else None) if use_ble else None
            reason = pump(state, link, leave)
            log(reason)
        except Exception as e:
            if link.kind == "ble": once("ble", f"Bluetooth: {e} - looking again")
            else: log("serial problem:", e, "- retrying")
        finally:
            link.close()
            state.link = state.link_kind = state.link_obj = None
        time.sleep(0.5 if reason else 2)


def close_on_exit(state):
    """Close the buddy link when the bridge exits (normally, Ctrl-C, closed window, SIGTERM or SIGHUP).
    A Bluetooth link that is simply dropped is left "connected" by macOS for a while, and the
    buddy does not advertise meanwhile, so the next start cannot find it."""
    def release():
        link = state.link_obj
        if link: link.close()
    atexit.register(release)
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, lambda *_: sys.exit(0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port of the buddy (default: auto-detect)")
    ap.add_argument("--transport", choices=("auto", "usb", "ble"), default="auto",
                    help="auto: USB when plugged in, else Bluetooth (default); usb or ble: only that one")
    ap.add_argument("--scan-idle", action="store_true",
                    help="keep searching for the buddy over Bluetooth even while Claude Code is idle (default: pause)")
    ap.add_argument("--ble", metavar="NAME_OR_ADDRESS",
                    help="which buddy to use over Bluetooth, e.g. Claude-1234 (default: the first one found)")
    ap.add_argument("--watch", nargs="*", default=["~/.claude/projects"],
                    help="directories to watch for session transcripts (pass --watch with nothing to disable)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--no-labels", action="store_true",
                    help="don't tag transcript lines with their session (keeps heartbeats smallest for firmware with a 1 KB line buffer)")
    args = ap.parse_args()

    try:
        srv = listen()
    except OSError:
        sys.exit(f"Another bridge is already running ({TCP_HOST}:{TCP_PORT} is in use).")
    state = State()
    state.labels = not args.no_labels
    start(state, srv, args.watch)
    close_on_exit(state)
    link_loop(state, args.port, args.baud, args.transport, args.ble, args.scan_idle)


if __name__ == "__main__":
    try: main()
    except KeyboardInterrupt: pass
