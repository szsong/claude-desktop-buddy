#!/usr/bin/env python3
"""Runs buddy_bridge.py with a status window: is the buddy connected, what the
sessions are doing, any approval waiting, and the bridge's log. Closing the
window stops the bridge. host/install_shortcut.py puts a launcher on the Desktop.
"""
import os, sys, threading, time, tkinter as tk
from tkinter import font as tkfont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buddy_bridge as bb

# The buddy's own status colours
RED, GREEN, SKY, GREY = "#e5534b", "#3fb950", "#6cb6e6", "#8b949e"


def clawd_icon(root, size=1024):
    """Clawd on a dark rounded tile, the same block shape as the firmware pet:
    16 x 5 quarter cells, each 4 x 8 units (a terminal cell is twice as tall as wide)."""
    img = tk.PhotoImage(master=root, width=size, height=size)
    tile, margin = "#1f1f1f", size * 80 // 1024
    r, lo, hi = size * 190 // 1024, margin, size - margin
    for y in range(lo, hi):                                   # rounded square, a row at a time
        dy = max(lo + r - y, y - (hi - 1 - r), 0)
        dx = r - int((r * r - dy * dy) ** 0.5) if dy else 0
        img.put(tile, to=(lo + dx, y, hi - dx, y + 1))
    u = size * 10 // 1024                                     # px per unit
    x0, y0 = (size - 64 * u) // 2, (size - 40 * u) // 2
    def quarter(c, row, color="#da7756", w=1, h=1):
        img.put(color, to=(x0 + c * 4 * u, y0 + row * 8 * u, x0 + (c + w) * 4 * u, y0 + (row + h) * 8 * u))
    quarter(2, 0, w=12, h=4)                                  # body
    for c, row in ((0, 2), (1, 2), (14, 2), (15, 2)): quarter(c, row)        # arms
    for c in (3, 5, 10, 12): quarter(c, 4)                    # legs
    quarter(4, 1, tile); quarter(11, 1, tile)                 # eyes
    return img


class Window:
    def __init__(self, root, state):
        self.root, self.state, self.seen = root, state, 0
        root.title("Claude Buddy Bridge")
        root.geometry("620x440")
        root.minsize(480, 320)
        big = tkfont.Font(size=15, weight="bold")
        mono = tkfont.Font(family="Menlo", size=11)

        top = tk.Frame(root, padx=14, pady=10)
        top.pack(fill="x")
        self.status = tk.Label(top, font=big, anchor="w")
        self.status.grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 6))
        self.rows = {}
        for i, name in enumerate(("Buddy", "Hooks", "Sessions", "Approval", "Heartbeat"), start=1):
            tk.Label(top, text=name, fg=GREY, anchor="w", width=10).grid(row=i, column=0, sticky="w")
            self.rows[name] = tk.Label(top, anchor="w", justify="left")
            self.rows[name].grid(row=i, column=1, sticky="w")
        self.rows["Hooks"].config(text=f"listening on {bb.TCP_HOST}:{bb.TCP_PORT}")
        self.fg = self.rows["Hooks"].cget("fg")    # the theme's normal text colour

        logf = tk.Frame(root, padx=14)
        logf.pack(fill="both", expand=True)
        self.log = tk.Text(logf, font=mono, height=10, wrap="none", state="disabled",
                           borderwidth=1, relief="solid", highlightthickness=0)
        bar = tk.Scrollbar(logf, command=self.log.yview)
        self.log.config(yscrollcommand=bar.set)
        bar.pack(side="right", fill="y")
        self.log.pack(side="left", fill="both", expand=True)

        bottom = tk.Frame(root, padx=14, pady=10)
        bottom.pack(fill="x")
        tk.Button(bottom, text="Stop bridge", command=root.destroy).pack(side="right")
        tk.Label(bottom, text="Closing this window stops the bridge.", fg=GREY).pack(side="left")
        self.tick()

    def tick(self):
        s = self.state
        hb = s.heartbeat()
        link = s.link
        if not link:
            self.status.config(text="Looking for the buddy…", fg=GREY)
        elif hb["waiting"]:
            self.status.config(text="Needs your input", fg=RED)
        elif hb["running"]:
            self.status.config(text="Claude is working", fg=GREEN)
        else:
            self.status.config(text="Idle", fg=SKY)

        self.rows["Buddy"].config(text=f"connected over {link}" if link else "not found yet: plug it in by USB, or switch it on (Bluetooth)",
                                  fg=GREEN if link else RED)
        self.rows["Sessions"].config(
            text=f"{hb['total']} total · {hb['running']} running · {hb['waiting']} waiting")
        prompt = hb.get("prompt")
        self.rows["Approval"].config(text=f"{prompt['tool']}: {prompt['hint']}" if prompt else "none",
                                     fg=RED if prompt else self.fg)
        ago = time.time() - s.last_hb
        self.rows["Heartbeat"].config(text=f"sent {ago:.0f} s ago" if link and s.last_hb else "-")

        new = [line for seq, line in list(bb.LOG) if seq > self.seen]
        if new:
            self.seen = bb.LOG[-1][0]
            at_end = self.log.yview()[1] > 0.999
            self.log.config(state="normal")
            self.log.insert("end", "\n".join(new) + "\n")
            self.log.delete("1.0", "end-1000l")             # keep the last 1000 lines
            self.log.config(state="disabled")
            if at_end: self.log.see("end")
        self.root.after(500, self.tick)


def main():
    root = tk.Tk()
    root.iconphoto(True, clawd_icon(root, 256))   # the Dock icon while it runs
    try:
        srv = bb.listen()
    except OSError:
        root.title("Claude Buddy Bridge")
        tk.Label(root, padx=20, pady=16, justify="left",
                 text=f"Another bridge is already running ({bb.TCP_HOST}:{bb.TCP_PORT} is in use).\n"
                      "Close it first (its window, or Ctrl-C in its terminal), then start this again.").pack()
        tk.Button(root, text="OK", command=root.destroy).pack(pady=(0, 14))
        root.mainloop()
        return
    state = bb.State()
    bb.start(state, srv)
    threading.Thread(target=bb.link_loop, args=(state,), daemon=True).start()
    Window(root, state)
    root.mainloop()


if __name__ == "__main__":
    main()
