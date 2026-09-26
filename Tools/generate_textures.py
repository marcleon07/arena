"""Generates the game's tileable surface textures as PNGs.

Run from the repo root:  python Tools/generate_textures.py
Writes SourceArt/Textures/*.png, which Tools/import_art.py turns into /Game/Art/Textures.

Each surface is built from a height field, from which the normal map is derived:
  T_<Name>_D.png  greyscale albedo (tinted per surface in the material), roughness in alpha
  T_<Name>_N.png  tangent-space normal map: R = +U (right), G = +V (down), B = out
Pure standard library, deterministic (fixed seeds). Takes a minute or two.
"""

import math
import os
import random
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "SourceArt", "Textures")


# --- output ------------------------------------------------------------------

def write_png(path, size, channels, data):
    """data: bytes-like of size*size*channels, rows top to bottom."""
    stride = size * channels
    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter: none
        raw += data[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    color_type = {1: 0, 3: 2, 4: 6}[channels]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, color_type, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def to_byte(v):
    v = int(v * 255.0 + 0.5)
    return 0 if v < 0 else (255 if v > 255 else v)


def save_surface(name, size, height, albedo, rough, normal_strength):
    os.makedirs(OUT_DIR, exist_ok=True)
    d = bytearray(size * size * 4)
    for i in range(size * size):
        a = to_byte(albedo[i])
        d[i * 4] = a
        d[i * 4 + 1] = a
        d[i * 4 + 2] = a
        d[i * 4 + 3] = to_byte(rough[i])
    write_png(os.path.join(OUT_DIR, "T_%s_D.png" % name), size, 4, d)

    n = bytearray(size * size * 3)
    s = normal_strength * 0.5
    for y in range(size):
        up = ((y - 1) % size) * size
        down = ((y + 1) % size) * size
        row = y * size
        for x in range(size):
            left = (x - 1) % size
            right = (x + 1) % size
            nx = -(height[row + right] - height[row + left]) * s
            ny = -(height[down + x] - height[up + x]) * s
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + 1.0)
            i = (row + x) * 3
            n[i] = to_byte(nx * inv * 0.5 + 0.5)
            n[i + 1] = to_byte(ny * inv * 0.5 + 0.5)
            n[i + 2] = to_byte(inv * 0.5 + 0.5)
    write_png(os.path.join(OUT_DIR, "T_%s_N.png" % name), size, 3, n)
    print("wrote T_%s_D.png, T_%s_N.png" % (name, name))


# --- noise -------------------------------------------------------------------

def value_noise(size, cells_x, seed, cells_y=None):
    """Tileable smooth value noise in [0, 1] with a lattice of cells_x by cells_y."""
    cells_y = cells_y or cells_x
    rnd = random.Random(seed)
    lattice = [rnd.random() for _ in range(cells_x * cells_y)]
    x0, x1, sx = [], [], []
    for x in range(size):
        f = x * cells_x / size
        i = int(f)
        t = f - i
        x0.append(i % cells_x)
        x1.append((i + 1) % cells_x)
        sx.append(t * t * (3.0 - 2.0 * t))
    out = [0.0] * (size * size)
    for y in range(size):
        f = y * cells_y / size
        j = int(f)
        t = f - j
        ty = t * t * (3.0 - 2.0 * t)
        r0 = lattice[(j % cells_y) * cells_x:(j % cells_y + 1) * cells_x]
        r1 = lattice[((j + 1) % cells_y) * cells_x:((j + 1) % cells_y + 1) * cells_x]
        base = y * size
        for x in range(size):
            a = r0[x0[x]]
            b = r0[x1[x]]
            s = sx[x]
            top = a + (b - a) * s
            c = r1[x0[x]]
            bot = c + (r1[x1[x]] - c) * s
            out[base + x] = top + (bot - top) * ty
    return out


def fbm(size, cells, octaves, seed, cells_y=None):
    """Sum of octaves of value noise, normalised to [0, 1]."""
    total = [0.0] * (size * size)
    amp = 1.0
    norm = 0.0
    cy = cells_y or cells
    for o in range(octaves):
        layer = value_noise(size, cells, seed * 131 + o, cy)
        for i in range(size * size):
            total[i] += layer[i] * amp
        norm += amp
        amp *= 0.5
        cells *= 2
        cy *= 2
        if cells > size or cy > size:
            break
    inv = 1.0 / norm
    return [v * inv for v in total]


