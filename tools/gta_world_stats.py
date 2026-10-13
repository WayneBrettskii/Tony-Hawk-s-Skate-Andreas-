"""SkateAndreas: how much collision does all of San Andreas have? (phase 4-4 R1)
READ ONLY.

Reads the whole map from the GTA install with the same reader as
gta_area_dump.py (interiors and far-away LOD stand-ins skipped, nothing is
changed in the install) and writes into <project>/extracted/gta_world/
(gitignored):
  - world_stats.csv   per 500 m square: placements, collision objects,
                      triangles, vertical faces (walls), flat faces (floors),
                      lowest / highest point
  - summary.txt       totals, the busiest squares, what East LS (our test map)
                      has for comparison

No 3D file is written (it would be hundreds of MB). Takes a few minutes.

Usage (from the project folder):
  python tools\\gta_world_stats.py "D:\\Games\\GTA San Andreas"
"""

import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gta_area_dump as gad  # noqa: E402

WORLD = (-3100.0, -3100.0, 3100.0, 3100.0)  # all of San Andreas (about 6 x 6 km) and a bit
CELL = 500.0                                 # m
LSE_BOX = gad.AREAS["lse"]


def main():
    project = os.path.dirname(HERE)
    ap = argparse.ArgumentParser(description="Collision statistics for all of San Andreas (read only).")
    ap.add_argument("gta", help="GTA San Andreas folder")
    ap.add_argument("--out", help="output folder (default <project>/extracted/gta_world)")
    args = ap.parse_args()
    out = args.out or os.path.join(project, "extracted", "gta_world")
    os.makedirs(out, exist_ok=True)
    print("output to:  ", out)

    used, cols = gad.collect(args.gta, WORLD)
    print("placements:  %d (outside interiors, LOD stand-ins skipped)" % len(used))

    cells = {}
    total = dict(placements=0, objects=0, tris=0, walls=0, floors=0)
    lse = dict(placements=0, objects=0, tris=0)
    for n, inst in enumerate(used):
        if n % 5000 == 0:
            print("  %d / %d placements..." % (n, len(used)))
        x, y = inst["pos"][0], inst["pos"][1]
        key = (int(math.floor(x / CELL)), int(math.floor(y / CELL)))
        c = cells.setdefault(key, dict(placements=0, objects=0, tris=0, walls=0, floors=0, zmin=1e9, zmax=-1e9))
        c["placements"] += 1
        total["placements"] += 1
        in_lse = (LSE_BOX[0] - gad.MARGIN <= x <= LSE_BOX[2] + gad.MARGIN
                  and LSE_BOX[1] - gad.MARGIN <= y <= LSE_BOX[3] + gad.MARGIN)
        if in_lse:
            lse["placements"] += 1
        model = cols.get(inst["name"])
        if not model:
            continue
        verts, tris = gad.world_triangles(inst, model)
        if not tris:
            continue
        c["objects"] += 1
        total["objects"] += 1
        c["tris"] += len(tris)
        total["tris"] += len(tris)
        if in_lse:
            lse["objects"] += 1
            lse["tris"] += len(tris)
        for a, b, cc, _s in tris:
            A, B, C = verts[a], verts[b], verts[cc]
            u = [B[k] - A[k] for k in range(3)]
            v = [C[k] - A[k] for k in range(3)]
            nx = u[1] * v[2] - u[2] * v[1]
            ny = u[2] * v[0] - u[0] * v[2]
            nz = u[0] * v[1] - u[1] * v[0]
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            if not length:
                continue
            up = abs(nz / length)
            if up <= 0.01:
                c["walls"] += 1
                total["walls"] += 1
            elif up >= 0.9:
                c["floors"] += 1
                total["floors"] += 1
        zs = [p[2] for p in verts]
        c["zmin"] = min(c["zmin"], min(zs))
        c["zmax"] = max(c["zmax"], max(zs))

    with open(os.path.join(out, "world_stats.csv"), "w", newline="\n") as f:
        f.write("cell_x_min,cell_y_min,placements,objects,triangles,walls,floors,z_min,z_max\n")
        for (cx, cy), c in sorted(cells.items()):
            f.write("%.0f,%.0f,%d,%d,%d,%d,%d,%.1f,%.1f\n" % (
                cx * CELL, cy * CELL, c["placements"], c["objects"], c["tris"], c["walls"], c["floors"],
                c["zmin"] if c["objects"] else 0.0, c["zmax"] if c["objects"] else 0.0))

    busiest = sorted(cells.items(), key=lambda kv: -kv[1]["tris"])[:15]
    with open(os.path.join(out, "summary.txt"), "w", newline="\n") as f:
        f.write("whole map (box x %.0f..%.0f, y %.0f..%.0f), squares of %.0f m\n" % (
            WORLD[0], WORLD[2], WORLD[1], WORLD[3], CELL))
        f.write("placements %d, collision objects %d, triangles %d, vertical faces %d, flat faces %d\n" % (
            total["placements"], total["objects"], total["tris"], total["walls"], total["floors"]))
        f.write("squares with anything: %d\n" % len(cells))
        f.write("East LS test map area (with margin): placements %d, objects %d, triangles %d\n" % (
            lse["placements"], lse["objects"], lse["tris"]))
        f.write("busiest squares (x, y of the south-west corner: triangles, objects):\n")
        for (cx, cy), c in busiest:
            f.write("  %6.0f %6.0f: %7d triangles, %5d objects\n" % (cx * CELL, cy * CELL, c["tris"], c["objects"]))
    print("whole map:   %d collision objects, %d triangles (East LS area: %d objects, %d triangles)" % (
        total["objects"], total["tris"], lse["objects"], lse["tris"]))
    print("written:     world_stats.csv, summary.txt")
    print("done")


if __name__ == "__main__":
    sys.exit(main())
