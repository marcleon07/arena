"""Builds the game's weapon, item and projectile models as OBJ files.

Run from the repo root:  python Tools/generate_models.py
Writes SourceArt/Models/*.obj and SourceArt/Models/sockets.json, which
Tools/import_art.py turns into /Game/Art/Meshes static meshes.

Models are kitbashed from bevelled boxes, cylinders, tubes, tori and spheres.
Units are centimetres in Unreal's frame (X forward, Y right, Z up). Weapons have
their origin at the right hand's grip; sockets mark the muzzle, the left hand's
grip and the pivot of any spinning part.

Material slots (the importer assigns Arena materials by these names):
  Metal   gunmetal            Dark    rubber / dark paint
  Accent  painted in the weapon or item colour (set per instance at runtime)
  Glow    emissive in the weapon or item colour
  FX      additive, unlit (muzzle flash)
Pure standard library, deterministic.
"""

import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "SourceArt", "Models")
UV_SCALE = 1.0 / 24.0  # Detail texture repeats every 24 cm.


# --- vector helpers ------------------------------------------------------------

def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    length = math.sqrt(dot(a, a))
    return (a[0] / length, a[1] / length, a[2] / length) if length > 1e-9 else (0.0, 0.0, 1.0)


def perpendicular(w):
    helper = (0.0, 0.0, 1.0) if abs(w[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = norm(cross(helper, w))
    return u, cross(w, u)


class Rot:
    """3x3 rotation matrix; columns are where the X, Y and Z axes end up."""

    def __init__(self, x=(1, 0, 0), y=(0, 1, 0), z=(0, 0, 1)):
        self.x, self.y, self.z = x, y, z

    def apply(self, v):
        return add(add(mul(self.x, v[0]), mul(self.y, v[1])), mul(self.z, v[2]))

    def then(self, other):
        """This rotation followed by other."""
        return Rot(other.apply(self.x), other.apply(self.y), other.apply(self.z))


def pitch(deg):
    """Nose up for positive angles (X toward +Z)."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return Rot((c, 0, s), (0, 1, 0), (-s, 0, c))


def roll(deg):
    """Rotation about X (Y toward +Z)."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return Rot((1, 0, 0), (0, c, s), (0, -s, c))


def yaw(deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return Rot((c, s, 0), (-s, c, 0), (0, 0, 1))


IDENTITY = Rot()
X_AXIS = (1.0, 0.0, 0.0)
Y_AXIS = (0.0, 1.0, 0.0)
Z_AXIS = (0.0, 0.0, 1.0)


# --- mesh ------------------------------------------------------------------------

class Mesh:
    def __init__(self, name):
        self.name = name
        self.tris = []  # (material, ((p, n), (p, n), (p, n)))
        self.sockets = {}

    def tri(self, mat, p0, p1, p2, n0, n1=None, n2=None):
        n1 = n1 or n0
        n2 = n2 or n0
        # Wind so the geometric normal agrees with the shading normals.
        if dot(cross(sub(p1, p0), sub(p2, p0)), add(add(n0, n1), n2)) < 0.0:
            p1, p2, n1, n2 = p2, p1, n2, n1
        self.tris.append((mat, ((p0, n0), (p1, n1), (p2, n2))))

    def quad(self, mat, p0, p1, p2, p3, n0, n1=None, n2=None, n3=None):
        n1, n2, n3 = n1 or n0, n2 or n0, n3 or n0
        self.tri(mat, p0, p1, p2, n0, n1, n2)
        self.tri(mat, p0, p2, p3, n0, n2, n3)

    def socket(self, name, location):
        self.sockets[name] = [round(c, 3) for c in location]

    # --- primitives ---

    def box(self, mat, center, half, bevel=0.0, rot=IDENTITY):
        """Box with 45-degree chamfered edges, flat shaded."""
        h = half
        b = min(bevel, h[0] * 0.95, h[1] * 0.95, h[2] * 0.95)
        c = center

        def P(v):
            return add(c, rot.apply(v))

        def N(v):
            return norm(rot.apply(v))

        axes = (0, 1, 2)
        for a in axes:
            u, v = [k for k in axes if k != a]
            for s in (-1.0, 1.0):
                corners = []
                for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    p = [0.0, 0.0, 0.0]
                    p[a] = s * h[a]
                    p[u] = su * (h[u] - b)
                    p[v] = sv * (h[v] - b)
                    corners.append(P(p))
                n = [0.0, 0.0, 0.0]
                n[a] = s
                self.quad(mat, *corners, N(n))
        if b <= 0.0:
            return
        for a, bb in ((0, 1), (0, 2), (1, 2)):
            cc = 3 - a - bb
            for sa in (-1.0, 1.0):
                for sb in (-1.0, 1.0):
                    pts = []
                    for sc, first in ((-1, True), (-1, False), (1, False), (1, True)):
                        p = [0.0, 0.0, 0.0]
                        p[cc] = sc * (h[cc] - b)
                        if first:
                            p[a] = sa * h[a]
                            p[bb] = sb * (h[bb] - b)
                        else:
                            p[a] = sa * (h[a] - b)
                            p[bb] = sb * h[bb]
                        pts.append(P(p))
                    n = [0.0, 0.0, 0.0]
                    n[a] = sa
                    n[bb] = sb
                    self.quad(mat, *pts, N(n))
        for sx in (-1.0, 1.0):
            for sy in (-1.0, 1.0):
                for sz in (-1.0, 1.0):
                    s = (sx, sy, sz)
                    pts = []
                    for a in axes:
                        p = [s[k] * (h[k] - b) for k in axes]
                        p[a] = s[a] * h[a]
                        pts.append(P(p))
                    self.tri(mat, *pts, N(s))

    def cylinder(self, mat, p0, p1, r0, r1=None, segments=20, caps=True, flat=False, phase=0.0):
        """Frustum from p0 (radius r0) to p1 (radius r1)."""
        r1 = r0 if r1 is None else r1
        w = norm(sub(p1, p0))
        u, v = perpendicular(w)
        length = math.sqrt(dot(sub(p1, p0), sub(p1, p0)))
        slope = (r0 - r1) / length if length > 0 else 0.0

        def ring(i):
            a = phase + 2.0 * math.pi * i / segments
            return add(mul(u, math.cos(a)), mul(v, math.sin(a)))

        for i in range(segments):
            d0, d1 = ring(i), ring(i + 1)
            q = (add(p0, mul(d0, r0)), add(p0, mul(d1, r0)), add(p1, mul(d1, r1)), add(p1, mul(d0, r1)))
            if flat:
                n = norm(add(norm(add(d0, d1)), mul(w, slope)))
                self.quad(mat, *q, n)
            else:
                n0 = norm(add(d0, mul(w, slope)))
                n1 = norm(add(d1, mul(w, slope)))
                self.quad(mat, q[0], q[1], q[2], q[3], n0, n1, n1, n0)
            if caps:
                if r0 > 0:
                    self.tri(mat, p0, q[1], q[0], mul(w, -1.0))
                if r1 > 0:
                    self.tri(mat, p1, q[3], q[2], w)

    def tube(self, mat, p0, p1, outer0, inner0, outer1=None, inner1=None, segments=20, inner_mat="Dark"):
        """Hollow cylinder (barrels): outer and inner walls plus ring ends."""
        outer1 = outer0 if outer1 is None else outer1
        inner1 = inner0 if inner1 is None else inner1
        self.cylinder(mat, p0, p1, outer0, outer1, segments, caps=False)
        w = norm(sub(p1, p0))
        u, v = perpendicular(w)
        for i in range(segments):
            a0 = 2.0 * math.pi * i / segments
            a1 = 2.0 * math.pi * (i + 1) / segments
            d0 = add(mul(u, math.cos(a0)), mul(v, math.sin(a0)))
            d1 = add(mul(u, math.cos(a1)), mul(v, math.sin(a1)))
            # Inner wall faces the axis.
            self.quad(inner_mat, add(p0, mul(d0, inner0)), add(p0, mul(d1, inner0)),
                      add(p1, mul(d1, inner1)), add(p1, mul(d0, inner1)),
                      mul(d0, -1.0), mul(d1, -1.0), mul(d1, -1.0), mul(d0, -1.0))
            for p, ro, ri, n in ((p0, outer0, inner0, mul(w, -1.0)), (p1, outer1, inner1, w)):
                self.quad(mat, add(p, mul(d0, ri)), add(p, mul(d1, ri)), add(p, mul(d1, ro)), add(p, mul(d0, ro)), n)

    def torus(self, mat, center, axis, major, minor, segments=24, sides=10, phase=0.0):
        w = norm(axis)
        u, v = perpendicular(w)
        for i in range(segments):
            for j in range(sides):
                pts, nrm = [], []
                for di, dj in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    a = phase + 2.0 * math.pi * (i + di) / segments
                    b = 2.0 * math.pi * (j + dj) / sides
                    radial = add(mul(u, math.cos(a)), mul(v, math.sin(a)))
                    n = add(mul(radial, math.cos(b)), mul(w, math.sin(b)))
                    pts.append(add(center, add(mul(radial, major), mul(n, minor))))
                    nrm.append(n)
                self.quad(mat, *pts, *nrm)

    def sphere(self, mat, center, radius, segments=20, rings=12):
        rx, ry, rz = radius if isinstance(radius, tuple) else (radius, radius, radius)

        def point(i, j):
            theta = math.pi * j / rings
            phi = 2.0 * math.pi * i / segments
            d = (math.sin(theta) * math.cos(phi), math.sin(theta) * math.sin(phi), math.cos(theta))
            p = add(center, (d[0] * rx, d[1] * ry, d[2] * rz))
            n = norm((d[0] / rx, d[1] / ry, d[2] / rz))
            return p, n

        for i in range(segments):
            for j in range(rings):
                (a, na), (b, nb), (c, nc), (d, nd) = point(i, j), point(i + 1, j), point(i + 1, j + 1), point(i, j + 1)
                if j == 0:
                    self.tri(mat, a, c, d, na, nc, nd)
                elif j == rings - 1:
                    self.tri(mat, a, b, c, na, nb, nc)
                else:
                    self.quad(mat, a, b, c, d, na, nb, nc, nd)

    def spike(self, mat, base, tip, width, up):
        """A flat, double-sided triangle (muzzle-flash petal)."""
        side = mul(norm(cross(sub(tip, base), up)), width * 0.5)
        n = norm(cross(side, sub(tip, base)))
        a, b = add(base, side), sub(base, side)
        self.tri(mat, a, b, tip, n)
        self.tri(mat, a, b, tip, mul(n, -1.0))

    # --- output ---

    def write_obj(self, path):
        lines = ["# Generated by Tools/generate_models.py", "o " + self.name]
        vi = 1
        by_mat = {}
        for mat, corners in self.tris:
            by_mat.setdefault(mat, []).append(corners)
        for mat, tris in by_mat.items():
            lines.append("usemtl " + mat)
            for corners in tris:
                p0, p1, p2 = (c[0] for c in corners)
                fn = norm(cross(sub(p1, p0), sub(p2, p0)))
                # Box-project UVs on the face's dominant axis.
                ax = max(range(3), key=lambda k: abs(fn[k]))
                ua, va = [k for k in range(3) if k != ax]
                # Unreal is left-handed; OBJ is right-handed Z-up. Mirror Y and
                # reverse the winding so faces keep pointing outward.
                for p, n in (corners[0], corners[2], corners[1]):
                    lines.append("v %.4f %.4f %.4f" % (p[0], -p[1], p[2]))
                    lines.append("vn %.4f %.4f %.4f" % (n[0], -n[1], n[2]))
                    lines.append("vt %.4f %.4f" % (p[ua] * UV_SCALE, p[va] * UV_SCALE))
                lines.append("f %d/%d/%d %d/%d/%d %d/%d/%d" % (vi, vi, vi, vi + 1, vi + 1, vi + 1, vi + 2, vi + 2, vi + 2))
                vi += 3
        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")


# --- shared parts ------------------------------------------------------------------

def pistol_grip(m, x=0.0):
    """The right hand's grip, centred on the origin, raked back like a pistol grip."""
    m.box("Dark", (x - 1.0, 0.0, -3.5), (2.3, 1.7, 6.0), bevel=0.7, rot=pitch(-14))
    m.box("Dark", (x + 4.2, 0.0, -1.2), (0.5, 0.6, 2.2), bevel=0.2)  # trigger
    m.box("Metal", (x + 3.0, 0.0, -3.6), (3.6, 0.9, 0.4))  # trigger guard


def spinner_blades():
    m = Mesh("SM_Gauntlet_Blade")
    m.cylinder("Dark", (-1.5, 0, 0), (1.5, 0, 0), 2.2, segments=16)
    for k in range(3):
        r = roll(k * 120.0)
        m.box("Metal", r.apply((0.0, 0.0, 5.5)), (0.5, 1.4, 5.0), bevel=0.3, rot=r)
        m.box("Accent", r.apply((0.0, 0.0, 10.2)), (0.6, 1.6, 0.8), bevel=0.3, rot=r)
    return m


# --- weapons -----------------------------------------------------------------------

def gauntlet():
    m = Mesh("SM_Gauntlet")
    m.box("Metal", (8.0, 0.0, 2.0), (12.0, 5.0, 5.0), bevel=1.4)
    m.box("Accent", (8.0, 0.0, 7.3), (10.0, 3.4, 0.8), bevel=0.4)
    m.box("Accent", (8.0, 5.2, 2.0), (8.0, 0.6, 2.8), bevel=0.3)
    m.box("Accent", (8.0, -5.2, 2.0), (8.0, 0.6, 2.8), bevel=0.3)
    m.cylinder("Dark", (20.0, 0, 2.0), (27.0, 0, 2.0), 3.0, 2.4, segments=16)
    m.torus("Glow", (21.5, 0, 2.0), X_AXIS, 4.4, 0.7)
    m.torus("Glow", (24.5, 0, 2.0), X_AXIS, 3.4, 0.5)
    m.box("Dark", (-3.0, 0.0, 2.0), (2.0, 4.4, 4.4), bevel=0.8)
    m.socket("Spin", (28.5, 0.0, 2.0))
    m.socket("Muzzle", (33.0, 0.0, 2.0))
    return m


def machinegun():
    m = Mesh("SM_MachineGun")
    pistol_grip(m)
    m.box("Metal", (6.0, 0.0, 5.0), (14.0, 3.2, 4.5), bevel=1.0)
    m.box("Accent", (5.0, 3.35, 5.5), (9.0, 0.3, 2.4), bevel=0.2)
    m.box("Accent", (5.0, -3.35, 5.5), (9.0, 0.3, 2.4), bevel=0.2)
    m.box("Dark", (-14.0, 0.0, 4.0), (6.0, 2.2, 3.6), bevel=1.0)
    m.box("Dark", (9.0, 0.0, -2.5), (2.4, 1.5, 5.0), bevel=0.5, rot=pitch(10))
    m.cylinder("Dark", (20.0, 0, 6.0), (31.0, 0, 6.0), 3.9, segments=16)
    m.torus("Accent", (30.0, 0, 6.0), X_AXIS, 3.9, 0.6, sides=8)
    m.box("Glow", (4.0, 0.0, 9.7), (6.0, 0.6, 0.25))
    m.box("Dark", (13.0, 0.0, 10.4), (3.0, 0.8, 1.0), bevel=0.3)
    m.socket("Spin", (31.0, 0.0, 6.0))
    m.socket("Muzzle", (54.0, 0.0, 6.0))
    m.socket("LeftHand", (20.0, 0.0, 1.8))
    return m


def machinegun_barrels():
    m = Mesh("SM_MachineGun_Barrels")
    for k in range(6):
        a = math.radians(k * 60.0)
        y, z = math.cos(a) * 2.1, math.sin(a) * 2.1
        m.cylinder("Metal", (0.0, y, z), (22.0, y, z), 0.75, segments=8)
    m.cylinder("Dark", (0.0, 0, 0), (22.5, 0, 0), 0.8, segments=8)
    m.cylinder("Dark", (2.0, 0, 0), (3.2, 0, 0), 3.1, segments=16)
    m.cylinder("Dark", (18.0, 0, 0), (19.2, 0, 0), 3.1, segments=16)
    return m


def shotgun():
    m = Mesh("SM_Shotgun")
    pistol_grip(m)
    m.box("Metal", (-2.0, 0.0, 5.0), (10.0, 3.0, 4.0), bevel=1.0)
    m.box("Accent", (-22.0, 0.0, 3.0), (10.0, 2.2, 4.0), bevel=1.2, rot=pitch(-8))
    for y in (-2.1, 2.1):
        m.tube("Metal", (8.0, y, 6.5), (58.0, y, 6.5), 2.0, 1.4, segments=16)
    m.cylinder("Dark", (8.0, 0, 3.3), (46.0, 0, 3.3), 1.6, segments=12)
    m.box("Dark", (56.0, 0.0, 6.0), (1.5, 4.4, 2.6), bevel=0.4)
    m.box("Accent", (30.0, 0.0, 3.0), (8.0, 3.0, 2.3), bevel=1.0)
    for k in range(4):
        m.box("Dark", (24.5 + k * 3.6, 0.0, 3.0), (0.5, 3.15, 2.1))
    m.box("Glow", (0.0, 3.1, 6.0), (1.2, 0.2, 0.6))
    m.box("Glow", (0.0, -3.1, 6.0), (1.2, 0.2, 0.6))
    m.socket("Muzzle", (59.0, 0.0, 6.5))
    m.socket("LeftHand", (25.0, 0.0, 1.5))
    return m


def grenade_launcher():
    m = Mesh("SM_GrenadeLauncher")
    pistol_grip(m)
    m.box("Metal", (-2.0, 0.0, 5.0), (10.0, 3.5, 4.5), bevel=1.0)
    m.box("Dark", (-18.0, 0.0, 3.5), (7.0, 2.0, 3.5), bevel=1.0)
    m.cylinder("Accent", (8.0, 0, 5.0), (22.0, 0, 5.0), 7.5, segments=24)
    for k in range(6):
        a = math.radians(k * 60.0 + 30.0)
        y, z = math.cos(a) * 4.6, 5.0 + math.sin(a) * 4.6
        m.cylinder("Glow", (21.6, y, z), (22.6, y, z), 1.3, segments=10)
        m.box("Dark", (15.0, math.cos(a) * 7.6, 5.0 + math.sin(a) * 7.6), (5.5, 0.6, 0.6), rot=roll(k * 60.0 + 30.0))
    m.tube("Metal", (22.0, 0, 5.0), (42.0, 0, 5.0), 3.4, 2.6, segments=18)
    m.tube("Dark", (41.0, 0, 5.0), (46.0, 0, 5.0), 4.3, 2.6, segments=18)
    m.box("Dark", (26.0, 0.0, -0.5), (1.9, 1.6, 4.0), bevel=0.6, rot=pitch(6))
    m.socket("Muzzle", (46.5, 0.0, 5.0))
    m.socket("LeftHand", (26.0, 0.0, -1.0))
    return m


def rocket_launcher():
    m = Mesh("SM_RocketLauncher")
    pistol_grip(m)
    z = 9.0
    m.tube("Accent", (-30.0, 0, z), (40.0, 0, z), 6.0, 5.0, segments=24)
    m.tube("Metal", (40.0, 0, z), (49.0, 0, z), 6.2, 5.0, 7.6, 5.6, segments=24)
    m.tube("Dark", (-37.0, 0, z), (-30.0, 0, z), 7.2, 5.2, 6.3, 5.0, segments=24)
    for x in (-12.0, 18.0):
        m.cylinder("Dark", (x, 0, z), (x + 3.0, 0, z), 6.5, segments=24)
    m.box("Metal", (0.0, 0.0, 2.8), (8.0, 3.0, 3.2), bevel=0.8)
    m.box("Dark", (19.0, 0.0, -0.5), (1.9, 1.6, 4.5), bevel=0.6, rot=pitch(6))
    m.box("Dark", (8.0, -6.8, z + 4.0), (4.0, 1.0, 2.0), bevel=0.4)
    m.box("Glow", (12.1, -6.8, z + 4.0), (0.2, 0.8, 1.3))
    m.torus("Glow", (37.5, 0, z), X_AXIS, 6.1, 0.35, segments=32, sides=6)
    m.socket("Muzzle", (50.0, 0.0, z))
    m.socket("LeftHand", (19.0, 0.0, -1.5))
    return m


def lightning_gun():
    m = Mesh("SM_LightningGun")
    pistol_grip(m)
    m.box("Metal", (0.0, 0.0, 5.0), (12.0, 3.5, 4.5), bevel=1.2)
    m.box("Accent", (0.0, 0.0, 9.7), (9.0, 2.2, 0.5), bevel=0.2)
    m.box("Dark", (-16.0, 0.0, 4.0), (5.0, 2.0, 3.5), bevel=0.8)
    m.cylinder("Dark", (12.0, 0, 5.0), (34.0, 0, 5.0), 2.0, segments=12)
    for k in range(3):
        r = roll(k * 120.0 + 90.0)
        m.box("Metal", add((33.0, 0.0, 5.0), r.apply((0.0, 0.0, 3.2))), (4.5, 0.6, 0.6), bevel=0.2, rot=r)
    m.sphere("Glow", (37.0, 0.0, 5.0), 2.4)
    m.box("Glow", (0.0, 3.6, 5.0), (7.0, 0.2, 0.5))
    m.box("Glow", (0.0, -3.6, 5.0), (7.0, 0.2, 0.5))
    m.box("Dark", (20.0, 0.0, -0.5), (1.8, 1.5, 4.0), bevel=0.5, rot=pitch(6))
    m.socket("Spin", (14.0, 0.0, 5.0))
    m.socket("Muzzle", (39.0, 0.0, 5.0))
    m.socket("LeftHand", (20.0, 0.0, -1.0))
    return m


def lightning_coil():
    m = Mesh("SM_LightningGun_Coil")
    for x in (0.0, 5.0, 10.0, 15.0):
        m.torus("Glow", (x, 0, 0), X_AXIS, 3.8, 0.55, segments=20, sides=8)
    for k in range(3):
        r = roll(k * 120.0)
        m.box("Metal", r.apply((7.5, 0.0, 3.8)), (8.2, 0.5, 0.5), rot=r)
    return m


def railgun():
    m = Mesh("SM_Railgun")
    pistol_grip(m)
    m.box("Metal", (0.0, 0.0, 5.0), (16.0, 2.8, 5.0), bevel=1.2)
    m.box("Accent", (-24.0, 0.0, 4.0), (8.0, 2.0, 4.5), bevel=1.0, rot=pitch(-5))
    m.box("Metal", (41.0, 0.0, 9.3), (29.0, 1.6, 1.2), bevel=0.4)
    m.box("Metal", (41.0, 0.0, 1.7), (29.0, 1.6, 1.2), bevel=0.4)
    m.box("Glow", (41.0, 0.0, 5.5), (27.0, 0.6, 1.8))
    for x in (20.0, 32.0, 44.0, 56.0):
        m.torus("Accent", (x, 0, 5.5), X_AXIS, 5.3, 0.9, segments=24, sides=8)
    m.cylinder("Dark", (-2.0, 0, 12.8), (14.0, 0, 12.8), 1.8, segments=14)
    m.box("Glow", (14.1, 0.0, 12.8), (0.15, 1.2, 1.2))
    m.box("Dark", (6.0, 0.0, 10.8), (1.0, 0.8, 1.2))
    m.box("Dark", (70.5, 0.0, 5.5), (1.0, 2.2, 5.0), bevel=0.4)
    m.socket("Muzzle", (72.0, 0.0, 5.5))
    m.socket("LeftHand", (22.0, 0.0, -0.3))
    return m


def plasma_gun():
    m = Mesh("SM_PlasmaGun")
    pistol_grip(m)
    m.box("Accent", (4.0, 0.0, 5.0), (14.0, 4.0, 5.5), bevel=2.0)
    m.box("Metal", (2.0, 0.0, 10.8), (10.0, 2.5, 1.0), bevel=0.5)
    for y in (-4.8, 4.8):
        m.cylinder("Glow", (-6.0, y, 4.5), (14.0, y, 4.5), 2.2, segments=14)
        m.cylinder("Dark", (-7.0, y, 4.5), (-6.0, y, 4.5), 2.6, segments=14)
        m.cylinder("Dark", (14.0, y, 4.5), (15.0, y, 4.5), 2.6, segments=14)
    m.cylinder("Metal", (18.0, 0, 5.0), (30.0, 0, 5.0), 4.0, 2.6, segments=18)
    m.torus("Glow", (29.0, 0, 5.0), X_AXIS, 2.8, 0.5, segments=18, sides=8)
    m.tube("Dark", (30.0, 0, 5.0), (32.0, 0, 5.0), 2.9, 1.8, segments=18)
    m.box("Dark", (-14.0, 0.0, 4.0), (4.0, 2.5, 4.0), bevel=1.0)
    m.socket("Muzzle", (33.0, 0.0, 5.0))
    m.socket("LeftHand", (19.0, 0.0, 0.0))
    return m


# --- items -------------------------------------------------------------------------

def health():
    m = Mesh("SM_Health")
    m.box("Accent", (0, 0, 0), (13.0, 9.0, 8.0), bevel=1.6)
    m.box("Glow", (0, 0, 8.3), (6.0, 1.8, 0.4))
    m.box("Glow", (0, 0, 8.3), (1.8, 6.0, 0.4))
    for s in (-1.0, 1.0):
        m.box("Glow", (0, s * 9.3, 0), (5.0, 0.4, 1.6))
        m.box("Glow", (0, s * 9.3, 0), (1.6, 0.4, 5.0))
        m.box("Dark", (s * 10.0, 0, 0), (1.2, 9.3, 8.3), bevel=0.4)
    m.box("Dark", (0, 0, 10.0), (6.0, 1.2, 1.0), bevel=0.4)
    return m


def mega_health():
    m = Mesh("SM_MegaHealth")
    m.sphere("Glow", (0, 0, 0), 11.0, segments=24, rings=14)
    m.torus("Accent", (0, 0, 0), Z_AXIS, 16.0, 1.3, segments=32, sides=10)
    m.torus("Accent", (0, 0, 0), X_AXIS, 16.0, 1.3, segments=32, sides=10)
    m.torus("Metal", (0, 0, 0), Y_AXIS, 14.0, 0.6, segments=32, sides=8)
    return m


def armor():
    m = Mesh("SM_Armor")
    m.box("Accent", (0.0, 0.0, 4.0), (5.0, 15.0, 12.0), bevel=3.0)
    for k in range(2):
        m.box("Accent", (0.5, 0.0, -11.5 - k * 5.0), (4.2, 12.0 - k * 1.8, 2.2), bevel=1.0)
    for s in (-1.0, 1.0):
        m.box("Accent", (-1.0, s * 17.0, 14.0), (7.0, 6.0, 3.0), bevel=2.0, rot=roll(-s * 20.0))
        m.box("Dark", (-1.0, s * 14.0, -4.0), (5.2, 1.2, 10.0), bevel=0.5)
    m.box("Dark", (0.0, 0.0, 17.0), (4.0, 8.0, 1.5), bevel=0.6)
    m.box("Glow", (5.1, 0.0, 6.0), (0.3, 4.0, 4.0))
    m.box("Metal", (-5.5, 0.0, 2.0), (1.0, 13.0, 12.0), bevel=0.5)
    return m


def ammo():
    m = Mesh("SM_Ammo")
    m.box("Dark", (0, 0, 0), (12.0, 8.0, 7.0), bevel=1.0)
    m.box("Accent", (0, 0, 0), (12.3, 8.3, 2.0), bevel=0.4)
    for k in range(5):
        x = -8.0 + k * 4.0
        m.cylinder("Metal", (x, 0, 7.0), (x, 0, 11.0), 1.3, segments=10)
        m.cylinder("Accent", (x, 0, 11.0), (x, 0, 13.0), 1.3, 0.4, segments=10)
    m.box("Glow", (0, 8.35, 4.5), (6.0, 0.2, 0.8))
    m.box("Glow", (0, -8.35, 4.5), (6.0, 0.2, 0.8))
    return m


def item_base():
    m = Mesh("SM_ItemBase")
    m.cylinder("Dark", (0, 0, 0), (0, 0, 5.0), 42.0, 40.0, segments=6, flat=True, phase=math.radians(30))
    m.cylinder("Metal", (0, 0, 5.0), (0, 0, 6.0), 35.0, segments=6, flat=True, phase=math.radians(30))
    m.torus("Glow", (0, 0, 5.4), Z_AXIS, 38.0, 1.0, segments=6, sides=6, phase=math.radians(30))
    return m


def jump_pad():
    m = Mesh("SM_JumpPad")
    m.cylinder("Dark", (0, 0, -8.0), (0, 0, 4.0), 88.0, 84.0, segments=8, flat=True, phase=math.radians(22.5))
    m.cylinder("Metal", (0, 0, 4.0), (0, 0, 6.0), 78.0, segments=8, flat=True, phase=math.radians(22.5))
    m.cylinder("Glow", (0, 0, 4.0), (0, 0, 6.8), 60.0, segments=32)
    m.torus("Accent", (0, 0, 6.5), Z_AXIS, 70.0, 2.5, segments=48, sides=8)
    for k in range(8):
        r = yaw(k * 45.0 + 22.5)
        m.box("Accent", r.apply((80.0, 0.0, 5.0)), (5.0, 7.0, 2.5), bevel=1.0, rot=r)
    return m


def rocket():
    m = Mesh("SM_Rocket")
    m.cylinder("Accent", (-12.0, 0, 0), (8.0, 0, 0), 2.4, segments=16)
    m.cylinder("Metal", (8.0, 0, 0), (16.0, 0, 0), 2.4, 0.3, segments=16)
    for k in range(4):
        r = roll(k * 90.0 + 45.0)
        m.box("Dark", r.apply((-9.0, 0.0, 3.6)), (3.0, 0.25, 1.6), rot=r)
    m.tube("Dark", (-15.0, 0, 0), (-12.0, 0, 0), 2.2, 1.4, segments=16)
    m.cylinder("Glow", (-13.5, 0, 0), (-12.5, 0, 0), 1.4, segments=12)
    return m


def grenade():
    m = Mesh("SM_Grenade")
    m.sphere("Accent", (0, 0, 0), 4.5, segments=16, rings=10)
    m.torus("Dark", (0, 0, 0), Z_AXIS, 4.5, 0.6, segments=20, sides=6)
    m.torus("Dark", (0, 0, 0), X_AXIS, 4.5, 0.4, segments=20, sides=6)
    m.cylinder("Dark", (0, 0, 3.8), (0, 0, 6.0), 1.6, segments=10)
    m.sphere("Glow", (0, 0, 6.3), 0.8, segments=8, rings=6)
    return m


def muzzle_flash():
    """Unit-size star of petals pointing down +X; scaled per weapon at runtime."""
    m = Mesh("SM_MuzzleFlash")
    for k in range(6):
        a = math.radians(k * 60.0)
        d = (0.0, math.cos(a), math.sin(a))
        m.spike("FX", (0.0, 0.0, 0.0), add((10.0, 0.0, 0.0), mul(d, 3.5)), 3.0, d)
    for k in range(8):
        a = math.radians(k * 45.0)
        d = (0.0, math.cos(a), math.sin(a))
        m.spike("FX", (0.0, 0.0, 0.0), mul(d, 5.0 if k % 2 == 0 else 3.0), 2.2, X_AXIS)
    return m


MODELS = [
    gauntlet, spinner_blades, machinegun, machinegun_barrels, shotgun, grenade_launcher,
    rocket_launcher, lightning_gun, lightning_coil, railgun, plasma_gun,
    health, mega_health, armor, ammo, item_base, jump_pad, rocket, grenade, muzzle_flash,
]

if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    sockets = {}
    for make in MODELS:
        mesh = make()
        mesh.write_obj(os.path.join(OUT_DIR, mesh.name + ".obj"))
        if mesh.sockets:
            sockets[mesh.name] = mesh.sockets
        print("wrote %s.obj (%d triangles)" % (mesh.name, len(mesh.tris)))
    with open(os.path.join(OUT_DIR, "sockets.json"), "w") as f:
        json.dump(sockets, f, indent=2, sort_keys=True)
