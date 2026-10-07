#!/usr/bin/env python3
"""Render Grok Bot Companion UI mockups (466x466, round-masked PNGs).

Layout numbers mirror main/ui/*.c + ui_theme.h so the mockups match the firmware.
Avatars use the same procedural shapes/colors as relay/bots.json (the app profiles'
avatarShape / avatarColor, drawn by ui_avatar.c; colors approximate until checked
against app screenshots). Run:  python3 docs/mockups/render_mockups.py
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

def _load_bots() -> dict:
    """Flatten relay/bots.json v2 the same way relay/server.py load_bots() does."""
    data = json.loads((ROOT / "relay/bots.json").read_text())
    pal = {k.lower(): v for k, v in data.get("palette", {}).items() if isinstance(v, dict)}
    out = {}
    for b in data["bots"]:
        av = b.get("avatar", b)
        cname = av.get("color", "default")
        entry = pal.get(cname.lower(), pal.get("default", {}))
        color = cname if cname.startswith("#") else entry.get("color", "#64748B")
        out[b["id"]] = {
            "id": b["id"], "name": b["name"], "shape": av.get("shape", "circle"),
            "color": color, "accent": av.get("accent") or entry.get("accent", "#FFFFFF"),
            "rim": av.get("rim") or entry.get("rim"), "scale": av.get("scale", 86),
            "rotation": av.get("rotation", 0), "wobble": av.get("wobble", 50),
            "seed": av.get("seed", sum(map(ord, b["id"])) % 256), "tbd": av.get("tbd", False),
            "default_voice_id": b.get("default_voice_id"),
        }
    return out


BOTS = _load_bots()
VOICES = json.loads((ROOT / "relay/voices.json").read_text())["voices"]
VOICE_NAME = {v["id"]: v["name"] for v in VOICES}
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
OUTLINE_N = 48
FACE = {  # eye_y, eye_gap, eye_w, eye_h, mouth_y, shine, shine_x, shine_y  (unit space)
    "circle":   (-0.08, 0.30, 0.15, 0.23, 0.34, True, -0.40, -0.46),
    "blob":     (-0.06, 0.29, 0.15, 0.23, 0.34, True, -0.38, -0.44),
    "teardrop": (0.10, 0.25, 0.14, 0.21, 0.33, True, -0.36, 0.00),
    "cloud":    (0.04, 0.28, 0.14, 0.21, 0.34, True, -0.30, -0.44),
    "hex":      (-0.06, 0.28, 0.15, 0.23, 0.34, True, -0.36, -0.42),
    "squircle": (-0.08, 0.30, 0.15, 0.23, 0.34, True, -0.42, -0.46),
}
CLOUD_PUFFS = [(-0.46, 0.28, 0.42), (0.46, 0.28, 0.42), (0.00, 0.30, 0.50),
               (-0.42, -0.06, 0.40), (0.02, -0.32, 0.52), (0.46, -0.08, 0.38)]


def outline(bot: dict, morph: float = 0.0):
    """Unit-space outline + fan centre: a port of shape_outline() in ui_avatar.c."""
    shape, w = bot["shape"], bot["wobble"] / 50
    seed, m = bot["seed"] * 0.0245, math.radians(morph)
    pts, fy = [], 0.0
    for i in range(OUTLINE_N):
        t = 2 * math.pi * i / OUTLINE_N
        c, sn = math.cos(t), math.sin(t)
        if shape == "blob":
            rr = 1 + w * (0.05 * math.sin(2 * t + seed + m) + 0.04 * math.sin(3 * t + 2 * seed - m)
                          + 0.025 * math.sin(5 * t + 3 * seed + 2 * m))
            rr /= 1 + w * 0.115
            x, y = c * rr, sn * rr
        elif shape == "teardrop":
            x, y, fy = 0.95 * sn * math.sin(t / 2), -0.95 * c, 0.30
        elif shape == "cloud":
            best = 0.3
            for k, (px, py, pr) in enumerate(CLOUD_PUFFS):
                pr *= 1 + 0.02 * w * math.sin(m + k)
                bb = px * c + py * sn
                disc = bb * bb - (px * px + py * py - pr * pr)
                if disc >= 0:
                    best = max(best, bb + math.sqrt(disc))
            x, y = c * best, sn * best
        elif shape == "hex":
            a = math.fmod(t + math.pi / 2 + 2 * math.pi, math.pi / 3)
            rr = min(math.cos(math.pi / 6) / math.cos(a - math.pi / 6), 0.95) / 0.95
            x, y = c * rr, sn * rr
        elif shape == "squircle":
            x = 0.92 * math.copysign(math.sqrt(abs(c)), c)
            y = 0.92 * math.copysign(math.sqrt(abs(sn)), sn)
        else:
            x, y = c, sn
        if bot["rotation"]:
            ra = math.radians(bot["rotation"])
            x, y = x * math.cos(ra) - y * math.sin(ra), x * math.sin(ra) + y * math.cos(ra)
        pts.append((x, y))
    return pts, (0.0, fy)


def is_light(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000 > 150


def avatar(cv: Canvas, bot: dict, cx: float, cy: float, scale: float = 1.0, opa: float = 1.0,
           anim: str = "idle", level: float = 0.0, t: float = 0.0, bg=BG, glow: bool = False,
           breath: float = 0.5, morph: float = 40.0, ripple: float = 0.2, blink: float = 0.0,
           screen_r: float = W / 2, glow_cy: float | None = None):
    """Same layers/order as draw_cb(): glow, ripples, body, rim, shine, eyes, mouth, orbit."""
    R = screen_r * bot["scale"] / 100
    if anim in ("idle", "listening", "working"):
        R *= 1 + 0.025 * breath
    if anim == "speaking":
        R *= 1 + 0.06 * level
    R *= scale
    base = hexrgb(bot["color"])
    body = blend(blend((0xDC, 0x26, 0x26), base, 170 / 255) if anim == "error" else base, bg, opa)
    acc = hexrgb(bot["accent"])
    eye_c = (0x11, 0x13, 0x18) if is_light(base) and anim != "error" else (255, 255, 255)
    if glow:
        gy = cy if glow_cy is None else glow_cy
        cv.circle(cx, gy, screen_r - 4.5, outline=blend(acc, bg, 0.36 * opa), width=7)
        cv.circle(cx, gy, screen_r - 11, outline=blend(acc, bg, 0.12 * opa), width=6)
    if anim == "listening":
        for k in range(3):
            ph = (ripple + k / 3) % 1
            rr = min(R * (1 + 0.16 * ph), screen_r - 3) - 2
            cv.circle(cx, cy, rr, outline=blend(acc, bg, 0.78 * (1 - ph) * opa), width=4)
    pts, (fx, fy) = outline(bot, morph)
    poly_px = [(s(cx + x * R), s(cy + y * R)) for x, y in pts]
    cv.d.polygon(poly_px, fill=body)
    if bot.get("rim"):
        rim = blend(hexrgb(bot["rim"]), bg, opa)
        cv.d.line(poly_px + [poly_px[0]], fill=rim, width=max(s(2), s(R * 0.015)), joint="curve")
    ey_, gap, ew, eh, my, shine, shx, shy = FACE.get(bot["shape"], FACE["circle"])
    if shine:
        sw, sh = 0.26 * R, 0.14 * R
        sx, sy = cx + shx * R, cy + shy * R
        cv.d.ellipse([s(sx - sw / 2), s(sy - sh / 2), s(sx + sw / 2), s(sy + sh / 2)],
                     fill=blend(blend(acc, base if anim != "error" else body, 0.30), bg, opa))
    e = blend(eye_c, bg, opa)
    h = max(3, eh * R * (1 - 0.85 * blink))
    for sx_ in (-1, 1):
        ex, ey = cx + sx_ * gap * R, cy + ey_ * R
        cv.d.ellipse([s(ex - ew * R / 2), s(ey - h / 2), s(ex + ew * R / 2), s(ey + h / 2)], fill=e)
    if anim == "speaking":
        mY = cy + my * R
        bw, bg_ = max(4, 0.065 * R), 0.045 * R
        for k in range(-2, 3):
            wave = 0.55 + 0.45 * math.sin(t / 20 + k * 1.3)
            bh = max(bw, 0.05 * R + level * 0.16 * R * wave)
            x0 = cx + k * (bw + bg_)
            cv.rrect(x0 - bw / 2, mY - bh / 2, bw, bh, bw / 2, fill=e)
    if anim in ("thinking", "working"):
        rad = screen_r - 14 if screen_r > 150 else screen_r + 8
        oy = cy if glow_cy is None else glow_cy
        dcol = TEXT if anim == "thinking" else acc
        for k in range(3):
            a = math.radians(t - k * 22)
            dr = (14 - k * 3) / 2
            cv.circle(cx + rad * math.cos(a), oy + rad * math.sin(a), dr,
                      fill=blend(dcol, bg, (255 - k * 60) / 255 * opa))


# --- screens ---------------------------------------------------------------------
SPACING, NEIGHBOR_SCALE = 340, 200 / 256          # ui_theme.h
AVATAR_DY, SCRIM_Y, NAME_Y, STATUS_Y, STATUS_W = -18, 220, 336, 378, 300
SCRIM_STOPS = [(0, 0), (110, 150), (255, 225)]     # (frac 0..255, opa) like ui_home.c


def wrap_lines(cv, txt, f, width, max_lines=2):
    words, lines, cur = txt.split(), [], ""
    for wd in words:
        if cur and cv.text_w(cur + " " + wd, f) > width:
            lines.append(cur)
            cur = wd
        else:
            cur = (cur + " " + wd).strip()
    lines.append(cur)
    if len(lines) > max_lines:
        lines = lines[:max_lines]
        lines[-1] = lines[-1].rstrip(".") + "..."
    return lines


def home(bot_id: str, state: str, status: str, offset: float = 0.0, t: float = 30,
         level: float = 0.6, conv: bool = False, **av_kw) -> Canvas:
    """ui_home.c: full-screen avatar + name + status over a soft gradient. Nothing else."""
    cv = Canvas()
    idx, n = ORDER.index(bot_id), len(ORDER)
    slots = []
    for k in (-1, 0, 1):
        x = k * SPACING + offset
        tt = min(abs(x), SPACING) / SPACING
        opa = 1 - tt
        if k != 0 and opa < 8 / 255:
            continue                                   # neighbours invisible at rest
        slots.append((tt, k, x, 1 - (1 - NEIGHBOR_SCALE) * tt, opa))
    for tt, k, x, sc, op in sorted(slots, reverse=True):
        b = BOTS[ORDER[(idx + k) % n]]
        anim = state if (k == 0 and offset == 0) else "static"
        avatar(cv, b, CX + x, CY + AVATAR_DY, sc, op, anim, level, t,
               glow=(conv and k == 0 and offset == 0), glow_cy=CY, **av_kw)   # rim cues are screen-centred
    # soft dark gradient: 3-stop vertical ramp, same stops as ui_home.c
    scrim = Image.new("L", (W * S, H * S), 0)
    sd = ImageDraw.Draw(scrim)
    for yy in range(s(SCRIM_Y), H * S):
        fr = (yy / S - SCRIM_Y) / (H - SCRIM_Y) * 255
        for (f0, o0), (f1, o1) in zip(SCRIM_STOPS, SCRIM_STOPS[1:]):
            if f0 <= fr <= f1:
                sd.line([(0, yy), (W * S, yy)], fill=int(o0 + (o1 - o0) * (fr - f0) / (f1 - f0)))
                break
    cv.img.paste(Image.new("RGB", cv.img.size, BG), (0, 0), scrim)
    cv.d = ImageDraw.Draw(cv.img)
    cap = 1 - min(abs(offset), SPACING / 3) / (SPACING / 3)
    if cap > 0:
        name = BOTS[bot_id]["name"]
        cv.text(CX + 2, NAME_Y + 2, name, F28, blend(BG, BG, 1))
        cv.text(CX, NAME_Y, name, F28, blend(TEXT, BG, cap))
        col = (0xFC, 0xA5, 0xA5) if state == "error" else (0xE5, 0xE7, 0xEB)
        for i, ln in enumerate(wrap_lines(cv, status, F16, STATUS_W)):
            cv.text(CX, STATUS_Y + i * 21, ln, F16, blend(col, BG, cap))
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


def voice_picker(bot_id: str, selected_id: str) -> Canvas:
    """ui_voice_picker.c: 5-row roller over the 28 app voices; Preview disabled (no clip yet)."""
    cv = Canvas()
    selected = next(i for i, v in enumerate(VOICES) if v["id"] == selected_id)
    cv.grabber(12)
    cv.text(CX, 36, f"Voice - {BOTS[bot_id]['name']}", F20, TEXT)
    x, w, y, rowh, rows = CX - 150, 300, 82, 34, 5
    cv.rrect(x, y, w, rowh * rows + 16, 18, fill=SURFACE)
    mid = y + 8 + rowh * (rows / 2)
    cv.rrect(x + 8, mid - rowh / 2, w - 16, rowh, 10, fill=ACCENT)
    for off in range(-3, 4):
        i = selected + off
        if 0 <= i < len(VOICES):
            yy = mid + off * rowh
            if y + 10 < yy < y + rowh * rows + 8:
                fade = 1 - 0.28 * abs(off)
                cv.text(CX, yy, VOICES[i]["name"], F16, INK if off == 0 else blend(MUTED, SURFACE, fade),
                        anchor="mm")
    # scroll position hint (28 items)
    track_y, track_h = y + 14, rowh * rows - 12
    cv.rrect(x + w - 9, track_y, 3, track_h, 1.5, fill=SURFACE2)
    th = track_h * rows / len(VOICES)
    cv.rrect(x + w - 9, track_y + (track_h - th) * selected / (len(VOICES) - 1), 3, th, 1.5, fill=DIM)
    desc = VOICES[selected]["description"] or " "
    for i, ln in enumerate(wrap_lines(cv, desc, F14, 300, 2)):
        cv.text(CX, 278 + i * 18, ln, F14, TEXT)
    dis = blend(SURFACE2, BG, 0.4)
    cv.rrect(CX - 150, 316, 140, 48, 24, fill=dis)
    tcol = blend(TEXT, BG, 0.4)
    cv.d.polygon([(s(CX - 122), s(332)), (s(CX - 122), s(348)), (s(CX - 109), s(340))], fill=tcol)
    cv.text(CX - 70, 340, "Preview", F16, tcol, anchor="mm")
    cv.rrect(CX + 10, 316, 140, 48, 24, fill=ACCENT)
    cv.text(CX + 80, 340, "Select", F16, INK, anchor="mm")
    cv.text(CX, 384, "Cancel", F14, MUTED)
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
    for i, (k, v) in enumerate((("Firmware", "0.3.0"), ("Device ID", "pocket-001"), ("Relay", "reachable (HTTPS)"))):
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


def nav_map(home_img, top_img, bottom_img, left_img, right_img, path: Path):
    """How the panels open from the minimal home (half-size thumbnails)."""
    th = W // 2
    pad, vpad = 36, 64
    cw, ch = th * 3 + pad * 4, th * 3 + vpad * 2 + pad * 2
    out = Image.new("RGBA", (cw, ch), (12, 13, 16, 255))
    d = ImageDraw.Draw(out)
    lf = ImageFont.truetype(FONT_CANDIDATES[0], 16) if Path(FONT_CANDIDATES[0]).exists() else None
    if lf is not None:
        try:
            lf.set_variation_by_name("Medium")
        except Exception:
            pass
    hy0 = pad + th + vpad
    pos = {"home": (pad * 2 + th, hy0), "top": (pad * 2 + th, pad), "bottom": (pad * 2 + th, hy0 + th + vpad),
           "left": (pad, hy0), "right": (pad * 3 + th * 2, hy0)}
    for key, img in (("home", home_img), ("top", top_img), ("bottom", bottom_img), ("left", left_img), ("right", right_img)):
        out.alpha_composite(img.resize((th, th), Image.LANCZOS), pos[key])
    c = (200, 204, 212)
    hx, hy = pos["home"]
    lx, rx = pos["left"][0] + th / 2, pos["right"][0] + th / 2
    labels = [((hx + th / 2, hy - vpad / 2), "swipe down from top edge: device settings"),
              ((hx + th / 2, hy + th + vpad * 0.7), "swipe up from bottom edge: bot panel"),
              ((lx, hy + th + 16), "swipe right: previous bot"),
              ((rx, hy + th + 16), "swipe left: next bot")]
    for (x, y), txt in labels:
        d.text((x, y), txt, fill=c, font=lf, anchor="mm")
    d.text((cw / 2, ch - 14), "Home shows only avatar + name + status. The bot list wraps both ways.",
           fill=(150, 154, 162), font=lf, anchor="mb")
    out.save(path)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    made = []
    tmp = []

    def frame(cv: Canvas, name: str, keep: bool = True, **kw) -> Image.Image:
        p = OUT / name
        img = cv.finish(p, **kw)
        (made if keep else tmp).append(p)
        return img

    # (1) minimal full-screen home, three real bots / states (+ one with a conversation open)
    f_mer = frame(home("meridian", "idle", "Ready", blink=0.0), "01_home_meridian_idle.png")
    f_pho = frame(home("photon", "working", "Photon is working on it", t=-30, breath=0.8, morph=120),
                  "01_home_photon_working.png")
    f_pul = frame(home("pulse", "listening", "Listening...", ripple=0.15, morph=200),
                  "01_home_pulse_listening.png")
    f_scr = frame(home("scribe", "speaking", "Here's the summary you asked for.", level=0.7, t=40, conv=True),
                  "01_home_scribe_speaking.png")
    p = OUT / "01_home_carousel.png"
    strip([f_mer, f_pho, f_pul, f_scr],
          ["Meridian - idle", "Photon - working", "Pulse - listening", "Scribe - speaking (conversation glow)"], p)
    made.append(p)
    # (2) mid-swipe transition: last bot (Nexus) slides/fades out, first (Meridian) slides in
    f_mid = frame(home("nexus", "idle", "Ready", offset=-150), "01b_carousel_midswipe.png")
    p = OUT / "01b_carousel_wrap_sequence.png"
    f_nex = frame(home("nexus", "idle", "Ready"), "_tmp_nexus.png", keep=False)
    strip([f_nex, f_mid, f_mer], ["Nexus (last bot) at rest - no neighbours", "mid-swipe left: Nexus out, Meridian in",
                                 "Meridian (first bot) - name fades in"], p)
    made.append(p)
    # (3) panels (open from the minimal home by edge swipes)
    clay_voice = VOICE_NAME.get(BOTS["clay"]["default_voice_id"], "Eve")
    f_panel = frame(bot_panel("clay", clay_voice, conv_active=True), "02_bot_panel.png")
    frame(bot_panel("clay", clay_voice, conv_active=True, scroll=150), "02b_bot_panel_scrolled.png")
    frame(voice_picker("clay", BOTS["clay"]["default_voice_id"]), "03_voice_picker.png")
    f_dev = frame(device_panel(), "04_device_settings.png")
    frame(lock_frame(), "05_sleep_lock.png", dim=0.6)
    # navigation map
    f_prev = frame(home("dr_eggbot", "idle", "Ready"), "_tmp_prev.png", keep=False)
    f_next = frame(home("clay", "idle", "Ready"), "_tmp_next.png", keep=False)
    p = OUT / "07_navigation_map.png"
    nav_map(f_pho, f_dev, f_panel, f_prev, f_next, p)
    made.append(p)
    # avatar sheet: every bot x every animation state (reference for the renderer)
    states = ["idle", "listening", "thinking", "working", "speaking", "error"]
    cell = 200
    rows = []
    for bid in ORDER:
        cv = Canvas()
        cv.img = Image.new("RGB", (cell * len(states) * S, cell * S), BG)
        cv.d = ImageDraw.Draw(cv.img)
        for j, st in enumerate(states):
            avatar(cv, BOTS[bid], cell * j + cell / 2, cell / 2 - 12, 1.0, 1.0, st, 0.7, 40,
                   screen_r=78, morph=60 * j)
            label = f"{BOTS[bid]['name']} - {st}" + (" (TBD look)" if BOTS[bid]["tbd"] and j == 0 else "")
            cv.text(cell * j + cell / 2, cell - 24, label, F14, MUTED)
        rows.append(cv.img.resize((cell * len(states), cell), Image.LANCZOS))
    sheet = Image.new("RGB", (cell * len(states), cell * len(rows)), BG)
    for i, r in enumerate(rows):
        sheet.paste(r, (0, i * cell))
    p = OUT / "06_avatar_states_sheet.png"
    sheet.save(p)
    made.append(p)
    for t_ in tmp:
        t_.unlink(missing_ok=True)
    for m in made:
        print(m.relative_to(ROOT))


if __name__ == "__main__":
    main()
