#!/usr/bin/env python3
"""Feeds the Claude buddy over USB without the Claude desktop Hardware Buddy window.

Claude Code hooks (buddy_hook.py) tell this bridge what each session is doing;
an optional watcher notices sessions that have no hooks (anything that writes
~/.claude/projects/**/*.jsonl). The bridge turns that into the same JSON the
desktop app sends, writes it to the buddy's USB serial port, and returns the
buddy's approve/deny to the hook that is waiting for it.

Needs Python 3.8+. Uses pyserial if it is installed, otherwise termios
(macOS and Linux).
"""
import argparse, glob, json, os, socket, sys, threading, time, uuid

TCP_HOST, TCP_PORT = "127.0.0.1", 47321
RUN_TIMEOUT = 300       # a "running" session with no events for this long counts as idle
WATCH_ACTIVE = 15       # a transcript written this recently counts as running
MAX_ENTRIES = 6          # keeps the heartbeat under the firmware 1 KB line buffer


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, file=sys.stderr, flush=True)


# ───────────── serial ─────────────
class Serial:
    """Minimal line-based serial port: pyserial when available, else termios."""

    def __init__(self, path, baud=115200):
        self.path, self.baud, self.f, self.ser, self.buf = path, baud, None, None, b""

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


# ───────────── state ─────────────
class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.sessions = {}      # id -> {"state": idle|running|waiting, "last": ts}
        self.watched = set()    # session ids seen only through the transcript watcher
        self.entries = []       # oldest first, newest last (what the firmware displays)
        self.pending = {}       # permission id -> {"tool","hint","event","decision","sid"}
        self.msg = "idle"
        self.dirty = True

    def touch(self, sid, state):
        s = self.sessions.setdefault(sid, {"state": "idle", "last": 0})
        s["state"], s["last"] = state, time.time()
        self.dirty = True

    def add_entry(self, text):
        self.entries.append(text[:80])
        self.entries = self.entries[-MAX_ENTRIES:]
        self.msg = text[:44]
        self.dirty = True

    def apply_event(self, ev):
        name, sid = ev.get("hook_event_name", ""), ev.get("session_id", "?")
        tool, ti = ev.get("tool_name", ""), ev.get("tool_input") or {}
        hhmm = time.strftime("%H:%M")
        with self.lock:
            if name == "UserPromptSubmit":
                self.touch(sid, "running")
                self.add_entry(f"{hhmm} thinking...")
            elif name == "PreToolUse":
                self.touch(sid, "running")
                self.add_entry(f"{hhmm} {tool} {describe(tool, ti)}".strip())
            elif name in ("PostToolUse", "PostToolUseFailure", "SubagentStart", "SessionStart"):
                self.touch(sid, "running" if name != "SessionStart" else "idle")
            elif name in ("Stop", "SubagentStop", "StopFailure"):
                if name != "SubagentStop":
                    self.touch(sid, "idle")
                    self.add_entry(f"{hhmm} done")
            elif name == "SessionEnd":
                self.sessions.pop(sid, None)
                self.dirty = True
            elif name == "Notification":
                nt = ev.get("notification_type", "")
                if "permission" in nt or "permission" in str(ev.get("message", "")).lower():
                    self.touch(sid, "waiting")
                elif "idle" in nt:
                    self.touch(sid, "idle")

    def expire(self):
        now = time.time()
        with self.lock:
            for sid, s in list(self.sessions.items()):
                if s["state"] == "running" and now - s["last"] > RUN_TIMEOUT:
                    s["state"] = "idle"; self.dirty = True
            for sid in [k for k, s in self.sessions.items() if now - s["last"] > 6 * 3600]:
                del self.sessions[sid]

    def heartbeat(self):
        with self.lock:
            run = sum(1 for s in self.sessions.values() if s["state"] == "running")
            wait = sum(1 for s in self.sessions.values() if s["state"] == "waiting") + len(self.pending)
            hb = {"total": len(self.sessions), "running": run, "waiting": wait,
                  "msg": ("approve: " + next(iter(self.pending.values()))["tool"]) if self.pending else self.msg,
                  "entries": list(self.entries), "tokens": 0, "tokens_today": 0}
            for pid, p in self.pending.items():
                hb["prompt"] = {"id": pid, "tool": p["tool"][:19], "hint": p["hint"][:43]}
                break
            return hb


def describe(tool, ti):
    for k in ("command", "file_path", "path", "pattern", "url", "description"):
        if isinstance(ti.get(k), str):
            return os.path.basename(ti[k]) if k in ("file_path", "path") else ti[k].replace("\n", " ")
    return ""


# ───────────── hook server ─────────────
def serve(state):
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((TCP_HOST, TCP_PORT)); srv.listen(8)
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
        p = {"tool": tool, "hint": describe(tool, ti) or tool, "event": threading.Event(), "decision": None}
        with state.lock:
            state.pending[pid] = p; state.dirty = True
        conn.settimeout(None)
        log("approval requested:", tool, p["hint"][:60])
        # Wait for the buddy, or for the hook to hang up (it gives up after its own timeout).
        while not p["event"].wait(0.5):
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
                        seen.add("w:" + os.path.basename(path)[:-6])
                except OSError:
                    pass
        with state.lock:
            for sid in seen - logged:
                log("watcher: session", sid[:20], "is active (no hooks)")
            logged |= seen
            for sid in seen:
                # Hooked sessions manage their own state; only add the ones we have no hooks for.
                if sid not in state.sessions or sid in state.watched:
                    state.watched.add(sid)
                    s = state.sessions.setdefault(sid, {"state": "idle", "last": 0})
                    if s["state"] != "running": state.dirty = True
                    s["state"], s["last"] = "running", now
            for sid in list(state.watched):
                if sid not in seen and sid in state.sessions:
                    state.sessions[sid]["state"] = "idle"; state.dirty = True
        time.sleep(2)


# ───────────── main loop ─────────────
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port of the buddy (default: auto-detect)")
    ap.add_argument("--watch", nargs="*", default=["~/.claude/projects"],
                    help="directories to watch for session transcripts (pass --watch with nothing to disable)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    state = State()
    threading.Thread(target=serve, args=(state,), daemon=True).start()
    if args.watch:
        threading.Thread(target=watch, args=(state, args.watch), daemon=True).start()
    log(f"hooks connect to {TCP_HOST}:{TCP_PORT}")

    port, last_hb, last_time = None, 0, 0
    while True:
        path = args.port or find_port()
        if not path:
            time.sleep(2); continue
        ser = Serial(path, args.baud)
        try:
            ser.open()
            log("buddy on", path)
            last_hb = last_time = 0
            while True:
                now = time.time()
                for line in ser.lines():
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
                    ser.write((json.dumps({"time": [int(now), off]}) + "\n").encode()); last_time = now
                state.expire()
                if state.dirty or now - last_hb > 3:
                    state.dirty = False
                    ser.write((json.dumps(state.heartbeat(), separators=(",", ":")) + "\n").encode())
                    last_hb = now
                time.sleep(0.05)
        except (OSError, Exception) as e:
            log("serial problem:", e, "- retrying")
        finally:
            ser.close()
        time.sleep(2)


if __name__ == "__main__":
    try: main()
    except KeyboardInterrupt: pass
