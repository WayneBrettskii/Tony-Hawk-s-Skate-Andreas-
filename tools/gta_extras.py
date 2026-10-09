"""my-passthrough: things GTA San Andreas shows but has no collision for (phase 4-3).

GTA draws some walls, fences and railings that its collision files leave out,
so in our reTHAWed map there is nothing to ride or grind there. This file
lists them by hand and builds collision (and grind rails) for them.

First entry (2026-10-09): the East LS skatepark's surrounding low wall and the
fence on top of it. GTA's collision there is only flat ground (checked along
x 1862, y -1451 and y -1351.5). Positions are in GTA world metres, read off
the Los Santos East map for reTHAWed (whose author built this wall and fence
from GTA's visual model), rounded to about 10 cm. Only these numbers are kept
here, nothing from that map's files.

Each wall run is a list of points along the wall's centre line, in order so
that the skatepark (the inside) is on the LEFT: around the park anticlockwise
seen from above. Each point: (x, y, ground z, wall top z, fence height above
the wall top, 0 = no fence). Between two points the wall is a straight box;
heights change linearly. The fence stands in the MIDDLE of the wall (in GTA
it stands near the outer edge), so the wall top is a ledge on both sides.
Grind rails: along both top edges of the wall and along the top of the fence.
(First version, 2026-10-09 00:04: fence on the outer edge and only an inner
rail; the user could not grind from outside the park, nor the fence top.)

Used by gta_to_thaw_map.py; also usable later for the whole map.
"""

import math

WALL_WIDTH = 0.7       # m (LSE: 0.7 to 0.9)
FENCE_THICKNESS = 0.15  # m
BURY = 0.3             # m: walls start this far below the given ground (no gaps on slopes)
RAIL_INSET = 0.02      # m: rail this far in from the inner top edge

WALLS = [
    # name, points (x, y, ground, top, fence above top)
    ("skatepark wall west (north part)", [
        (1862.1, -1351.0, 12.5, 13.1, 3.7),
        (1862.1, -1371.1, 12.5, 13.1, 3.7),
    ]),
    ("skatepark wall west (south part)", [
        (1862.1, -1390.9, 12.5, 13.15, 1.7),
        (1862.1, -1451.1, 12.5, 13.15, 1.7),
    ]),
    ("skatepark wall south (west part)", [
        (1862.1, -1450.65, 12.5, 13.2, 1.7),
        (1876.8, -1450.65, 12.5, 13.2, 3.1),
        (1956.7, -1450.65, 12.5, 13.2, 3.1),
    ]),
    ("skatepark wall south (east part)", [
        (1961.6, -1450.65, 12.5, 13.1, 3.2),
        (1976.65, -1450.65, 12.5, 13.15, 3.2),
    ]),
    ("skatepark wall east", [
        (1976.65, -1450.65, 12.5, 13.2, 3.1),
        (1976.65, -1441.8, 12.5, 13.2, 3.1),
        (1976.65, -1438.9, 12.7, 13.4, 3.1),
        (1976.65, -1421.8, 15.8, 16.5, 3.1),
        (1976.65, -1401.9, 19.4, 20.1, 3.1),
        (1976.65, -1384.8, 22.5, 23.2, 3.1),
        (1976.65, -1381.8, 22.7, 23.4, 3.1),
        (1976.65, -1351.25, 22.9, 23.6, 3.1),
    ]),
    ("skatepark wall north (east part)", [
        (1976.65, -1351.25, 22.9, 23.6, 3.1),
        (1965.9, -1351.25, 22.7, 23.4, 3.1),
        (1963.0, -1351.25, 22.5, 23.2, 3.1),
        (1945.9, -1351.25, 19.4, 20.1, 3.1),
        (1926.0, -1351.25, 15.8, 16.5, 3.1),
        (1908.9, -1351.25, 12.7, 13.4, 3.1),
        (1906.0, -1351.25, 12.5, 13.2, 3.0),
        (1896.7, -1351.25, 12.5, 13.15, 3.0),
    ]),
    ("skatepark wall north (west part)", [
        (1876.8, -1351.25, 12.5, 13.1, 3.7),
        (1862.1, -1351.25, 12.5, 13.1, 3.7),
    ]),
]