def smooth(t):
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def add_scratches(size, albedo, rough, height, count, seed, length=(20, 120), bright=0.12):
    """Thin random scratches: brighter, smoother and slightly engraved."""
    rnd = random.Random(seed)
    for _ in range(count):
        x = rnd.uniform(0, size)
        y = rnd.uniform(0, size)
        angle = rnd.uniform(0, math.pi)
        steps = int(rnd.uniform(*length))
        dx = math.cos(angle)
        dy = math.sin(angle)
        strength = rnd.uniform(0.4, 1.0)
        for s in range(steps):
            i = (int(y + dy * s) % size) * size + int(x + dx * s) % size
            albedo[i] = min(1.0, albedo[i] + bright * strength)
            rough[i] = max(0.05, rough[i] - 0.2 * strength)
            height[i] -= 0.02 * strength


# --- surfaces ----------------------------------------------------------------

def tiles(size=1024):
    """Floor: 4x4 bevelled tiles with grout and per-tile variation."""
    tile = size // 4
    grout, bevel = 4, 12
    fine = fbm(size, 64, 3, 11)
    blot = fbm(size, 4, 4, 12)
    rnd = random.Random(13)
    tint = [0.82 + rnd.random() * 0.22 for _ in range(16)]
    H, A, R = [0.0] * size * size, [0.0] * size * size, [0.0] * size * size
    for y in range(size):
        ty = y % tile
        dy = min(ty, tile - 1 - ty)
        ry = (y // tile) * 4
        for x in range(size):
            tx = x % tile
            d = min(tx, tile - 1 - tx, dy)
            i = y * size + x
            f = fine[i]
            if d < grout:
                H[i] = 0.05 * f
                A[i] = 0.16 + 0.08 * f
                R[i] = 0.92
            else:
                e = smooth((d - grout) / bevel)
                H[i] = 0.25 + 0.75 * e + 0.04 * f
                t = tint[ry + x // tile]
                A[i] = 0.5 * t * (0.9 + 0.2 * f) * (0.82 + 0.3 * blot[i]) * (0.8 + 0.2 * e)
                R[i] = 0.5 + 0.25 * f + 0.15 * (1.0 - e)
    add_scratches(size, A, R, H, 260, 14)
    save_surface("Tiles", size, H, A, R, 5.0)


def panels(size=1024):
    """Walls: 2x2 metal panels with a raised frame, corner bolts and vents."""
    panel = size // 2
    seam, frame, bevel = 5, 44, 6
    fine = fbm(size, 32, 4, 21)
    blot = fbm(size, 4, 4, 22)
    H, A, R = [0.0] * size * size, [0.0] * size * size, [0.0] * size * size
    bolts = []
    for py in range(2):
        for px in range(2):
            for cx in (px * panel + 24, px * panel + panel - 24):
                for cy in (py * panel + 24, py * panel + panel - 24):
                    bolts.append((cx, cy))
    for y in range(size):
        ly = y % panel
        dy = min(ly, panel - 1 - ly)
        vent_panel_row = (y // panel)
        for x in range(size):
            lx = x % panel
            d = min(lx, panel - 1 - lx, dy)
            i = y * size + x
            f = fine[i]
            if d < seam:
                h, a, r = 0.0, 0.12, 0.9
            elif d < frame:
                e = smooth((d - seam) / bevel)
                inner = smooth((frame - d) / bevel)
                h = 0.3 + 0.7 * min(e, 1.0) * (0.8 + 0.2 * inner)
                a = 0.52 * (0.9 + 0.2 * f)
                r = 0.42 + 0.2 * f
            else:
                e = smooth((d - frame) / (bevel * 2))
                h = 0.8 - 0.25 * e + 0.03 * f
                a = 0.44 * (0.85 + 0.25 * f) * (0.8 + 0.35 * blot[i])
                r = 0.55 + 0.25 * f
                # Vent slots on the diagonal panels.
                if (x // panel) == vent_panel_row and ly > panel * 0.55 and ly < panel * 0.88:
                    sy = (ly - int(panel * 0.55)) % 28
                    if sy < 14 and panel * 0.22 < lx < panel * 0.78:
                        edge = min(sy, 13 - sy, lx - panel * 0.22, panel * 0.78 - lx)
                        k = smooth(edge / 4.0)
                        h -= 0.5 * k
                        a *= 1.0 - 0.7 * k
                        r = 0.9
            H[i], A[i], R[i] = h, a, r
    for (cx, cy) in bolts:
        for y in range(cy - 9, cy + 10):
            for x in range(cx - 9, cx + 10):
                dd = math.hypot(x - cx, y - cy) / 8.0
                if dd < 1.0:
                    i = (y % size) * size + (x % size)
                    H[i] = 1.0 + 0.35 * math.sqrt(1.0 - dd * dd)
                    A[i] = 0.34
                    R[i] = 0.35
    add_scratches(size, A, R, H, 320, 23)
    save_surface("Panels", size, H, A, R, 5.0)


def plate(size=1024):
    """Tread plate: raised lozenges in alternating directions, seams at the edges."""
    cell = 64
    blot = fbm(size, 4, 4, 31)
    fine = fbm(size, 64, 2, 32)
    c45 = math.cos(math.pi / 4)
    H, A, R = [0.0] * size * size, [0.0] * size * size, [0.0] * size * size
    for y in range(size):
        cy = y // cell
        v0 = (y % cell) - cell / 2 + 0.5
        edge_y = min(y, size - 1 - y)
        for x in range(size):
            cx = x // cell
            u0 = (x % cell) - cell / 2 + 0.5
            sign = 1.0 if (cx + cy) % 2 == 0 else -1.0
            u = (u0 + sign * v0) * c45
            v = (-sign * u0 + v0) * c45
            r2 = (u / 22.0) ** 2 + (v / 6.0) ** 2
            i = y * size + x
            b = blot[i]
            if r2 < 1.0:
                k = math.sqrt(1.0 - r2)
                H[i] = 0.3 + 0.6 * k
                A[i] = 0.62 * (0.9 + 0.15 * fine[i])
                R[i] = 0.28 + 0.15 * (1.0 - k)
            else:
                H[i] = 0.3 + 0.03 * fine[i]
                A[i] = 0.46 * (0.75 + 0.35 * b)
                R[i] = 0.45 + 0.25 * b
            edge = min(x, size - 1 - x, edge_y)
            if edge < 4:
                H[i] = 0.0
                A[i] = 0.12
                R[i] = 0.9
    add_scratches(size, A, R, H, 200, 33)
    save_surface("Plate", size, H, A, R, 4.0)


def concrete(size=1024):
    """Poured concrete with board lines, form-tie holes and pits."""
    fine = fbm(size, 32, 5, 41)
    blot = fbm(size, 3, 4, 42)
    pits = value_noise(size, 256, 43)
    H, A, R = [0.0] * size * size, [0.0] * size * size, [0.0] * size * size
    holes = [(x, y) for x in (size // 4, 3 * size // 4) for y in (size // 4, 3 * size // 4)]
    for y in range(size):
        board = 0.9 if (y % (size // 4)) < 2 else 1.0
        for x in range(size):
            i = y * size + x
            f = fine[i]
            h = 0.6 + 0.12 * f
            a = 0.55 * (0.72 + 0.4 * blot[i]) * (0.92 + 0.12 * f) * board
            r = 0.82 + 0.12 * f
            if pits[i] > 0.86:
                k = (pits[i] - 0.86) / 0.14
                h -= 0.25 * k
                a *= 1.0 - 0.4 * k
            if board < 1.0:
                h -= 0.08
            H[i], A[i], R[i] = h, a, r
    for (cx, cy) in holes:
        for y in range(cy - 16, cy + 17):
            for x in range(cx - 16, cx + 17):
                dd = math.hypot(x - cx, y - cy)
                if dd < 15.0:
                    i = y * size + x
                    k = smooth((15.0 - dd) / 5.0)
                    H[i] -= 0.4 * k
                    A[i] *= 1.0 - 0.55 * k
    save_surface("Concrete", size, H, A, R, 3.0)


def bricks(size=1024):
    """Stone blocks in running courses, bevelled, with chipped faces."""
    rows = 8
    row_h = size // rows
    mortar, bevel = 5, 16
    fine = fbm(size, 16, 5, 51)
    grain = fbm(size, 128, 2, 52)
    rnd = random.Random(53)
    courses = []
    for _ in range(rows):
        widths = []
        remaining = size
        while remaining > 0:
            w = rnd.randint(170, 300)
            if remaining - w < 170:
                w = remaining
            widths.append(w)
            remaining -= w
        offset = rnd.randint(0, size - 1)
        edges = []
        pos = offset
        for w in widths:
            edges.append(pos % size)
            pos += w
        courses.append((sorted(edges), [0.78 + rnd.random() * 0.3 for _ in widths]))
    H, A, R = [0.0] * size * size, [0.0] * size * size, [0.0] * size * size
    for y in range(size):
        row = y // row_h
        ly = y % row_h
        dy = min(ly, row_h - 1 - ly)
        edges, tints = courses[row]
        for x in range(size):
            # Distance to the nearest vertical joint in this course (wrapping).
            dx = size
            k = 0
            for n, e in enumerate(edges):
                dd = (x - e) % size
                if dd < dx:
                    dx = dd
                    k = n
                dd2 = (e - x) % size
                if dd2 < dx:
                    dx = dd2
            d = min(dx, dy)
            i = y * size + x
            f = fine[i]
            if d < mortar:
                H[i] = 0.05 + 0.05 * grain[i]
                A[i] = 0.32 * (0.9 + 0.2 * grain[i])
                R[i] = 0.95
            else:
                e = smooth((d - mortar) / bevel * (0.7 + 0.6 * f))
                H[i] = 0.2 + 0.7 * e + 0.12 * f + 0.04 * grain[i]
                A[i] = 0.5 * tints[k % len(tints)] * (0.8 + 0.3 * f) * (0.92 + 0.12 * grain[i])
                R[i] = 0.8 + 0.12 * grain[i]
    save_surface("Bricks", size, H, A, R, 4.0)


def sand(size=1024):
    """Wind-rippled sand and grit."""
    low = fbm(size, 4, 3, 61)
    fine = fbm(size, 32, 4, 62)
    grain = value_noise(size, 512, 63)
    H, A, R = [0.0] * size * size, [0.0] * size * size, [0.0] * size * size
    for y in range(size):
        for x in range(size):
            i = y * size + x
            ripple = 0.5 + 0.5 * math.sin((y + low[i] * 90.0) * 2.0 * math.pi * 16.0 / size)
            H[i] = 0.35 * ripple + 0.5 * fine[i] + 0.08 * grain[i]
            A[i] = 0.6 * (0.85 + 0.2 * fine[i]) * (0.85 + 0.3 * grain[i])
            R[i] = 0.93
    save_surface("Sand", size, H, A, R, 2.5)


def metal(size=512):
    """Brushed, scratched metal used on weapons, items and player armour."""
    brushed = fbm(size, 4, 4, 71, cells_y=128)
    blot = fbm(size, 4, 3, 72)
    H, A, R = [0.0] * size * size, [0.0] * size * size, [0.0] * size * size
    for i in range(size * size):
        H[i] = 0.5 + 0.08 * brushed[i]
        A[i] = 0.82 + 0.12 * brushed[i] + 0.06 * blot[i]
        R[i] = 0.45 + 0.2 * brushed[i] + 0.15 * blot[i]
    add_scratches(size, A, R, H, 180, 73, length=(10, 60), bright=0.08)
    save_surface("Metal", size, H, A, R, 1.5)


def grime(size=256):
    """Large-scale variation, sampled at a low frequency over every surface."""
    g = fbm(size, 4, 5, 81)
    data = bytearray(to_byte(v) for v in g)
    os.makedirs(OUT_DIR, exist_ok=True)
    write_png(os.path.join(OUT_DIR, "T_Grime.png"), size, 1, data)
    print("wrote T_Grime.png")


if __name__ == "__main__":
    for make in (grime, metal, tiles, panels, plate, concrete, bricks, sand):
        make()
