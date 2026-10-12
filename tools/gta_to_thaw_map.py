"""my-passthrough: make a reTHAWed user map from GTA San Andreas collision (phase 4-2d).

Reads GTA's collision for an area (via gta_area_dump.py, read only from the
GTA install) and writes a reTHAWed user map into
<project>/extracted/thaw_maps/<folder>/ (gitignored):
  level.ini
  <PREFIX>.pak.wpc       empty textures (.tex), invisible scene (.geom),
                         level script with the node list, collision (.col)
  <PREFIX>_sky.pak.wpc   sky: empty textures, one empty sector, a small script
  <PREFIX>_sfx.pak.wpc   empty (only the end marker)
  map_info.txt           what was written, and the formula back to GTA
Nothing is written into the GTA or reTHAWed folders. Copying the map folder
into reTHAWed's data\\mod\\UserMods\\Maps\\ is a separate, manual step.

First version (4-2d step 1): collision only (the scene has empty sectors, so
nothing is drawn; reTHAWed needs a scene to hang the collision on), no rails; one start point (Restart node). Every GTA placement becomes one THAW
collision object plus a LevelGeometry node with the same name (as in the Los
Santos East map, where each collision object has one).

Usage (from the project folder):
  python tools\\gta_to_thaw_map.py "D:\\Games\\GTA San Andreas"
  python tools\\gta_to_thaw_map.py --from-obj extracted\\gta_lse\\collision_gta.obj   (no GTA needed)

Coordinates: THAW units are taken as inches (39.3701 per metre). With area
centre (cx, cy): THAW x = (GTA x - cx) * k, THAW y (up) = GTA z * k,
THAW z = -(GTA y - cy) * k. Back to GTA: x = cx + X/k, y = cy - Z/k, z = Y/k,
which is the LevelAlignment form in src/common/convert.h (scale 1/k, ySign -1,
offset (cx, cy, 0)).

Formats: docs/DESIGN.md 8.9 (THAW collision version 10 incl. BSP trees; PAK
entries; compiled level script items as seen in Los Santos East).
"""

import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gta_area_dump as gad  # noqa: E402
import gta_extras  # noqa: E402

K = 39.3701  # THAW units per metre (inches)
LEAF_FACES = 24
MAX_DEPTH = 12
DEFAULT_PREFIX = "MPGTALSE"
DEFAULT_NAME = "GTA East Los Santos (my-passthrough)"
DEFAULT_FOLDER = "my-passthrough GTA East LS"
DEFAULT_SPAWN = (1976.1, -1449.7, 12.5)  # GTA: right corner of the skatepark (4-0 landmark 1)


def qbkey(text):
    c = 0xFFFFFFFF
    for b in text.lower().encode("ascii"):
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
    return c


# ---------------------------------------------------------------- input

# ---------------------------------------------------------------- region barriers (DESIGN 8.31 D1)
#
# GTA's map files place invisible blocks across the bridges and roads out of
# Los Santos (the "region closed" barriers its story removes). In GTA they are
# gone once the regions are open, but our map kept them: invisible walls (user's
# whole-map test, 2026-10-10). They are GTA's models 4504-4527 in
# data\maps\leveldes\seabed.ide (cuntw_roadblock*ld, sfw_roadblock*ld,
# sfn_roadblockld, cn2_roadblock0*ld, CE_Makospan1ld, CE_Fredbar*ld,
# CE_Flintwat01ld, CE_Flintintld, sfse_roadblock1-5), and only they use the
# texture sets "roadblkx" and "barrierblk". Every placement of a model with one
# of those texture sets is left out.
# (First try, 18:10: "big box a car road runs through" also caught a Las
# Venturas car park deck (venetiancpark01) and missed CE_Fredbar*, sfse_*: replaced.)

BARRIER_TXDS = ("roadblkx", "barrierblk")
REMOVED_BARRIERS = []      # (model, position), for removed_barriers.txt
PLACEMENTS = []            # (model, map file, box min, box max, triangles): placements.csv, to name what a wall is


def barrier_models(gta):
    """Lower-case names of GTA's region-barrier models (by their texture set)."""
    found = set()
    ides, _ipls = gad.read_gta_dat(gta)
    for rel in ides:
        path = gad.find_case_insensitive(gta, rel)
        if not path:
            continue
        section = None
        for line in open(path, encoding="latin-1"):
            s = line.split("#")[0].strip()
            low = s.lower()
            if not s:
                continue
            if low in ("objs", "tobj", "anim", "weap", "hier", "cars", "peds", "path", "2dfx", "txdp"):
                section = low
                continue
            if low == "end":
                section = None
                continue
            if section in ("objs", "tobj"):
                p = [x.strip().lower() for x in s.split(",")]
                # (one sea bed piece, sbcw_seabed01, shares the texture set: kept)
                if len(p) >= 3 and p[2] in BARRIER_TXDS and ("roadblock" in p[1] or p[1].endswith("ld")):
                    found.add(p[1])
    return found


def objects_from_gta(gta, box):
    used, cols = gad.collect(gta, box)
    barriers = barrier_models(gta)
    print("barriers:     %d region-barrier models in GTA's IDE files (texture sets %s)" % (
        len(barriers), ", ".join(BARRIER_TXDS)))
    out = []
    for k, inst in enumerate(used):
        if inst["name"] in barriers:
            REMOVED_BARRIERS.append((inst["name"], inst["pos"]))
            continue
        model = cols.get(inst["name"])
        if not model:
            continue
        verts, tris = gad.world_triangles(inst, model)
        out.append(("%s_%d" % (inst["name"], k), verts, [(a, b, c) for a, b, c, _s in tris]))
        if verts:
            PLACEMENTS.append((inst["name"], inst.get("source", ""), [min(v[i] for v in verts) for i in range(3)],
                               [max(v[i] for v in verts) for i in range(3)], len(tris)))
    return out


def objects_from_obj(path):
    out, name, verts, faces, base = [], None, [], [], 1
    allv = []

    def flush():
        if name is not None and faces:
            used = sorted(set(i for f in faces for i in f))
            remap = {g: n for n, g in enumerate(used)}
            out.append((name, [allv[g] for g in used], [tuple(remap[i] for i in f) for f in faces]))

    for line in open(path):
        if line.startswith("o "):
            flush()
            name, faces = line.split(None, 1)[1].strip(), []
        elif line.startswith("v "):
            allv.append(tuple(float(x) for x in line.split()[1:4]))
        elif line.startswith("f "):
            faces.append(tuple(int(x.split("/")[0]) - 1 for x in line.split()[1:4]))
    flush()
    return out


# ---------------------------------------------------------------- collision

class ColObject:
    def __init__(self, name, verts, faces):
        self.name = name
        self.key = qbkey(name)
        self.verts = verts
        self.faces = faces
        xs, ys, zs = zip(*verts)
        self.mn = (min(xs), min(ys), min(zs))
        self.mx = (max(xs), max(ys), max(zs))
        self.face_flags = [0] * len(faces)


