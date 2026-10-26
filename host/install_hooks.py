#!/usr/bin/env python3
"""Adds the buddy hooks to ~/.claude/settings.json (or just prints them).

    python3 install_hooks.py            # show what would be added
    python3 install_hooks.py --apply    # merge into ~/.claude/settings.json (backup kept)
    python3 install_hooks.py --remove   # take them out again
"""
import json, os, shutil, sys

HOOK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "buddy_hook.py")
CMD = f"python3 {HOOK}"
EVENTS = ["UserPromptSubmit", "PreToolUse", "PostToolUse", "Notification", "Stop", "SessionStart", "SessionEnd"]
SETTINGS = os.path.expanduser("~/.claude/settings.json")


def ours(group):
    return any(h.get("command", "") == CMD for h in group.get("hooks", []))


def main():
    apply, remove = "--apply" in sys.argv, "--remove" in sys.argv
    cfg = json.load(open(SETTINGS)) if os.path.exists(SETTINGS) else {}
    hooks = cfg.setdefault("hooks", {})
    for ev in EVENTS + ["PermissionRequest"]:
        groups = [g for g in hooks.get(ev, []) if not ours(g)]
        if not remove:
            timeout = 100 if ev == "PermissionRequest" else 5
            groups.append({"matcher": "*", "hooks": [{"type": "command", "command": CMD, "timeout": timeout}]})
        if groups: hooks[ev] = groups
        else: hooks.pop(ev, None)
    if not hooks: cfg.pop("hooks")
    text = json.dumps(cfg, indent=2) + "\n"
    if not (apply or remove):
        print(text); print("(not written; run with --apply to save)", file=sys.stderr); return
    os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
    if os.path.exists(SETTINGS): shutil.copy(SETTINGS, SETTINGS + ".bak")
    open(SETTINGS, "w").write(text)
    print("updated", SETTINGS)


if __name__ == "__main__":
    main()
