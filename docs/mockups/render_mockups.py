#!/usr/bin/env python3
"""Render Grok Bot Companion UI mockups (466x466, round-masked PNGs).

Layout numbers mirror main/ui/*.c + ui_theme.h so the mockups match the firmware.
Avatars are the same procedural shapes/colors as relay/bots.json (not the Grok Bot
app's real art). Run:  python3 docs/mockups/render_mockups.py
"""
from __future__ import annotations

import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = HERE
S = 3                      # supersampling factor
W = H = 466
CX, CY = W // 2, H // 2

# --- fonts (Montserrat = LVGL's built-in font family) -------------------------
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/sand-box/google/Montserrat/Montserrat-VariableFont_wght.ttf",
    "/tmp/lvgl94/scripts/built_in_font/Montserrat-Medium.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]


def font(size: int, weight: str = "Medium") -> ImageFont.FreeTypeFont:
    for path in FONT_CANDIDATES:
        if Path(path).exists():
            f = ImageFont.truetype(path, size * S)
            try:
                f.set_variation_by_name(weight)
            except Exception:
                pass
            return f
    return ImageFont.load_default()


F14, F16, F20, F28 = font(14), font(16), font(20, "SemiBold"), font(28, "Bold")
F14B = font(14, "SemiBold")

# --- palette (ui_theme.h) ------------------------------------------------------
BG = (0, 0, 0)
SURFACE = (0x15, 0x17, 0x1C)
SURFACE2 = (0x22, 0x25, 0x2D)
TEXT = (0xF4, 0xF5, 0xF7)
MUTED = (0x9A, 0xA0, 0xAA)
DIM = (0x5B, 0x61, 0x6B)
OK = (0x22, 0xC5, 0x5E)
WARN = (0xF5, 0x9E, 0x0B)
ERR = (0xEF, 0x44, 0x44)
ACCENT = (0x60, 0xA5, 0xFA)
INK = (0x0B, 0x12, 0x20)

BOTS = {b["id"]: b for b in json.loads((ROOT / "relay/bots.json").read_text())["bots"]}
VOICES = json.loads((ROOT / "relay/voices.json").read_text())["voices"]
ORDER = list(BOTS)


def hexrgb(h: str) -> tuple[int, int, int]:
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def s(v: float) -> int:
    return int(round(v * S))


def blend(c, bg, a: float):
    return tuple(int(c[i] * a + bg[i] * (1 - a)) for i in range(3))


# --- primitives ----------------------------------------------------------------
class Canvas:
    def __init__(self):
        self.img = Image.new("RGB", (W * S, H * S), BG)
        self.d = ImageDraw.Draw(self.img)

    def rrect(self, x, y, w, h, r, fill=None, outline=None, width=0):
        self.d.rounded_rectangle([s(x), s(y), s(x + w), s(y + h)], radius=s(r), fill=fill,
                                 outline=outline, width=s(width) if width else 0)

    def circle(self, cx, cy, r, fill=None, outline=None, width=0):
        self.d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=fill, outline=outline,
                       width=s(width) if width else 0)

    def text(self, x, y, txt, f, fill, anchor="mt"):
        self.d.text((s(x), s(y)), txt, font=f, fill=fill, anchor=anchor)

    def text_w(self, txt, f) -> float:
        return self.d.textlength(txt, font=f) / S

    def grabber(self, y):
        self.rrect(CX - 20, y, 40, 5, 3, fill=DIM)

    # tiny vector icons (LVGL uses FontAwesome symbols on device)
    def wifi(self, x, y, col):
        for i, r in enumerate((10, 6.5, 3)):
            self.d.arc([s(x - r), s(y - r), s(x + r), s(y + r)], 225, 315, fill=col, width=s(1.8))
        self.circle(x, y - 0.5, 1.4, fill=col)

    def battery(self, x, y, pct, col, charging=False):
        self.rrect(x, y - 6, 20, 11, 2.5, outline=col, width=1.5)
        self.rrect(x + 20.5, y - 2.5, 2, 4, 1, fill=col)
        self.rrect(x + 2.5, y - 3.5, 15 * pct / 100, 6, 1, fill=col)

    def finish(self, path: Path, dim: float = 1.0):
        img = self.img
        if dim < 1.0:
            img = Image.eval(img, lambda v: int(v * dim))
        img = img.resize((W, H), Image.LANCZOS)
        mask = Image.new("L", (W * S, H * S), 0)
        ImageDraw.Draw(mask).ellipse([0, 0, W * S - 1, H * S - 1], fill=255)
        mask = mask.resize((W, H), Image.LANCZOS)
        out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        out.paste(img, (0, 0), mask)
        ring = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
        ImageDraw.Draw(ring).ellipse([s(1), s(1), W * S - s(1), H * S - s(1)],
                                     outline=(60, 64, 72, 255), width=s(2))
        out = Image.alpha_composite(out, ring.resize((W, H), Image.LANCZOS))
        out.save(path)
        return out