# ---------------------------------------------------------------- vert (bowls, quarter pipes)
#
# THAW face flag 0x0008 = "vert": off such a face the skater goes straight up
# and lands back on it (bowls, quarter pipes, spine transfers). Without it he
# flies off forwards, out of the bowl (user's skate test, 2026-10-08 14:20).
# Checked in LSE: 1,544 faces have 0x0008 (alone), all sloped (normal up
# 0.0 to 0.9), mostly in and around the skatepark. 0x0004 (wallride) is on
# the vertical walls.
#
# GTA's collision has no such marks, so we say where vert is: per area, a
# box in GTA metres, the GTA models whose faces count (so traffic lights,
# lamp posts and buildings in the box are left alone), a height range, and how
# steep a face must be (normal up between lo and hi; 1 = flat floor, 0 = wall).
# The East LS skatepark is its own GTA model, laeskateparkla. Its bowl: walls at 54 degrees (normal
# up 0.58, 10.8 to 12.6 m), the lip at 66 degrees (0.40, 12.6 to 13.0 m), the
# gentle bottom curve (0.85, 9.7 to 10.8 m) is left as ordinary ground.

FLAG_VERT = 0x0008

VERT_AREAS = [
    # name, GTA box (x0, y0, x1, y1), models, height range (m), normal-up range
    # Box = the whole skatepark model (x 1846..1978, y -1487..-1351). The first
    # box (x 1860..1945, y -1460..-1390) cut the bowl off: 128 of its 220 steep
    # faces; the user's test launched out exactly where faces were missing.
    # Height to 15.5 m and steepness from 0.03: the narrow channel's walls go on
    # nearly vertical (normal-up 0.05 / 0.09) from the 13.0 m lip to 15.4 m. LSE
    # has vert on exactly those 12 faces; without it the skater ran into them
    # above the lip and stopped (bowl test 2, 2026-10-08 18:46).
    ("East LS skatepark bowl", (1840.0, -1490.0, 1980.0, -1345.0), ("laeskateparkla",),
     (9.0, 15.5), (0.03, 0.7)),
    # The two half pipes (GTA model laeskatetube1): real curved transitions,
    # 2 x 2 faces per band, normal-up 0.93 / 0.80 (left plain, like the bowl's
    # bottom curve), 0.60 / 0.34 / 0.03 (vert; the top band is 0.029 to 0.034,
    # hence the lower limit 0.02). The boxes and heights leave out
    # the thin sloped side braces (normal-up 0.42, starting at the floor below
    # the ramps), which are not riding surfaces. LSE flags only 8 faces of
    # these half pipes (none on the raised one), so LSE is no guide here.
    ("East LS half pipe (ground)", (1866.0, -1369.0, 1890.0, -1356.0), ("laeskatetube1",),
     (13.7, 18.2), (0.02, 0.7)),
    ("East LS half pipe (raised)", (1948.0, -1380.0, 1961.0, -1364.0), ("laeskatetube1",),
     (18.7, 23.2), (0.02, 0.7)),
]


def model_of(name):
    return name.rsplit("_", 1)[0].lower()


FLAG_WALLRIDE = 0x0004
WALLRIDE_MAX_UP = 0.01  # |normal-up| at most this: a vertical wall (LSE; SceneConverter uses the same limit)