def _box(corners_bottom, corners_top):
    """A closed box from 4 bottom and 4 top corners (same order around).
    Returns (vertices, faces) with faces pointing outwards in GTA's own corner
    order (the order gta_to_thaw_map.py expects from GTA meshes)."""
    verts = list(corners_bottom) + list(corners_top)
    quads = [(0, 1, 2, 3), (4, 5, 6, 7)] + [(i, (i + 1) % 4, (i + 1) % 4 + 4, i + 4) for i in range(4)]
    cx = sum(v[0] for v in verts) / 8.0
    cy = sum(v[1] for v in verts) / 8.0
    cz = sum(v[2] for v in verts) / 8.0
    faces = []
    for q in quads:
        for tri in ((q[0], q[1], q[2]), (q[0], q[2], q[3])):
            A, B, C = (verts[i] for i in tri)
            u = [B[k] - A[k] for k in range(3)]
            v = [C[k] - A[k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            fc = [(A[k] + B[k] + C[k]) / 3.0 for k in range(3)]
            out = n[0] * (fc[0] - cx) + n[1] * (fc[1] - cy) + n[2] * (fc[2] - cz)
            # GTA mesh convention in GTA coordinates (Z up): (B-A) x (C-A)
            # points INTO the solid (that's why the THAW writer reverses it).
            faces.append(tri if out < 0 else (tri[0], tri[2], tri[1]))
    return verts, faces


def wall_objects():
    """(name, vertices, faces) per wall run, GTA world metres, for gta_to_thaw_map."""
    out = []
    for name, pts in WALLS:
        verts, faces = [], []
        for p0, p1 in zip(pts, pts[1:]):
            dx, dy = p1[0] - p0[0], p1[1] - p0[1]
            length = math.hypot(dx, dy)
            if length < 1e-6:
                continue
            lx, ly = -dy / length, dx / length  # left = inside
            inner, outer = WALL_WIDTH / 2.0, -WALL_WIDTH / 2.0
            f_in, f_out = FENCE_THICKNESS / 2.0, -FENCE_THICKNESS / 2.0

            def at(p, off, z):
                return (p[0] + lx * off, p[1] + ly * off, z)

            pieces = [(
                [at(p0, inner, p0[2] - BURY), at(p1, inner, p1[2] - BURY), at(p1, outer, p1[2] - BURY), at(p0, outer, p0[2] - BURY)],
                [at(p0, inner, p0[3]), at(p1, inner, p1[3]), at(p1, outer, p1[3]), at(p0, outer, p0[3])],
            )]
            if p0[4] > 0 or p1[4] > 0:
                pieces.append((
                    [at(p0, f_in, p0[3]), at(p1, f_in, p1[3]), at(p1, f_out, p1[3]), at(p0, f_out, p0[3])],
                    [at(p0, f_in, p0[3] + p0[4]), at(p1, f_in, p1[3] + p1[4]),
                     at(p1, f_out, p1[3] + p1[4]), at(p0, f_out, p0[3] + p0[4])],
                ))
            for bottom, top in pieces:
                v, f = _box(bottom, top)
                base = len(verts)
                verts += v
                faces += [(a + base, b + base, c + base) for a, b, c in f]
        out.append(("extra_" + name.replace(" ", "_").replace("(", "").replace(")", ""), verts, faces))
    return out


def wall_rails():
    """Grind rails per wall run: both top edges of the wall, and the fence top
    (where there is a fence). Lists of GTA points."""
    rails = []
    for _name, pts in WALLS:
        sides = [(WALL_WIDTH / 2.0 - RAIL_INSET, 3, None), (-(WALL_WIDTH / 2.0 - RAIL_INSET), 3, None)]
        if any(p[4] > 0 for p in pts):
            sides.append((0.0, 3, 4))  # fence top: wall top + fence height
        for off, top, extra in sides:
            chain = []
            for i, p in enumerate(pts):
                # direction of the wall here (average of the neighbouring segments)
                a = pts[max(0, i - 1)]
                b = pts[min(len(pts) - 1, i + 1)]
                dx, dy = b[0] - a[0], b[1] - a[1]
                length = math.hypot(dx, dy) or 1.0
                lx, ly = -dy / length, dx / length
                z = p[top] + (p[extra] if extra is not None else 0.0)
                chain.append((p[0] + lx * off, p[1] + ly * off, z))
            rails.append(chain)
    return rails
