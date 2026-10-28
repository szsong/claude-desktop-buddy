#!/usr/bin/env python3
"""Claude Code hook: forwards the event on stdin to buddy_bridge.py.

For PermissionRequest it waits for the buddy's answer (tap = allow, double-tap
= deny) and prints the decision Claude Code expects. If the bridge isn't
running, or nobody answers within the timeout, it prints nothing and Claude
Code asks in the usual way. It never blocks other events for more than a moment.
"""
import json, socket, sys

HOST, PORT = "127.0.0.1", 47321
WAIT = 90   # seconds to wait for the buddy; keep below the hook timeout in settings


def main():
    try:
        ev = json.load(sys.stdin)
    except ValueError:
        return
    permission = ev.get("hook_event_name") == "PermissionRequest"
    try:
        s = socket.create_connection((HOST, PORT), timeout=0.5)
        s.sendall((json.dumps(ev) + "\n").encode())
        s.settimeout(WAIT if permission else 0.5)
        reply = b""
        while not reply.endswith(b"\n"):
            chunk = s.recv(4096)
            if not chunk: break
            reply += chunk
    except OSError:
        return
    if permission:
        try:
            decision = json.loads(reply.decode()).get("decision")
        except ValueError:
            return
        if decision in ("allow", "deny"):
            out = {"behavior": decision}
            if decision == "deny":
                out["message"] = "Denied from the buddy"
            print(json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": out}}))


if __name__ == "__main__":
    main()