# --- avatar (mirrors ui_avatar.c) -----------------------------------------------
NO_SHINE = {"diamond", "triangle", "star", "ring"}   # mirrors shape_face_t.shine
FACE = {  # eye_dy, eye_gap, eye_w, eye_h
    "circle": (-8, 24, 16, 22), "squircle": (-8, 26, 16, 22), "hexagon": (-6, 24, 16, 22),
    "diamond": (-4, 20, 14, 20), "triangle": (14, 18, 14, 18), "star": (-2, 16, 12, 18),
    "ring": (-6, 18, 12, 18), "pill": (-6, 30, 16, 20), "octagon": (-8, 24, 16, 22),
    "blob": (-2, 20, 14, 20),
}


def poly(n, cx, cy, r, start, inner=0):
    steps = n * 2 if inner else n
    pts = []
    for i in range(steps):
        a = math.radians(start + 360 * i / steps)
        rr = inner if (inner and i % 2) else r
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts


def is_light(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000 > 150


def avatar(cv: Canvas, bot: dict, cx: float, cy: float, scale: float = 1.0, opa: float = 1.0,
           anim: str = "idle", level: float = 0.0, t: float = 0.0, bg=BG):
    size = 150 * scale
    if anim == "idle":
        size *= 1.03
    if anim == "speaking":
        size *= 1 + 0.19 * level
    col = blend(hexrgb(bot["color"]), bg, opa)
    acc = blend(hexrgb(bot["accent"]), bg, opa)
    if anim == "error":
        col = blend((0xDC, 0x26, 0x26), bg, opa)
    r = size / 2
    shape = bot["shape"]
    d = cv.d

    # animation layers behind the body
    if anim == "listening":
        for i, k in enumerate((0.15, 0.5, 0.85)):
            rr = (150 + (250 - 150) * k) / 2 * scale
            a = 0.86 * (1 - k)
            cv.circle(cx, cy, rr, outline=blend(acc, bg, a), width=3)
    face_dy = 0
    if shape == "squircle":
        cv.rrect(cx - r, cy - r, size, size, size * 0.28, fill=col)
    elif shape == "pill":
        cv.rrect(cx - r, cy - size * 0.33, size, size * 0.66, size * 0.33, fill=col)
    elif shape == "blob":
        cv.rrect(cx - size * 0.40, cy - r, size * 0.80, size, size * 0.40, fill=col)
    elif shape == "ring":
        cv.circle(cx, cy, r, outline=col, width=size * 0.17 / 1)
    elif shape == "circle":
        cv.circle(cx, cy, r, fill=col)
    else:
        if shape == "hexagon":
            pts = poly(6, cx, cy, r, -90)
        elif shape == "octagon":
            pts = poly(8, cx, cy, r, -90 + 22)
        elif shape == "diamond":
            pts = poly(4, cx, cy, r, -90)
        elif shape == "triangle":
            pts = poly(3, cx, cy + r / 6, r + r / 8, -90)
        else:  # star
            pts = poly(5, cx, cy, r, -90, r * 0.5)
        d.polygon([(s(x), s(y)) for x, y in pts], fill=col)
    if shape not in NO_SHINE:
        cv.d.ellipse([s(cx - size * 0.30), s(cy - size * 0.34), s(cx - size * 0.08), s(cy - size * 0.20)],
                     fill=blend(acc, col, 0.4))
    # face
    fdy, gap, ew, eh = FACE[shape]
    k = size / 150
    eye = col if shape == "ring" else ((0x11, 0x13, 0x18) if is_light(hexrgb(bot["color"])) else (255, 255, 255))
    eye = blend(eye, bg, opa) if shape != "ring" else col
    for sx in (-1, 1):
        ex, ey = cx + sx * gap * k, cy + fdy * k
        cv.rrect(ex - ew * k / 2, ey - eh * k / 2, ew * k, eh * k, ew * k / 2, fill=eye)
    if anim == "speaking":
        mh = (4 + 20 * level) * k
        cv.rrect(cx - 15 * k, cy + (fdy + 30) * k - mh / 2, 30 * k, mh, mh / 2, fill=eye)
    # orbit dots in front
    if anim in ("thinking", "working"):
        rad = 150 / 2 * scale + 26
        dcol = TEXT if anim == "thinking" else hexrgb(bot["accent"])
        for i in range(3):
            a = math.radians(t + i * 120)
            dr = (14 - i * 3) / 2
            cv.circle(cx + rad * math.cos(a), cy + rad * math.sin(a), dr, fill=dcol)


# --- screens ---------------------------------------------------------------------
def status_bar(cv: Canvas):
    cv.grabber(10)
    cv.text(CX, 24, "1:21", F20, TEXT)
    total = 20 + 16 + 24 + 6 + cv.text_w("82%", F14)
    x0 = CX - total / 2
    cv.wifi(x0 + 10, 64, MUTED)
    cv.battery(x0 + 36, 58, 82, MUTED)
    cv.text(x0 + 66, 51, "82%", F14, MUTED, anchor="lt")


def home(bot_id: str, state: str, status: str, offset: float = 0.0, t: float = 30,
         level: float = 0.5, hint: str = "Hold BOOT to talk") -> Canvas:
    cv = Canvas()
    status_bar(cv)
    idx = ORDER.index(bot_id)
    n = len(ORDER)
    spacing, peek_s, peek_o = 200, 140 / 256, 110 / 255
    anim = {"idle": "idle", "listening": "listening", "thinking": "thinking", "working": "working",
            "speaking": "speaking", "error": "error"}[state]
    slots = []
    for k in range(-2, 3):
        x = CX + k * spacing + offset
        dist = min(abs(x - CX), spacing) / spacing
        sc = 1 - (1 - peek_s) * dist
        op = 1 - (1 - peek_o) * dist
        slots.append((dist, k, x, sc, op))
    for dist, k, x, sc, op in sorted(slots, reverse=True):   # draw center last
        b = BOTS[ORDER[(idx + k) % n]]
        avatar(cv, b, x, 178, sc, op, anim if k == 0 and offset == 0 else "static", level, t)
    name_opa = 1 - min(abs(offset), spacing / 2) / (spacing / 2)
    cv.text(CX, 290, BOTS[bot_id]["name"], F28, blend(TEXT, BG, name_opa))
    dot = {"idle": DIM, "listening": OK, "thinking": ACCENT, "working": WARN,
           "speaking": (0xA7, 0x8B, 0xFA), "error": ERR}[state]
    tw = cv.text_w(status, F16)
    x0 = CX - (tw + 18) / 2
    cv.circle(x0 + 5, 344, 5, fill=dot)
    cv.text(x0 + 18, 335, status, F16, TEXT, anchor="lt")
    cv.text(CX, 366, hint, F14, MUTED)
    # page dots
    widths = [16 if i == idx else 6 for i in range(n)]
    tot = sum(widths) + 6 * (n - 1)
    x = CX - tot / 2
    for i, wdt in enumerate(widths):
        colr = hexrgb(BOTS[ORDER[i]]["color"]) if i == idx else DIM
        cv.rrect(x, 402, wdt, 6, 3, fill=colr)
        x += wdt + 6
    cv.grabber(H - 18)
    return cv


def seg(cv, x, y, w, h, labels, sel, font_=F14):
    gap = 6
    bw = (w - gap * (len(labels) - 1)) / len(labels)
    for i, lab in enumerate(labels):
        bx = x + i * (bw + gap)
        on = i == sel
        cv.rrect(bx, y, bw, h, 12, fill=ACCENT if on else SURFACE2)
        cv.text(bx + bw / 2, y + h / 2, lab, font_, INK if on else MUTED, anchor="mm")


def slider(cv, x, y, w, val, lo, hi):
    cv.rrect(x, y, w, 10, 5, fill=SURFACE2)
    fx = x + w * (val - lo) / (hi - lo)
    cv.rrect(x, y, fx - x, 10, 5, fill=ACCENT)
    cv.circle(fx, y + 5, 11, fill=TEXT)


def bot_panel(bot_id: str, voice_name: str, conv_active: bool, scroll: float = 0) -> Canvas:
    cv = Canvas()
    b = BOTS[bot_id]
    x, w = CX - 170, 340
    y = 84 - scroll
    # talk mode card
    cv.rrect(x, y, w, 95, 18, fill=SURFACE)
    cv.text(x + 12, y + 12, "Talk mode", F14, MUTED, anchor="lt")
    seg(cv, x + 12, y + 37, w - 24, 46, ["Press to talk", "Always listen"], 0)
    y += 105
    # conversation button
    cv.rrect(x, y, w, 52, 26, fill=ERR if conv_active else OK)
    cv.text(CX, y + 26, ("End conversation" if conv_active else "Start conversation"), F16,
            (0x07, 0x13, 0x0B), anchor="mm")
    sym_x = CX - cv.text_w("Start conversation", F16) / 2 - 18
    if conv_active:
        cv.rrect(sym_x - 6, y + 20, 12, 12, 2, fill=(0x07, 0x13, 0x0B))
    else:
        cv.d.polygon([(s(sym_x - 5), s(y + 19)), (s(sym_x - 5), s(y + 33)), (s(sym_x + 7), s(y + 26))],
                     fill=(0x07, 0x13, 0x0B))
    y += 62
    # voice row
    cv.rrect(x, y, w, 56, 18, fill=SURFACE)
    # little speaker-note glyph
    cv.circle(x + 20, y + 33, 4, fill=TEXT)
    cv.rrect(x + 23, y + 17, 2.5, 16, 1, fill=TEXT)
    cv.d.polygon([(s(x + 25), s(y + 17)), (s(x + 33), s(y + 21)), (s(x + 25), s(y + 24))], fill=TEXT)
    cv.text(x + 42, y + 28, "Voice", F16, TEXT, anchor="lm")
    cv.text(x + w - 30, y + 28, voice_name, F14, MUTED, anchor="rm")
    cv.text(x + w - 14, y + 28, ">", F16, MUTED, anchor="rm")
    y += 66
    # volume
    cv.rrect(x, y, w, 72, 18, fill=SURFACE)
    cv.text(x + 12, y + 12, "Volume  70%", F14, MUTED, anchor="lt")
    slider(cv, x + 18, y + 44, w - 36, 70, 0, 100)
    y += 82
    cv.rrect(x, y, w, 72, 18, fill=SURFACE)
    cv.text(x + 12, y + 12, "Brightness  80%", F14, MUTED, anchor="lt")
    slider(cv, x + 18, y + 44, w - 36, 80, 5, 100)
    y += 82
    cv.rrect(x, y, w, 90, 18, fill=SURFACE)
    cv.text(x + 12, y + 12, "Auto-sleep", F14, MUTED, anchor="lt")
    seg(cv, x + 12, y + 38, w - 24, 40, ["15s", "30s", "1m", "5m", "Never"], 1)
    # header drawn last (content scrolls under it)
    cv.d.rectangle([0, 0, W * S, s(84)], fill=BG)
    cv.grabber(12)
    tw = cv.text_w(b["name"], F20)
    cv.circle(CX - (tw + 22) / 2 + 7, 46, 7, fill=hexrgb(b["color"]))
    cv.text(CX - (tw + 22) / 2 + 22, 34, b["name"], F20, TEXT, anchor="lt")
    return cv


def voice_picker(bot_id: str, selected: int) -> Canvas:
    cv = Canvas()
    cv.grabber(12)
    cv.text(CX, 36, f"Voice - {BOTS[bot_id]['name']}", F20, TEXT)
    x, w, y, rowh = CX - 150, 300, 82, 34
    cv.rrect(x, y, w, rowh * 4 + 20, 18, fill=SURFACE)
    # roller: selected row in the middle band
    mid = y + 10 + rowh * 1.5
    cv.rrect(x + 8, mid - rowh / 2, w - 16, rowh, 10, fill=ACCENT)
    for off in (-2, -1, 0, 1, 2):
        i = selected + off
        if 0 <= i < len(VOICES):
            yy = mid + off * rowh
            if y + 6 < yy < y + rowh * 4 + 14:
                cv.text(CX, yy, VOICES[i]["name"], F16, INK if off == 0 else MUTED, anchor="mm")
    # desc
    desc = VOICES[selected]["description"]
    words, lines, cur = desc.split(), [], ""
    for wd in words:
        if cv.text_w((cur + " " + wd).strip(), F14) > 300:
            lines.append(cur)
            cur = wd
        else:
            cur = (cur + " " + wd).strip()
    lines.append(cur)
    for i, ln in enumerate(lines[:3]):
        cv.text(CX, 262 + i * 18, ln, F14, TEXT)
    # buttons
    cv.rrect(CX - 150, 316, 140, 48, 24, fill=SURFACE2)
    cv.d.polygon([(s(CX - 122), s(332)), (s(CX - 122), s(348)), (s(CX - 109), s(340))], fill=TEXT)
    cv.text(CX - 70, 340, "Preview", F16, TEXT, anchor="mm")
    cv.rrect(CX + 10, 316, 140, 48, 24, fill=ACCENT)
    cv.text(CX + 80, 340, "Select", F16, INK, anchor="mm")
    cv.text(CX, 384, "Cancel", F14, MUTED)
    cv.text(CX, 410, "Placeholder voices - reply side", F14, DIM)
    cv.text(CX, 428, "picks the real voice", F14, DIM)
    return cv


def device_panel() -> Canvas:
    cv = Canvas()
    cv.text(CX, 34, "1:21", F28, TEXT)
    cv.text(CX, 70, "Wed Oct 07 - SNTP synced", F14, MUTED)
    x, w = CX - 165, 330
    y = 96
    cv.rrect(x, y, w, 85, 18, fill=SURFACE)
    cv.wifi(x + 20, y + 26, MUTED)
    cv.text(x + 36, y + 12, "Wi-Fi", F14, MUTED, anchor="lt")
    cv.text(x + 12, y + 35, "HomeNetwork", F16, TEXT, anchor="lt")
    cv.text(x + 12, y + 58, "Connected - Good -61 dBm - 192.168.1.42", F14, MUTED, anchor="lt")
    y += 93
    cv.rrect(x, y, w, 97, 18, fill=SURFACE)
    cv.battery(x + 12, y + 19, 82, MUTED)
    cv.text(x + 40, y + 12, "Battery", F14, MUTED, anchor="lt")
    cv.text(x + 12, y + 35, "82%", F16, TEXT, anchor="lt")
    cv.rrect(x + 12, y + 60, w - 24, 8, 4, fill=SURFACE2)
    cv.rrect(x + 12, y + 60, (w - 24) * 0.82, 8, 4, fill=OK)
    cv.text(x + 12, y + 74, "On battery - 3986 mV", F14, MUTED, anchor="lt")
    y += 105
    cv.rrect(x, y, w, 102, 18, fill=SURFACE)
    cv.text(x + 12, y + 12, "About", F14, MUTED, anchor="lt")
    for i, (k, v) in enumerate((("Firmware", "0.2.0"), ("Device ID", "pocket-001"), ("Relay", "reachable"))):
        cv.text(x + 12, y + 34 + i * 20, k, F14, MUTED, anchor="lt")
        cv.text(x + 110, y + 34 + i * 20, v, F14B, TEXT, anchor="lt")
    cv.grabber(H - 19)
    return cv


def lock_frame() -> Canvas:
    """ui.c lock overlay: opaque black + hint, shown dimmed for MUSE_DIM_BEFORE_SLEEP_MS."""
    cv = Canvas()
    gy = CY - 30
    cv.d.arc([s(CX - 20), s(gy - 14), s(CX + 20), s(gy + 14)], 20, 160, fill=MUTED, width=s(3))
    for dx in (-14, 0, 14):
        cv.d.line([s(CX + dx), s(gy + 12), s(CX + dx * 1.35), s(gy + 22)], fill=MUTED, width=s(2.5))
    for i, ln in enumerate(("Sleeping", "press BOOT to wake", "hold BOOT to talk")):
        cv.text(CX, CY + 18 + i * 24, ln, F16, MUTED)
    return cv


def strip(frames: list[Image.Image], labels: list[str], path: Path):
    pad, lab_h = 24, 40
    out = Image.new("RGBA", (len(frames) * (W + pad) + pad, H + lab_h + pad * 2), (12, 13, 16, 255))
    d = ImageDraw.Draw(out)
    lf = ImageFont.truetype(FONT_CANDIDATES[0], 18) if Path(FONT_CANDIDATES[0]).exists() else None
    for i, (fr, lab) in enumerate(zip(frames, labels)):
        x = pad + i * (W + pad)
        out.alpha_composite(fr, (x, pad))
        d.text((x + W / 2, pad + H + 14), lab, fill=(200, 204, 212), font=lf, anchor="mt")
    out.save(path)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    made = []
    # (a) home / carousel: Nexus is the LAST bot; Meridian (first) peeks on the right = wrap
    p = OUT / "01_home_carousel.png"
    home("nexus", "working", "Nexus is working on it", t=35,
         hint="Conversation open - hold BOOT").finish(p)
    made.append(p)
    # wrap sequence: Nexus → (mid-slide) → Meridian, no end stop
    f1 = home("nexus", "idle", "Ready").finish(OUT / "_tmp1.png")
    f2 = home("nexus", "idle", "Ready", offset=-100).finish(OUT / "_tmp2.png")
    f3 = home("meridian", "idle", "Ready").finish(OUT / "_tmp3.png")
    p = OUT / "01b_carousel_wrap_sequence.png"
    strip([f1, f2, f3], ["Nexus (last bot)", "swipe left - slides continuously", "Meridian (first bot) - Nexus peeks left"], p)
    for t in ("_tmp1.png", "_tmp2.png", "_tmp3.png"):
        (OUT / t).unlink()
    made.append(p)
    # (b) swipe-up per-bot panel (with Voice row) + scrolled
    p = OUT / "02_bot_panel.png"
    bot_panel("nexus", "Calm (placeholder)", conv_active=True).finish(p)
    made.append(p)
    p = OUT / "02b_bot_panel_scrolled.png"
    bot_panel("nexus", "Calm (placeholder)", conv_active=True, scroll=150).finish(p)
    made.append(p)
    # voice picker open
    p = OUT / "03_voice_picker.png"
    voice_picker("nexus", 1).finish(p)
    made.append(p)
    # (c) swipe-down device settings
    p = OUT / "04_device_settings.png"
    device_panel().finish(p)
    made.append(p)
    # lock / sleep transition
    p = OUT / "05_sleep_lock.png"
    lock_frame().finish(p, dim=0.6)   # dimmed panel; then the AMOLED turns off
    made.append(p)
    # avatar sheet: every bot + every animation state (reference for the renderer)
    states = ["idle", "listening", "thinking", "working", "speaking", "error"]
    cell = 200
    sheet = Image.new("RGB", (cell * len(states) * S, cell * len(ORDER) * S // 2 * 0 + cell * S), BG)
    rows = []
    for bid in ORDER:
        cv = Canvas()
        cv.img = Image.new("RGB", (cell * len(states) * S, cell * S), BG)
        cv.d = ImageDraw.Draw(cv.img)
        for j, st in enumerate(states):
            avatar(cv, BOTS[bid], cell * j + cell / 2, cell / 2 - 10, 0.8, 1.0, st, 0.6, 40)
            cv.text(cell * j + cell / 2, cell - 26, f"{BOTS[bid]['name']} - {st}", F14, MUTED)
        rows.append(cv.img.resize((cell * len(states), cell), Image.LANCZOS))
    sheet = Image.new("RGB", (cell * len(states), cell * len(rows)), BG)
    for i, r in enumerate(rows):
        sheet.paste(r, (0, i * cell))
    p = OUT / "06_avatar_states_sheet.png"
    sheet.save(p)
    made.append(p)
    for m in made:
        print(m.relative_to(ROOT))


if __name__ == "__main__":
    main()
