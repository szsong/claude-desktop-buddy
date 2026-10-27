# Run the buddy without the Claude desktop Hardware Buddy window

For computers where the desktop app has no **Developer → Open Hardware Buddy…**.
The buddy firmware already understands the same messages over its USB serial
port and over Bluetooth, so no new firmware is needed: plug the buddy in by USB,
or just switch it on, and run a bridge.

```
Claude Code hooks ──► buddy_hook.py ──► buddy_bridge.py ──USB or Bluetooth──► buddy
transcript watcher ───────────────────►        ▲                          │
                                               └── approve / deny ◄───────┘
```

* `buddy_bridge.py` keeps track of sessions, sends the buddy a heartbeat every
  3 s (and on every change), syncs the clock, and passes approvals back.
* `buddy_hook.py` is the Claude Code hook. It forwards each event to the bridge.
  On a permission request it waits for the buddy: **tap = allow, double-tap = deny**.
  If the bridge isn't running or nobody answers within 90 s, Claude Code asks as usual.
* The watcher (on by default) treats any `~/.claude/projects/**/*.jsonl` written in the
  last 15 s as a running session. This covers sessions without hooks, but it can't
  see approvals or tool names.

## Use it

1. Plug the buddy in by USB, or switch it on within Bluetooth range. Only Python 3.8+ is
   needed for USB (`pip install pyserial` is optional); Bluetooth needs `pip install bleak`.
   The bridge uses USB whenever the cable is in and falls back to Bluetooth otherwise; if
   you plug the cable in while it is on Bluetooth, it moves to USB. `--transport usb|ble`
   forces one, `--scan-idle` keeps searching over Bluetooth while Claude Code is idle (by default the
   search pauses then, and starts as soon as a session works or asks for you), and `--ble Claude-1234` picks a buddy when several are in range. In the status window, **Connect over Bluetooth** drops USB and
   searches over Bluetooth at once, even while Claude Code is idle, and stays there until you press
   **Back to automatic**. The first
   Bluetooth connection may ask you to pair: type the 6-digit code the buddy shows.
2. Start the bridge and leave it running: `python3 host/buddy_bridge.py`
   (`--port /dev/cu.usbmodemXXXX` if auto-detection picks the wrong board, `--watch` with
   no directories turns the watcher off).
   Or, on a Mac, run `python3 host/install_shortcut.py` once: it puts **Claude Buddy Bridge**
   on the Desktop, which starts the bridge with a status window (buddy connection, sessions,
   pending approval, log). Closing the window stops the bridge; `--remove` deletes the shortcut.
3. Add the hooks: `python3 host/install_hooks.py` shows the change,
   `--apply` writes it to `~/.claude/settings.json` (keeping a `.bak`), `--remove` undoes it.
   Restart Claude Code sessions afterwards.

The light on the buddy: sky blue idle, yellow thinking, green coding, red needs input.

## Limits

* Opening the USB port restarts the board once, so start the bridge before you rely on it.
* Only one computer can hold the Bluetooth link, and not alongside the desktop app's
  Hardware Buddy window. Close one before using the other.
* Hooks cover Claude Code, including the sessions behind the desktop app's code tab. Whether
  Cowork runs hooks isn't known; the watcher is the fallback there.
* The desktop protocol's token counters aren't available this way (they show 0).
* The bridge listens only on 127.0.0.1:47321.