def face_up(obj, i):
    """Normal-up of face i (1 = flat floor, 0 = wall), in the corner order we write."""
    a, b, c = obj.faces[i]
    A, B, C = obj.verts[a], obj.verts[c], obj.verts[b]
    u = [B[k] - A[k] for k in range(3)]
    v = [C[k] - A[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
    return n[1] / length if length else None


def mark_wallride(obj):
    """Wallride (0x0004) on every vertical wall of the map that isn't vert, as
    LSE does (31k faces there; SceneConverter uses the same 0.01 limit).
    Returns the number of faces newly marked."""
    marked = 0
    for i in range(len(obj.faces)):
        if obj.face_flags[i] & (FLAG_VERT | FLAG_WALLRIDE):
            continue
        up = face_up(obj, i)
        if up is not None and abs(up) <= WALLRIDE_MAX_UP:
            obj.face_flags[i] |= FLAG_WALLRIDE
            marked += 1
    return marked


# ---------------------------------------------------------------- grind rails
#
# A rail goes along the outer edge of a flat top that drops away: kerbs,
# ledges, planters, low walls, the bowl's coping. Two kinds of edges:
#  (a) a flat top meets a steep side going down in the same object (ledges,
#      benches, the coping: deck meets the bowl's 65-degree lip);
#  (b) a flat top's edge that no other face of its object shares, with lower
#      ground just beyond it. GTA's roads have no kerb faces at all: the
#      pavement is a separate plane 17 cm above the road (checked at GTA
#      x 1900, y -1456), so kerbs only show up this way.
# How far it drops is measured by looking at the ground 30 cm beyond the
# edge (all flat-ish faces of the map). Corners are matched by position
# (1 cm). Edges are joined into chains and written as RailNode chains
# (format: DESIGN 8.10). THAW units (K per metre), Y up.

RAIL_TOP_MIN_UP = 0.9        # the top face: nearly flat
RAIL_SIDE_MAX_UP = 0.5       # kind (a): the side face is steeper than this (coping lip: 0.42)
RAIL_DROP = (0.08, 1.6)      # m: kerb .. ledge
RAIL_PROBE = 0.3             # m: where beyond the edge the ground is looked at
RAIL_MAX_SLOPE = 0.5         # rise / run of the edge itself (about 27 degrees)
RAIL_MAX_TURN = 35.0         # degrees: sharper bends start a new rail
RAIL_MIN_LENGTH = 1.0        # m: shorter chains are dropped (tree / pole rings, crumbs)
# At most this many rail nodes; the longest rails are kept. The map with
# 19,476 rail nodes (21,963 nodes in all) made reTHAWed stop responding while
# loading (2026-10-08 23:52); LSE has 11,574 rail nodes (12,524 nodes) and
# loads fine, so we stay below that until we know the real limit.
RAIL_NODE_BUDGET = 9000
GROUND_MIN_UP = 0.7          # faces counted as ground for the drop check
GROUND_CELL = 2.0            # m: grid cell for ground lookups


def _key(p):
    return (round(p[0] * 100 / K), round(p[1] * 100 / K), round(p[2] * 100 / K))  # 1 cm


def _normal(A, B, C):
    u = [B[k] - A[k] for k in range(3)]
    v = [C[k] - A[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
    return (n[0] / length, n[1] / length, n[2] / length) if length else None


class Ground:
    """Height of the ground (flat-ish faces) under a point, from a 2D grid."""

    def __init__(self, objs):
        self.cell = GROUND_CELL * K
        self.grid = {}
        for o in objs:
            for a, b, c in o.faces:
                A, B, C = o.verts[a], o.verts[c], o.verts[b]
                n = _normal(A, B, C)
                if n is None or n[1] < GROUND_MIN_UP:
                    continue
                xs = (A[0], B[0], C[0]); zs = (A[2], B[2], C[2])
                for gx in range(int(math.floor(min(xs) / self.cell)), int(math.floor(max(xs) / self.cell)) + 1):
                    for gz in range(int(math.floor(min(zs) / self.cell)), int(math.floor(max(zs) / self.cell)) + 1):
                        self.grid.setdefault((gx, gz), []).append((A, B, C))

    def below(self, x, z, y_max):
        """Highest ground at (x, z) not above y_max, or None."""
        best = None
        for A, B, C in self.grid.get((int(math.floor(x / self.cell)), int(math.floor(z / self.cell))), ()):
            d = (B[2] - C[2]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[2] - C[2])
            if abs(d) < 1e-9:
                continue
            l1 = ((B[2] - C[2]) * (x - C[0]) + (C[0] - B[0]) * (z - C[2])) / d
            l2 = ((C[2] - A[2]) * (x - C[0]) + (A[0] - C[0]) * (z - C[2])) / d
            l3 = 1.0 - l1 - l2
            if min(l1, l2, l3) < -1e-6:
                continue
            y = l1 * A[1] + l2 * B[1] + l3 * C[1]
            if y <= y_max and (best is None or y > best):
                best = y
        return best


def rail_edges(obj, ground):
    """Rail edges of one object, as (point, point) in THAW units."""
    pts = {}
    tri = []
    for a, b, c in obj.faces:
        A, B, C = obj.verts[a], obj.verts[c], obj.verts[b]  # corner order as written
        n = _normal(A, B, C)
        if n is None:
            continue
        keys = [_key(A), _key(B), _key(C)]
        for k, p in zip(keys, (A, B, C)):
            pts.setdefault(k, p)
        tri.append((keys, n))
    edges = {}
    for t, (keys, n) in enumerate(tri):
        for e in ((keys[0], keys[1]), (keys[1], keys[2]), (keys[2], keys[0])):
            edges.setdefault(tuple(sorted(e)), []).append(t)
    out = []
    lo, hi = RAIL_DROP[0] * K, RAIL_DROP[1] * K
    for e, ts in edges.items():
        if e[0] == e[1]:
            continue
        tops = [t for t in ts if tri[t][1][1] >= RAIL_TOP_MIN_UP]
        if len(tops) != 1:
            continue
        others = [t for t in ts if t != tops[0]]
        P, Q = pts[e[0]], pts[e[1]]
        run = math.hypot(Q[0] - P[0], Q[2] - P[2])
        if run < 1e-6 or abs(Q[1] - P[1]) / run > RAIL_MAX_SLOPE:
            continue
        mid = [(P[k] + Q[k]) / 2.0 for k in range(3)]
        top_other = pts[[k for k in tri[tops[0]][0] if k not in e][0]]
        # outward: across the edge, away from the top face, horizontally
        ex, ez = (Q[0] - P[0]) / run, (Q[2] - P[2]) / run
        ox, oz = -ez, ex
        if (top_other[0] - mid[0]) * ox + (top_other[2] - mid[2]) * oz > 0:
            ox, oz = -ox, -oz
        if not others:                                   # kind (b): open edge
            g = ground.below(mid[0] + ox * RAIL_PROBE * K, mid[2] + oz * RAIL_PROBE * K, mid[1] + 0.02 * K)
            if g is None or not (lo <= mid[1] - g <= hi):
                continue
        elif len(others) == 1:                           # kind (a): top meets side
            side_keys, side_n = tri[others[0]]
            if abs(side_n[1]) > RAIL_SIDE_MAX_UP:
                continue
            if side_n[0] * ox + side_n[2] * oz <= 0:     # the side must face outwards (convex)
                continue
            side_other = pts[[k for k in side_keys if k not in e][0]]
            if not (lo <= mid[1] - side_other[1]):       # the side goes down at least a kerb's height
                continue
            g = ground.below(mid[0] + ox * RAIL_PROBE * K, mid[2] + oz * RAIL_PROBE * K, mid[1] + 0.02 * K)
            if g is not None and mid[1] - g < lo:        # something flat right beyond it: not an edge
                continue
            if g is not None and mid[1] - g > hi and mid[1] - side_other[1] > hi:
                continue                                 # a roof edge or a high wall top
        else:
            continue
        out.append((P, Q))
    return out


def chain_rails(segments):
    """Join segments that share an end into chains with gentle bends."""
    ends = {}
    for i, (P, Q) in enumerate(segments):
        ends.setdefault(_key(P), []).append(i)
        ends.setdefault(_key(Q), []).append(i)
    used = [False] * len(segments)

    def turn(d1, d2):
        l1 = math.hypot(*d1); l2 = math.hypot(*d2)
        c = (d1[0] * d2[0] + d1[1] * d2[1]) / (l1 * l2) if l1 and l2 else 1.0
        return math.degrees(math.acos(max(-1.0, min(1.0, c))))

    def extend(chain):
        while True:
            tail, prev = chain[-1], chain[-2]
            d1 = (tail[0] - prev[0], tail[2] - prev[2])
            best = None
            for i in ends.get(_key(tail), []):
                if used[i]:
                    continue
                P, Q = segments[i]
                nxt = Q if _key(P) == _key(tail) else P
                t = turn(d1, (nxt[0] - tail[0], nxt[2] - tail[2]))
                if t <= RAIL_MAX_TURN and (best is None or t < best[0]):
                    best = (t, i, nxt)
            if not best:
                return chain
            used[best[1]] = True
            chain.append(best[2])

    chains = []
    for i, (P, Q) in enumerate(segments):
        if used[i]:
            continue
        used[i] = True
        forward = extend([P, Q])
        backward = extend([forward[1], forward[0]])  # grow the start too
        chain = list(reversed(backward[2:])) + forward
        # drop points on (nearly) straight runs
        slim = [chain[0]]
        for k in range(1, len(chain) - 1):
            a, b, c = slim[-1], chain[k], chain[k + 1]
            if turn((b[0] - a[0], b[2] - a[2]), (c[0] - b[0], c[2] - b[2])) > 3.0 or abs(
                    (b[1] - a[1]) - (c[1] - b[1]) * math.hypot(b[0] - a[0], b[2] - a[2]) / max(
                        1e-6, math.hypot(c[0] - b[0], c[2] - b[2]))) > 0.05 * K:
                slim.append(b)
        slim.append(chain[-1])
        length = sum(math.dist(slim[k], slim[k + 1]) for k in range(len(slim) - 1))
        if length >= RAIL_MIN_LENGTH * K:
            chains.append(slim)
    return chains


def find_rails(objs, budget=RAIL_NODE_BUDGET):
    """Rail chains, longest first, at most `budget` nodes in all.
    Returns (chains, number found before the budget)."""
    ground = Ground(objs)
    segments = []
    for o in objs:
        segments += rail_edges(o, ground)
    chains = chain_rails(segments)
    chains.sort(key=lambda c: -sum(math.dist(c[k], c[k + 1]) for k in range(len(c) - 1)))
    # 8.32 R1: how big would "all rails" be?
    total = sum(len(c) for c in chains)
    metres = sum(math.dist(c[k], c[k + 1]) for c in chains for k in range(len(c) - 1)) / K
    print("rails found:  %d rails, %d rail nodes, %.0f km (before the budget)" % (len(chains), total, metres / 1000))
    kept, used = [], 0
    for c in chains:
        if budget > 0 and used + len(c) > budget:  # budget 0 = no limit
            continue
        kept.append(c)
        used += len(c)
    return kept, len(chains)


def mark_vert(obj, centre):
    """Set FLAG_VERT on obj's faces that lie in a VERT_AREAS area, and
    FLAG_WALLRIDE on that area's vertical walls (as LSE does in the skatepark).
    Returns the number of vert faces."""
    model = model_of(obj.name)
    areas = [a for a in VERT_AREAS if model in a[2]]
    if not areas:
        return 0
    marked = 0
    for i, (a, b, c) in enumerate(obj.faces):
        A, B, C = obj.verts[a], obj.verts[c], obj.verts[b]  # corner order as written
        u = [B[k] - A[k] for k in range(3)]
        v = [C[k] - A[k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
        if not length:
            continue
        up = n[1] / length
        gta = [(centre[0] + p[0] / K, centre[1] - p[2] / K, p[1] / K) for p in (A, B, C)]
        for _name, box, _models, (z0, z1), (lo, hi) in areas:
            if not (all(box[0] <= g[0] <= box[2] and box[1] <= g[1] <= box[3] for g in gta)
                    and all(z0 <= g[2] <= z1 for g in gta)):
                continue
            if lo <= up <= hi:
                obj.face_flags[i] |= FLAG_VERT
                marked += 1
            elif abs(up) <= WALLRIDE_MAX_UP:
                obj.face_flags[i] |= FLAG_WALLRIDE
            break
    return marked


def to_thaw(v, centre):
    gx, gy, gz = v
    return ((gx - centre[0]) * K, gz * K, -(gy - centre[1]) * K)


def build_bsp(obj, nodes, facelist):
    """Append obj's BSP tree to nodes (8-byte entries) / facelist; return root byte offset.
    Split: left = less than the point; faces crossing go to both sides (as in LSE)."""
    tri_min = [tuple(min(obj.verts[i][a] for i in f) for a in range(3)) for f in obj.faces]
    tri_max = [tuple(max(obj.verts[i][a] for i in f) for a in range(3)) for f in obj.faces]

    root = len(nodes)
    nodes.append(None)

    def fill(slot, faces, depth):
        if len(faces) <= LEAF_FACES or depth >= MAX_DEPTH:
            nodes[slot] = ("leaf", len(faces), len(facelist))
            facelist.extend(faces)
            return
        lo = [min(tri_min[f][a] for f in faces) for a in range(3)]
        hi = [max(tri_max[f][a] for f in faces) for a in range(3)]
        axis = max(range(3), key=lambda a: hi[a] - lo[a])
        cents = sorted((tri_min[f][axis] + tri_max[f][axis]) / 2.0 for f in faces)
        point = math.floor(cents[len(cents) // 2] * 16.0) / 16.0
        left = [f for f in faces if tri_min[f][axis] < point]
        right = [f for f in faces if tri_max[f][axis] >= point]
        if len(left) == len(faces) or len(right) == len(faces) or not left or not right:
            nodes[slot] = ("leaf", len(faces), len(facelist))
            facelist.extend(faces)
            return
        li = len(nodes)
        nodes.append(None)
        nodes.append(None)
        nodes[slot] = ("split", axis, point, li)
        fill(li, left, depth + 1)
        fill(li + 1, right, depth + 1)

    fill(root, list(range(len(obj.faces))), 0)
    return root * 8


# ---------------------------------------------------------------- merging into tiles
#
# reTHAWed keeps a hash entry per collision object / scene sector (and per
# node) in one pool of 30,000 ("Out of HashItems (30000 max)" with the whole
# map's 28,220 objects, 2026-10-09 01:09). So for big maps the objects are
# merged into square tiles: far fewer objects, same triangles and flags.
# Done after vert / wallride / rails, which need GTA's separate objects.

MERGE_TILE = 200.0   # m; 0 = keep GTA's objects as they are
MAX_OBJECT = 65535   # corners or faces per collision object (16-bit counts)


def merge_tiles(objs, tile_m):
    if tile_m <= 0:
        return objs
    tile = tile_m * K
    groups = {}
    for o in objs:
        cx = (o.mn[0] + o.mx[0]) / 2.0
        cz = (o.mn[2] + o.mx[2]) / 2.0
        groups.setdefault((int(math.floor(cx / tile)), int(math.floor(cz / tile))), []).append(o)
    out = []
    for (gx, gz), members in sorted(groups.items()):
        part = 0
        verts, faces, flags = [], [], []
        for o in members:
            if verts and (len(verts) + len(o.verts) > MAX_OBJECT or len(faces) + len(o.faces) > MAX_OBJECT):
                out.append(_tile_object(gx, gz, part, verts, faces, flags))
                part += 1
                verts, faces, flags = [], [], []
            base = len(verts)
            verts += o.verts
            faces += [(a + base, b + base, c + base) for a, b, c in o.faces]
            flags += o.face_flags
        if verts:
            out.append(_tile_object(gx, gz, part, verts, faces, flags))
    return out


def _tile_object(gx, gz, part, verts, faces, flags):
    o = ColObject("tile_%d_%d_%d" % (gx, gz, part), verts, faces)
    o.face_flags = flags
    return o


def write_col(objs):
    nv = sum(len(o.verts) for o in objs)
    nf = sum(len(o.faces) for o in objs)
    mn = [min(o.mn[a] for o in objs) for a in range(3)]
    mx = [max(o.mx[a] for o in objs) for a in range(3)]
    rows = max(1, int(round((mx[0] - mn[0]) / 1000.0)))
    cols = max(1, int(round((mx[2] - mn[2]) / 1000.0)))

    nodes, facelist, bsp_offsets = [], [], []
    for o in objs:
        bsp_offsets.append(build_bsp(o, nodes, facelist))

    out = bytearray()
    out += struct.pack("<7I", 10, len(objs), nv, nf, 0, nv, 0)
    out += b"\0" * 12
    out += struct.pack("<2I", rows, cols)
    out += struct.pack("<8f", mn[0], mn[1], mn[2], 1.0, mx[0], mx[1], mx[2], 1.0)
    assert len(out) == 0x50
    face_off = vert_off = light_off = 0
    for o, bsp in zip(objs, bsp_offsets):
        out += struct.pack("<IHHHBBI", o.key, 0, len(o.verts), len(o.faces), 0, 0, face_off)
        out += struct.pack("<8f", o.mn[0], o.mn[1], o.mn[2], 1.0, o.mx[0], o.mx[1], o.mx[2], 1.0)
        out += struct.pack("<IIif", vert_off, bsp, light_off, 0.0)
        face_off += 10 * len(o.faces)
        vert_off += 12 * len(o.verts)
        light_off += len(o.verts)
    for o in objs:
        for v in o.verts:
            out += struct.pack("<3f", *v)
    out += b"\xff" * nv  # vertex light: full (LSE: 255 almost everywhere)
    out += b"\0" * (-len(out) % 4)
    for o in objs:
        for (a, b, c), face_flags in zip(o.faces, o.face_flags):
            # Corners in reverse order (a, c, b): THAW's ground faces have
            # (B-A) x (C-A) pointing up (LSE: 94k up, 35k down); GTA's order gave
            # the opposite (14k up, 41k down), so the ground faced downwards and
            # the skater hung in the air (attempt 5). Flags: vert where marked
            # (mark_vert), else 0; terrain 0 (LSE: mostly 0).
            out += struct.pack("<5H", face_flags, 0, a, c, b)
    out += b"\0" * nf  # "optimization" byte per face (LSE: all 0)
    out += b"\0" * (-len(out) % 4)
    node_bytes = bytearray()
    for n in nodes:
        if n[0] == "leaf":
            node_bytes += struct.pack("<BBHI", 3, 0, n[1], n[2])
        else:
            _, axis, point, li = n
            node_bytes += struct.pack("<iI", (int(math.floor(point * 16.0)) << 2) | axis, li * 8)
    out += struct.pack("<I", len(node_bytes)) + node_bytes
    out += struct.pack("<%dH" % len(facelist), *facelist)
    out += b"\0" * (-len(out) % 4)
    stats = dict(objects=len(objs), verts=nv, faces=nf, nodes=len(nodes), face_refs=len(facelist),
                 rows=rows, cols=cols, box=(mn, mx))
    return bytes(out), stats


# ---------------------------------------------------------------- level script (compiled QB)

QB_MAGIC = (0x0402081C, 0x0C080410, 0x0402080C, 0x0C040214, 0x000C1010)  # as in LSE's header


def array_header(kind):
    """First word of an array: bytes 00 01 <kind> 00 (LSE). Attempts 1 to 3 wrote
    the node list's as 00 0A 01 00 (bytes swapped), which is what crashed
    attempt 3 (and attempt 2) while reading the level script."""
    return 0x00000100 | (kind << 16)


class QB:
    """Writes the subset of THAW's compiled script format seen in Los Santos East:
    one root array of structs; struct items: vector, checksum, flag, array of
    checksums, string. All pointers are byte offsets from the start of the file."""

    def __init__(self, prefix):
        self.pak = qbkey(prefix)
        self.b = bytearray(b"\0" * 28)

    def here(self):
        return len(self.b)

    def u32(self, *v):
        self.b += struct.pack("<%dI" % len(v), *v)

    def struct_items(self, items):
        """items: list of (kind, name, value). Returns offset of the struct header."""
        start = self.here()
        self.u32(0x00010000, start + 8)
        for i, (kind, name, value) in enumerate(items):
            last = i == len(items) - 1
            item = self.here()
            if kind == "vector":
                data = item + 16
                nxt = 0 if last else data + 16
                self.u32(0x00000D00, qbkey(name), data, nxt)
                self.u32(0x00000100)
                self.b += struct.pack("<3f", *value)
            elif kind == "checksum":
                self.u32(0x00001B00, qbkey(name), value, 0 if last else item + 16)
            elif kind == "checksum_key":  # name given as a checksum (name unknown)
                self.u32(0x00001B00, name, value, 0 if last else item + 16)
            elif kind == "flag":
                self.u32(0x00001B00, 0, value, 0 if last else item + 16)
            elif kind == "checksums":
                data = item + 16
                vals = item + 16 + 12
                nxt = 0 if last else vals + 4 * len(value)
                self.u32(0x00001900, qbkey(name), data, nxt)
                self.u32(array_header(0x0D), len(value), vals)
                self.u32(*value)
            elif kind == "link":
                # Links = [index]: a one-entry array is stored inline in LSE
                # (header, count 1, the value itself; no pointer).
                data = item + 16
                nxt = 0 if last else data + 12
                self.u32(0x00001900, qbkey(name), data, nxt)
                self.u32(array_header(0x01), 1, value)
            elif kind == "string":
                data = item + 16
                raw = value.encode("ascii") + b"\0"
                raw += b"\0" * (-len(raw) % 4)
                nxt = 0 if last else data + len(raw)
                self.u32(0x00000700, qbkey(name), data, nxt)
                self.b += raw
            else:
                raise ValueError(kind)
        return start

    def node_array(self, name, structs):
        item = self.here()
        self.u32(0x000C0400, qbkey(name), self.pak, item + 20, 0)
        arr = self.here()
        self.u32(array_header(0x0A), len(structs), arr + 12)  # 0x0A: array of structs
        table = self.here()
        self.b += b"\0" * (4 * len(structs))
        for i, items in enumerate(structs):
            off = self.struct_items(items)
            struct.pack_into("<I", self.b, table + 4 * i, off)

    def value_array(self, name, kind, values):
        """Top-level array of checksums (kind 0x0D) or integers (kind 0x01), as LSE's
        <prefix>_NodeArray_SortedNames / _SortedIndices."""
        item = self.here()
        self.u32(0x000C0400, qbkey(name), self.pak, item + 20, 0)
        arr = self.here()
        self.u32(array_header(kind), len(values), arr + 12)
        self.u32(*values)

    def root_struct(self, key, items):
        """Top-level struct (item kind 0x0A), as the one in LSE's sky script."""
        item = self.here()
        self.u32(0x000A0400, key, self.pak, item + 20, 0)
        self.struct_items(items)

    def empty_script(self, key):
        """Top-level script that does nothing, byte for byte as LSE's empty ones:
        item kind 0x07, then 0xBABEFACE, size 2, packed size 2, bytes 01 24
        (end of line, end of script), padded to 4."""
        item = self.here()
        self.u32(0x00070400, key, self.pak, item + 20, 0)
        self.u32(0xBABEFACE, 2, 2)
        self.b += b"\x01\x24\x00\x00"

    def finish(self):
        struct.pack_into("<7I", self.b, 0, 0, len(self.b), *QB_MAGIC)
        return bytes(self.b)


# The 8 empty scripts at the start of LSE's level script; names unknown (only
# their checksums), kept as they are in case the game looks for them.
LSE_EMPTY_SCRIPT_KEYS = (0x5774827A, 0xE5947879, 0x3541FE81, 0x28673697,
                         0x00DC351A, 0xBA114765, 0x7FB679EB, 0x4F2A84F7)


def write_level_qb(prefix, objs, spawn_thaw, rails=(), geometry_nodes=True):
    """rails: list of chains, each a list of THAW points (see find_rails).
    geometry_nodes: one LevelGeometry node per collision object (as so far).
    LSE has them for only 715 of its 7,506 collision objects, so the collision
    may not need them; the whole map would have 28,213 (phase 4-4)."""
    qb = QB(prefix)
    nodes = [[
        ("vector", "Pos", spawn_thaw),
        ("vector", "Angles", (0.0, 0.0, 0.0)),
        ("checksum", "Name", qbkey("Player1")),
        ("checksum", "Class", qbkey("Restart")),
        ("checksum", "Type", qbkey("Player1")),
        # restart_types: the three values Los Santos East's Player 1 start uses
        ("checksums", "restart_types", [0x41639CE5, 0xD86ACD5F, 0xBAE0E4DA]),
        ("string", "RestartName", "P1: Restart"),
    ]]
    for o in (objs if geometry_nodes else ()):
        centre = tuple((o.mn[a] + o.mx[a]) / 2.0 for a in range(3))
        nodes.append([
            ("vector", "Pos", centre),
            ("vector", "Angles", (0.0, 0.0, 0.0)),
            ("checksum", "Name", o.key),
            ("checksum", "Class", qbkey("LevelGeometry")),
            ("flag", "", qbkey("CreatedAtStart")),
            ("checksum", "CollisionMode", qbkey("Geometry")),
        ])
    rail_names = []
    for r, chain in enumerate(rails):
        for i, p in enumerate(chain):
            name = qbkey("%s_Rail%d_%d" % (prefix, r, i))
            rail_names.append(name)
            items = [
                ("vector", "Pos", p),
                ("vector", "Angles", (0.0, 0.0, 0.0)),
                ("checksum", "Name", name),
                ("checksum", "Class", qbkey("RailNode")),
                ("checksum", "Type", qbkey("Concrete")),
                ("checksum", "CollisionMode", qbkey("Geometry")),
                ("checksum", "TerrainType", qbkey("TERRAIN_GRINDCONC")),
                ("flag", "", qbkey("CreatedAtStart")),
            ]
            if i < len(chain) - 1:
                items.append(("link", "Links", len(nodes) + 1))  # the next node of this rail
            nodes.append(items)
    # Attempt 3 (4-2d): attempt 2 crashed on loading this file. LSE's level
    # script also has empty scripts the game may call while loading, and two
    # lookup arrays next to the node list. We add the same (empty) ones.
    for key in LSE_EMPTY_SCRIPT_KEYS:
        qb.empty_script(key)
    qb.node_array(prefix + "_NodeArray", nodes)
    for name in ("_Goals", "_Startup", "_Setup"):
        qb.empty_script(qbkey(prefix + name))
    for name in ("LoadAllParticleTextures", "LoadTerrain", "load_level_anims", "LoadCameras", "LoadObjectAnims"):
        qb.empty_script(qbkey(name))
    names = [qbkey("Player1")] + ([o.key for o in objs] if geometry_nodes else []) + rail_names  # node order
    qb.value_array(prefix + "_NodeArray_SortedNames", 0x0D, names)
    qb.value_array(prefix + "_NodeArray_SortedIndices", 0x01, list(range(len(names))))
    return qb.finish(), len(nodes)


# ---------------------------------------------------------------- sky PAK
#
# Attempt 5 (4-2d): attempt 4 loaded the whole level (textures, scene, level
# script, collision: "Loading collision... Loaded!"), then crashed right after
# "NEW SKY PATH: ...mpgtalse_sky.pak.wpc" (read of address 0). Our sky PAK was
# empty. LSE's has a texture file, a scene and a small script, all with PAK
# flag 1, named skies\<prefix>_sky\<prefix>_sky.*. We write the same shape:
# no textures, one empty sector, and a script laid out like LSE's sky script.

# Two checksums from LSE's sky script whose names we don't know: a top-level
# struct (Class = LevelGeometry, CreatedAtStart) and a flag on each sky node.
LSE_SKY_STRUCT_KEY = 0x6A222D81
LSE_SKY_NODE_FLAG = 0xB31DD4B2


class _SkyPiece:
    def __init__(self, name):
        self.name = name
        self.key = qbkey(name)
        self.mn = (-1.0, -1.0, -1.0)
        self.mx = (1.0, 1.0, 1.0)


def write_sky_pak(prefix):
    sky = prefix + "_sky"
    # Two pieces, as LSE's sky has two: a one-entry list may be stored
    # differently (not checked), so we avoid it.
    pieces = [_SkyPiece(sky + "_dummy1"), _SkyPiece(sky + "_dummy2")]
    qb = QB(sky)
    nodes = [[
        ("vector", "Pos", (0.0, 0.0, 0.0)),
        ("vector", "Angles", (0.0, 0.0, 0.0)),
        ("checksum", "Name", piece.key),
        ("checksum", "Class", qbkey("LevelGeometry")),
        ("flag", "", qbkey("CreatedAtStart")),
        ("flag", "", LSE_SKY_NODE_FLAG),
    ] for piece in pieces]
    qb.node_array(sky + "_NodeArray", nodes)
    qb.value_array(sky + "_NodeArray_SortedNames", 0x0D, [p.key for p in pieces])
    qb.value_array(sky + "_NodeArray_SortedIndices", 0x01, list(range(len(pieces))))
    qb.root_struct(LSE_SKY_STRUCT_KEY, [("checksum", "Class", qbkey("LevelGeometry")),
                                        ("flag", "", qbkey("CreatedAtStart"))])
    base = "skies\\%s\\%s" % (sky.lower(), sky.lower())
    named = "skies\\%s\\%s.qb.wpc" % (sky, sky)
    return write_pak([(T_TEX, base + ".tex", None, write_tex(), None, 1),
                      (T_GEOM, base + ".scn", None, write_scene(pieces), base + ".tex", 1),
                      (T_QB, base + ".qb", named, qb.finish(), base + ".scn", 1)])


# ---------------------------------------------------------------- scene (.geom) and textures (.tex)
#
# Attempt 2 (4-2d): attempt 1 crashed reTHAWed while loading the collision
# (debug.txt: "Loading asset ... (Type: 5 ...)" then FATAL ERROR, read of
# address 0). In Los Santos East the collision and the level script are
# children of the scene (PAK parent field), and the scene is a child of the
# texture file; ours had no scene, so the collision had nothing to attach to.
# Now: an empty texture file and a scene with one empty sector per collision
# object (no materials, no meshes: invisible). LSE has such empty sectors
# too (126 of 7,669), and 23 collision objects with no sector at all.

def write_tex():
    """THAW texture file with no textures: magic, version 1, 4, u16 count."""
    return struct.pack("<IBBH", 0xABADD00D, 1, 4, 0)


def write_scene(objs):
    """THAW (PC) scene file with no materials and one empty sector per object.
    Layout as in Los Santos East: disqualifier offset, 7 x 0xCABAAAFA,
    material list, 0xBABEFACE block, then the 172-byte scene header,
    48-byte sectors, 104-byte geoms, 20 bytes per sector, no meshes."""
    out = bytearray(struct.pack("<I", 0))
    out += struct.pack("<7I", *([0xCABAAAFA] * 7))
    # Material list (LSE: u8 2, u8 0x10, u16 count, u32 size up to 0xBABEFACE,
    # u32 16 = first material, u32 -1).
    out += struct.pack("<BBHIIi", 2, 0x10, 0, 16, 16, -1)
    out += struct.pack("<3I", 0xBABEFACE, 8, 234) + b"\0" * 4
    scene = len(out)
    n = len(objs)
    mn = [min(o.mn[a] for o in objs) for a in range(3)]
    mx = [max(o.mx[a] for o in objs) for a in range(3)]
    centre = [(mn[a] + mx[a]) / 2.0 for a in range(3)]
    radius = math.sqrt(sum((mx[a] - centre[a]) ** 2 for a in range(3)))
    hdr = bytearray()
    hdr += struct.pack("<HH", 1, 172) + b"\0" * 20 + b"\xff" * 8 + struct.pack("<I", 0)
    hdr += struct.pack("<8f", mn[0], mn[1], mn[2], 0.0, mx[0], mx[1], mx[2], 0.0)
    hdr += struct.pack("<4f", centre[0], centre[1], centre[2], radius)
    hdr += struct.pack("<I", 0) + b"\0" * 16 + b"\xff" * 8 + b"\0" * 8
    hdr += struct.pack("<i", -1) + b"\0" * 4  # no hierarchy
    sectors = 172
    geoms = sectors + 48 * n
    big = geoms + 104 * n
    meshes = big + 20 * n
    hdr += struct.pack("<I5i", n, sectors, geoms, -1, big, meshes)
    hdr += b"\0" * 16 + struct.pack("<I", 161)
    assert len(hdr) == 172
    out += hdr
    for o in objs:
        out += struct.pack("<3I", 0, o.key, 0x3) + b"\0" * 36
    for o in objs:
        out += struct.pack("<I", 48 * n) + b"\0" * 20
        out += struct.pack("<8f", o.mn[0], o.mn[1], o.mn[2], 0.0, o.mx[0], o.mx[1], o.mx[2], 0.0)
        out += b"\0" * 12 + struct.pack("<I", 0) + b"\0" * 32
    out += b"\0" * (20 * n)
    assert len(out) == scene + meshes
    out += b"\xaa" * (-len(out) % 32)
    return bytes(out)


# ---------------------------------------------------------------- PAK

T_COL, T_NQB, T_LAST = 0x72A6D78C, 0x49875607, 0xB524565F
T_TEX, T_GEOM, T_QB = 0x8BFA5E8E, 0x7EA7357B, 0xA7F505C4


def write_pak(files):
    """files: list of (type, full name or None, file name with .wpc or None, data
    [, parent full name[, extra flags]]). Extra flag 1 = sky file (as in LSE's sky PAK)."""
    entries = files + [(T_LAST, None, None, b"\xab\xab\xab\xab")]
    entries = [tuple(e) + (None, 0)[len(e) - 4:] for e in entries]
    head = bytearray()
    positions = []
    for t, full, named, data, parent, extra in entries:
        positions.append(len(head))
        flags = (0x20 if named else 0) | extra
        head += struct.pack("<8I", t, 0, len(data), qbkey(full) if full else 0, 0,
                            qbkey(named) if named else 0, qbkey(parent) if parent else 0, flags)
        if named:
            raw = named.encode("ascii")
            head += raw + b"\0" * (160 - len(raw))
    head += b"\0" * (-len(head) % 4096)
    body = bytearray()
    for i, (t, full, named, data, parent, extra) in enumerate(entries):
        start = len(head) + len(body)
        struct.pack_into("<I", head, positions[i] + 4, start - positions[i])
        body += data
        if i < len(entries) - 1:
            body += b"\0" * (-len(body) % 32)
    out = head + body
    out += b"\xab" * (-len(out) % 4096)
    return bytes(out)


# ---------------------------------------------------------------- main

def main():
    project = os.path.dirname(HERE)
    ap = argparse.ArgumentParser(description="Make a reTHAWed user map from GTA SA collision.")
    ap.add_argument("gta", nargs="?", help="GTA San Andreas folder")
    ap.add_argument("--from-obj", help="use a collision_gta.obj from gta_area_dump.py instead of the GTA install")
    ap.add_argument("--area", choices=sorted(gad.AREAS), default="lse")
    ap.add_argument("--prefix", default=DEFAULT_PREFIX)
    ap.add_argument("--name", default=DEFAULT_NAME)
    ap.add_argument("--folder", default=DEFAULT_FOLDER)
    ap.add_argument("--out", help="output folder (default <project>/extracted/thaw_maps/<folder>)")
    ap.add_argument("--no-rails", action="store_true", help="leave out the grind rails")
    ap.add_argument("--no-extras", action="store_true", help="leave out the hand-made walls of gta_extras.py")
    ap.add_argument("--box", nargs=4, type=float, metavar=("XMIN", "YMIN", "XMAX", "YMAX"),
                    help="GTA area to convert instead of --area (map size tests); same centre as --area")
    ap.add_argument("--no-merge", action="store_true",
                    help="keep one collision object per GTA placement (no %.0f m tiles)" % MERGE_TILE)
    ap.add_argument("--no-geometry-nodes", action="store_true",
                    help="no LevelGeometry node per collision object (map size tests)")
    ap.add_argument("--rail-budget", type=int, default=RAIL_NODE_BUDGET,
                    help="at most this many rail nodes, longest rails first (default %d; 0 = no limit)" % RAIL_NODE_BUDGET)
    args = ap.parse_args()
    if not args.gta and not args.from_obj:
        ap.error("give the GTA folder or --from-obj")

    area = gad.AREAS[args.area]
    # The centre stays the area's centre even with --box, so the alignment in
    # src/common/convert.h (kGtaEastLs) stays right for any box size.
    centre = ((area[0] + area[2]) / 2.0, (area[1] + area[3]) / 2.0)
    box = tuple(args.box) if args.box else area
    out = args.out or os.path.join(project, "extracted", "thaw_maps", args.folder)
    os.makedirs(out, exist_ok=True)
    print("output to:   ", out)

    raw = objects_from_obj(args.from_obj) if args.from_obj else objects_from_gta(args.gta, box)
    extras = [] if args.no_extras else gta_extras.wall_objects()
    raw = list(raw) + extras
    objs = []
    skipped = 0
    for name, verts, faces in raw:
        faces = [f for f in faces if len(set(f)) == 3]
        if not faces or len(verts) > 65535 or len(faces) > 65535:
            skipped += 1
            continue
        objs.append(ColObject(name, [to_thaw(v, centre) for v in verts], faces))
    print("objects:      %d (skipped %d empty or too big)" % (len(objs), skipped))
    vert = sum(mark_vert(o, centre) for o in objs)
    print("vert faces:   %d (%s)" % (vert, ", ".join(a[0] for a in VERT_AREAS)))
    sum(mark_wallride(o) for o in objs)
    wall = sum(1 for o in objs for f in o.face_flags if f & FLAG_WALLRIDE)
    print("wallride:     %d faces (every vertical wall)" % wall)

    # Hand-made walls (gta_extras.py) bring their own rails; the finder only
    # looks at GTA's collision.
    extra_rails = [] if (args.no_rails or args.no_extras) else [
        [to_thaw(p, centre) for p in chain] for chain in gta_extras.wall_rails()]
    budget = args.rail_budget - sum(len(c) for c in extra_rails)
    rails, found = ([], 0) if args.no_rails else find_rails(
        [o for o in objs if not o.name.startswith("extra_")], budget)
    rails = extra_rails + rails
    print("extras:       %d hand-made walls (gta_extras.py), %d rails along them" % (len(extras), len(extra_rails)))
    print("rails:        %d rails, %d rail nodes, %.0f m in all (found %d rails; longest kept, at most %s nodes)" % (
        len(rails), sum(len(r) for r in rails),
        sum(math.dist(r[k], r[k + 1]) for r in rails for k in range(len(r) - 1)) / K, found,
        args.rail_budget if args.rail_budget > 0 else "unlimited"))
    # 8.32: reTHAWed's memory pools these rails need (rethawed.ini [Memory]; its defaults:
    # RailPoint 20000, RailString 5000, HashItem 30000, CStruct 100000, CComponent 360000,
    # CVector 30000). Each rail node is one struct of about 8 items with 2 vectors (position,
    # angles) in the level script, plus a rail point; the defaults are kept as room for the game.
    nn = sum(len(r) for r in rails)
    print("rethawed.ini: this map needs at least, under [Memory]:")
    for key, value in (("Out set RailPoint", nn + 20000), ("Out set RailString", len(rails) + 5000),
                       ("HashItem", nn + 40000), ("CStruct", nn + 100000), ("CComponent", 10 * nn + 360000),
                       ("CVector", 2 * nn + 30000)):
        print("                %s=%d" % (key, value))

    if not args.no_merge:
        before = len(objs)
        objs = merge_tiles(objs, MERGE_TILE)
        print("merged:       %d objects into %d tiles of %.0f m" % (before, len(objs), MERGE_TILE))
    col, st = write_col(objs)
    spawn = to_thaw(DEFAULT_SPAWN, centre)
    spawn = (spawn[0], spawn[1] + 10.0, spawn[2])  # a little above the ground
    qb, nnodes = write_level_qb(args.prefix, objs, spawn, rails, not args.no_geometry_nodes)
    # 8.32 R3a: the rail nodes as written (node number in the level's node list,
    # rail, point, THAW position), so the THAW plugin can find reTHAWed's rail
    # list in memory by these exact positions.
    first = 1 + (len(objs) if not args.no_geometry_nodes else 0)
    with open(os.path.join(out, "rails_in_map.csv"), "w", newline="\n") as f:
        f.write("node,rail,point,x,y,z\n")
        n = first
        for r, chain in enumerate(rails):
            for i, p in enumerate(chain):
                f32 = struct.unpack("<3f", struct.pack("<3f", *p))  # exactly as stored in the level
                f.write("%d,%d,%d,%.9g,%.9g,%.9g\n" % (n, r, i, f32[0], f32[1], f32[2]))
                n += 1
    base = "worlds\\worldzones\\%s\\%s" % (args.prefix.lower(), args.prefix.lower())
    named_qb = "worlds\\worldzones\\%s\\%s.qb.wpc" % (args.prefix, args.prefix)
    # Order and parents as in Los Santos East: tex, scene (parent tex),
    # level script and collision (parent scene).
    pak = write_pak([(T_TEX, base + ".tex", None, write_tex()),
                     (T_GEOM, base + ".scn", None, write_scene(objs), base + ".tex"),
                     (T_NQB, base + ".qb", named_qb, qb, base + ".scn"),
                     (T_COL, base + ".col", None, col, base + ".scn")])
    open(os.path.join(out, args.prefix + ".pak.wpc"), "wb").write(pak)
    open(os.path.join(out, args.prefix + "_sky.pak.wpc"), "wb").write(write_sky_pak(args.prefix))
    open(os.path.join(out, args.prefix + "_sfx.pak.wpc"), "wb").write(write_pak([]))
    with open(os.path.join(out, "level.ini"), "w", newline="\r\n") as f:
        f.write("[ModInfo]\nDescription=GTA San Andreas collision, made by my-passthrough (local use only).\n"
                "Author=my-passthrough\nVersion=0.1\n\n[LevelInfo]\nName=%s\n"
                "Description=GTA San Andreas collision, made by my-passthrough.\nTODPreset=THUG2\nLevelPrefix=%s\n"
                % (args.name, args.prefix))
    with open(os.path.join(out, "map_info.txt"), "w", newline="\n") as f:
        f.write("prefix %s, level ID (load_<prefix>) 0x%08X\n" % (args.prefix, qbkey("load_" + args.prefix)))
        f.write("area x %.0f..%.0f y %.0f..%.0f, centre (%.3f, %.3f)\n" % (box[0], box[2], box[1], box[3], *centre))
        f.write("to GTA: x = cx + X / %.4f, y = cy - Z / %.4f, z = Y / %.4f\n" % (K, K, K))
        f.write("collision: %(objects)d objects, %(verts)d corners, %(faces)d triangles, %(nodes)d BSP nodes, "
                "%(face_refs)d BSP face entries, super sectors %(rows)d x %(cols)d\n" % st)
        f.write("nodes: %d (1 Restart + LevelGeometry per object + %d RailNode in %d rails)\n" % (
            nnodes, sum(len(r) for r in rails), len(rails)))
        f.write("spawn (THAW units): %.1f %.1f %.1f\n" % spawn)
        f.write("region barriers left out: %d (removed_barriers.txt)\n" % len(REMOVED_BARRIERS))
    with open(os.path.join(out, "removed_barriers.txt"), "w", newline="\n") as f:
        f.write("# GTA region barriers left out of the map (DESIGN 8.31 D1)\n")
        f.write("# model, GTA position x y z\n")
        for name, pos in REMOVED_BARRIERS:
            f.write("%s, %.1f %.1f %.1f\n" % (name, pos[0], pos[1], pos[2]))
    print("barriers:     %d placements left out (removed_barriers.txt)" % len(REMOVED_BARRIERS))
    # 8.31: which GTA model each piece of collision came from (the map's tiles
    # don't keep names), for finding what an invisible wall is
    if PLACEMENTS:
        with open(os.path.join(out, "placements.csv"), "w", newline="\n") as f:
            f.write("model,source,xmin,ymin,zmin,xmax,ymax,zmax,triangles\n")
            for name, src, lo, hi, n in PLACEMENTS:
                f.write("%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n" % (name, src, lo[0], lo[1], lo[2], hi[0], hi[1],
                                                                     hi[2], n))
        print("placements:   %d GTA placements listed in placements.csv" % len(PLACEMENTS))
    if gad.SKIPPED:
        with open(os.path.join(out, "skipped.csv"), "w", newline="\n") as f:
            f.write("model,id,source,x,y,z,interior,why\n")
            for inst, why in gad.SKIPPED:
                f.write("%s,%d,%s,%.2f,%.2f,%.2f,%d,%s\n" % (inst["name"] or "", inst["id"], inst.get("source", ""),
                                                          inst["pos"][0], inst["pos"][1], inst["pos"][2],
                                                          inst["interior"], why))
        print("skipped:      %d GTA placements not used, with the reason, in skipped.csv" % len(gad.SKIPPED))
    print("collision:    %d objects, %d corners, %d triangles, %d BSP nodes" % (
        st["objects"], st["verts"], st["faces"], st["nodes"]))
    nrail = sum(len(r) for r in rails)
    print("level script: %d nodes (1 start point + %d LevelGeometry + %d RailNode)" % (
        nnodes, nnodes - 1 - nrail, nrail))
    print("files:        %s.pak.wpc (%d bytes), %s_sky.pak.wpc, %s_sfx.pak.wpc, level.ini, map_info.txt" % (
        args.prefix, len(pak), args.prefix, args.prefix))
    print("level ID:     0x%08X (load_%s)" % (qbkey("load_" + args.prefix), args.prefix))
    print("done")


if __name__ == "__main__":
    sys.exit(main())
