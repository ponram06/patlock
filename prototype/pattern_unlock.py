"""
Pattern Unlock - Stage 1 prototype (3x3 grid, like an Android lock screen)

Run:  python pattern_unlock.py
Needs only Python 3 (tkinter ships with the Windows installer).

How to use:
  * First run: draw a pattern (min 4 dots), then draw it again to confirm.
  * After that: draw the pattern to "unlock".
  * Press and hold the left button (or tap-and-drag on the trackpad) and
    move across the dots.

What this proves: capture -> pattern sequence -> salted hash -> verify ->
lockout. The same logic gets reused later inside the Windows service that
sits behind the Credential Provider. This window is NOT real security.
"""

import hashlib
import hmac
import json
import math
import os
import time
import tkinter as tk
from pathlib import Path

CONFIG = Path.home() / ".pattern_unlock.json"
MIN_DOTS = 4
MAX_ATTEMPTS = 5
LOCKOUT_SECONDS = 30
ITERATIONS = 200_000

BG = "#111418"
DOT = "#4a5260"
ACTIVE = "#4da3ff"
BAD = "#ff5c5c"
GOOD = "#4cd137"


# ---------- storage: only a salted hash is saved, never the pattern ----------

def hash_pattern(seq, salt):
    data = "".join(str(d) for d in seq).encode()
    return hashlib.pbkdf2_hmac("sha256", data, salt, ITERATIONS)


def save_pattern(seq):
    salt = os.urandom(16)
    CONFIG.write_text(json.dumps({
        "salt": salt.hex(),
        "hash": hash_pattern(seq, salt).hex(),
    }))


def check_pattern(seq):
    cfg = json.loads(CONFIG.read_text())
    salt = bytes.fromhex(cfg["salt"])
    return hmac.compare_digest(hash_pattern(seq, salt), bytes.fromhex(cfg["hash"]))


# ---------- UI ----------

class PatternLock(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Pattern Unlock (prototype)")
        self.configure(bg=BG)
        self.resizable(False, False)

        self.size = 360
        self.canvas = tk.Canvas(self, width=self.size, height=self.size,
                                bg=BG, highlightthickness=0)
        self.canvas.pack(padx=20, pady=(20, 5))
        self.label = tk.Label(self, fg="#eeeeee", bg=BG, font=("Segoe UI", 12))
        self.label.pack(pady=(0, 5))
        self.change_btn = tk.Button(self, text="Change pattern",
                                    command=self.start_enroll, state="disabled")
        self.change_btn.pack(pady=(0, 15))

        pitch = self.size / 3
        self.centers = [(pitch * (c + 0.5), pitch * (r + 0.5))
                        for r in range(3) for c in range(3)]
        self.hit_radius = 40

        self.path = []
        self.drawing = False
        self.busy = False
        self.pending = None
        self.failed = 0
        self.locked_until = 0.0

        self.canvas.bind("<ButtonPress-1>", self.on_press)
        self.canvas.bind("<B1-Motion>", self.on_move)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)

        if CONFIG.exists():
            self.mode = "verify"
            self.say("Draw your pattern to unlock")
        else:
            self.start_enroll()
        self.redraw()

    # ----- helpers -----

    def say(self, text, color="#eeeeee"):
        self.label.config(text=text, fg=color)

    def start_enroll(self):
        self.mode = "enroll"
        self.pending = None
        self.change_btn.config(state="disabled")
        self.say(f"Draw a new pattern (at least {MIN_DOTS} dots)")
        self.reset_path()

    def reset_path(self):
        self.path = []
        self.drawing = False
        self.busy = False
        self.redraw()

    def add_dot(self, i):
        """Add dot i; like Android, auto-add an unvisited dot we jumped over."""
        if self.path:
            ra, ca = divmod(self.path[-1], 3)
            rb, cb = divmod(i, 3)
            if (ra + rb) % 2 == 0 and (ca + cb) % 2 == 0:
                mid = ((ra + rb) // 2) * 3 + (ca + cb) // 2
                if mid not in self.path:
                    self.path.append(mid)
        self.path.append(i)

    def try_hit(self, x, y):
        for i, (cx, cy) in enumerate(self.centers):
            if i not in self.path and math.hypot(x - cx, y - cy) <= self.hit_radius:
                self.add_dot(i)
                return

    def redraw(self, cursor=None, color=ACTIVE):
        c = self.canvas
        c.delete("all")
        for a, b in zip(self.path, self.path[1:]):
            c.create_line(*self.centers[a], *self.centers[b],
                          fill=color, width=6, capstyle="round")
        if self.drawing and self.path and cursor:
            c.create_line(*self.centers[self.path[-1]], *cursor,
                          fill=color, width=6, capstyle="round")
        for i, (x, y) in enumerate(self.centers):
            lit = i in self.path
            r = 14 if lit else 9
            c.create_oval(x - r, y - r, x + r, y + r,
                          fill=color if lit else DOT, outline="")

    # ----- events -----

    def on_press(self, e):
        if self.busy:
            return
        remaining = self.locked_until - time.time()
        if remaining > 0:
            self.say(f"Too many attempts. Wait {int(remaining) + 1}s", BAD)
            return
        self.path = []
        self.drawing = True
        self.try_hit(e.x, e.y)
        self.redraw((e.x, e.y))

    def on_move(self, e):
        if not self.drawing:
            return
        self.try_hit(e.x, e.y)
        self.redraw((e.x, e.y))

    def on_release(self, e):
        if not self.drawing:
            return
        self.drawing = False
        seq = list(self.path)
        if len(seq) < MIN_DOTS:
            self.flash(BAD, f"Connect at least {MIN_DOTS} dots")
            return
        if self.mode == "enroll":
            self.handle_enroll(seq)
        else:
            self.handle_verify(seq)

    # ----- logic -----

    def handle_enroll(self, seq):
        if self.pending is None:
            self.pending = seq
            self.flash(ACTIVE, "Draw it again to confirm")
        elif seq == self.pending:
            save_pattern(seq)
            self.mode = "verify"
            self.pending = None
            self.flash(GOOD, "Pattern saved. Draw it to unlock")
        else:
            self.pending = None
            self.flash(BAD, "Didn't match. Draw a new pattern")

    def handle_verify(self, seq):
        if check_pattern(seq):
            self.failed = 0
            self.mode = "unlocked"
            self.change_btn.config(state="normal")
            self.flash(GOOD, "Unlocked \u2714  (this is where the OS unlock would fire)")
        else:
            self.failed += 1
            if self.failed >= MAX_ATTEMPTS:
                self.failed = 0
                self.locked_until = time.time() + LOCKOUT_SECONDS
                self.flash(BAD, f"Locked for {LOCKOUT_SECONDS}s")
            else:
                left = MAX_ATTEMPTS - self.failed
                self.flash(BAD, f"Wrong pattern ({left} tries left)")

    def flash(self, color, text):
        """Show the finished path in a colour for a moment, then clear it."""
        self.busy = True
        self.say(text, color)
        self.redraw(color=color)

        def done():
            self.reset_path()
            if self.mode == "verify" and time.time() >= self.locked_until:
                self.say("Draw your pattern to unlock")
            elif self.mode == "enroll":
                self.say("Draw it again to confirm" if self.pending
                         else f"Draw a new pattern (at least {MIN_DOTS} dots)")
        self.after(900, done)


if __name__ == "__main__":
    PatternLock().mainloop()
