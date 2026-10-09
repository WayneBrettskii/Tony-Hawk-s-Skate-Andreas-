"""my-passthrough: GTA San Andreas collision for one area, as a 3D file (phase 4-2b).
READ ONLY.

Reads, from the GTA install (nothing is changed there):
  - data\\gta.dat           which map files the game loads
  - the IDE files it lists  model number -> model name
  - the text IPL files      where each model is placed in the world
  - models\\gta3.img         the binary "stream" IPL files (most placements) and
                            the .col files (the collision shapes)
and writes, for the models placed inside an area, into
<project>/extracted/gta_<area>/ (gitignored):
  - collision_gta.obj      all their collision (boxes and triangle meshes;
                           spheres as small 8-sided shapes), in GTA world
                           coordinates (metres, Z up), one object per placement
  - placements.csv         every placement used: model, position, rotation, source
  - summary.txt            counts, models without collision, surface materials

Usage (from the project folder):
  python tools\\gta_area_dump.py "D:\\Games\\GTA San Andreas"            (area: lse)
  python tools\\gta_area_dump.py "D:\\Games\\GTA San Andreas" --area lse
  python tools\\gta_area_dump.py <gta folder> --box 1700 -1900 2250 -1150 --name myarea

Areas: lse = the Los Santos East map's playable part (x 1700..2250,
y -1900..-1150), from the 4-2a picture.

Formats (public, e.g. gta.fandom.com "Collision File"; GTA SA PC 1.0):
  IMG (VER2): "VER2", u32 count, then 32-byte entries: u32 offset in 2048-byte
       sectors, u16 size in sectors, u16 (unused), 24-byte name.
  COL archive: models back to back; each: 4-byte type ("COLL", "COL2",
       "COL3", "COL4"), u32 size of the rest, 22-byte name, u16 model id, then
       COL1: bounds (radius, centre, min, max), then counted lists: spheres
             (radius, centre, surface), 0, boxes (min, max, surface), vertices
             (3 floats), faces (3 x u32, surface)
       COL2+: bounds (min, max, centre, radius), u16 spheres, u16 boxes, u16
             faces, u8 wheels, u8 pad, u32 flags, offsets (from just after the
             type) of spheres, boxes, suspension lines, vertices, faces,
             planes; spheres (centre, radius, surface), boxes (min, max,
             surface), vertices (3 x int16 / 128), faces (3 x u16, u8
             material, u8 light). Surface = 4 bytes (material first).
  Binary IPL: "bnry", u32 instance count, ..., u32 offset of instances at
       byte 28; instances 40 bytes: position (3 floats), rotation (4 floats,
       x y z w), u32 model id, u32 interior, i32 lod.
  Text IPL "inst" lines: id, name, interior, x, y, z, rx, ry, rz, rw, lod.
  Rotation: GTA SA stores the conjugate quaternion (believed): the tool uses
       (-x, -y, -z, w). --quat-as-is uses it as stored. The 4-2b overlay with
       Los Santos East shows which is right.
"""

import argparse
import math
import os
import struct
import sys

AREAS = {"lse": (1700.0, -1900.0, 2250.0, -1150.0)}
MARGIN = 150.0  # also take models placed up to this far outside the area (big buildings)


# ---------------------------------------------------------------- install files

def find_case_insensitive(root, rel):
    """Windows paths in gta.dat are upper case; find the real file (also works on Linux)."""
    path = root
    for part in rel.replace("\\", "/").split("/"):
        if not part:
            continue
        try:
            names = os.listdir(path)
        except OSError:
            return None
        match = next((n for n in names if n.lower() == part.lower()), None)
        if match is None:
            return None
        path = os.path.join(path, match)
    return path


def read_gta_dat(gta):
    ides, ipls = [], []
    for line in open(find_case_insensitive(gta, "data/gta.dat"), encoding="latin-1"):
        parts = line.strip().split(None, 1)
        if len(parts) != 2 or parts[0].startswith("#"):
            continue
        if parts[0].upper() == "IDE":
            ides.append(parts[1].strip())
        elif parts[0].upper() == "IPL":
            ipls.append(parts[1].strip())
    return ides, ipls


def read_ides(gta, ides):
    names = {}
    for rel in ides:
        path = find_case_insensitive(gta, rel)
        if not path:
            continue
        section = None
        for line in open(path, encoding="latin-1"):
            s = line.split("#")[0].strip()
            if not s:
                continue
            low = s.lower()
            if low in ("objs", "tobj", "anim", "weap", "hier", "cars", "peds", "path", "2dfx", "txdp"):
                section = low
                continue
            if low == "end":
                section = None
                continue
            if section in ("objs", "tobj", "anim"):
                p = [x.strip() for x in s.split(",")]
                if len(p) >= 2 and p[0].lstrip("-").isdigit():
                    names[int(p[0])] = p[1].lower()
    return names


def read_text_ipls(gta, ipls):
    out = []
    per_file = {}
    for rel in ipls:
        if not rel.lower().endswith(".ipl"):
            continue
        path = find_case_insensitive(gta, rel)
        if not path:
            continue
        section = None
        for line in open(path, encoding="latin-1"):
            s = line.split("#")[0].strip()
            if not s:
                continue
            low = s.lower()
            if low in ("inst", "cull", "grge", "enex", "pick", "cars", "jump", "tcyc", "auzo", "mult", "occl", "zone", "path"):
                section = low
                continue
            if low == "end":
                section = None
                continue
            if section == "inst":
                p = [x.strip() for x in s.split(",")]
                if len(p) >= 11:
                    src = os.path.basename(path)
                    index = per_file.get(src, 0)
                    per_file[src] = index + 1
                    out.append(dict(id=int(p[0]), name=p[1].lower(), interior=int(p[2]),
                                    pos=tuple(float(x) for x in p[3:6]), rot=tuple(float(x) for x in p[6:10]),
                                    lod=int(p[10]), source=src, index=index))
    return out


# ---------------------------------------------------------------- IMG archive

class Img:
    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(8)
        if head[:4] != b"VER2":
            raise ValueError("%s is not a VER2 IMG archive" % path)
        count = struct.unpack_from("<I", head, 4)[0]
        table = self.f.read(count * 32)
        self.entries = []
        for i in range(count):
            off, size, _unused = struct.unpack_from("<IHH", table, i * 32)
            name = table[i * 32 + 8:i * 32 + 32].split(b"\0")[0].decode("latin-1")
            self.entries.append((name, off * 2048, size * 2048))

    def read(self, offset, size):
        self.f.seek(offset)
        return self.f.read(size)


# ---------------------------------------------------------------- binary IPL

def read_binary_ipl(blob, source):
    out = []
    if blob[:4] != b"bnry":
        return out
    count = struct.unpack_from("<I", blob, 4)[0]
    off = struct.unpack_from("<I", blob, 28)[0]
    for i in range(count):
        o = off + i * 40
        if o + 40 > len(blob):
            break
        x, y, z, rx, ry, rz, rw, mid, interior, lod = struct.unpack_from("<7fIIi", blob, o)
        out.append(dict(id=mid, name=None, interior=interior, pos=(x, y, z), rot=(rx, ry, rz, rw),
                        lod=lod, source=source))
    return out


# ---------------------------------------------------------------- collision

def parse_col_models(blob):
    """Yield (name, model) for every collision model in a .col archive."""
    pos = 0
    while pos + 8 <= len(blob):
        fourcc = blob[pos:pos + 4]
        if fourcc not in (b"COLL", b"COL2", b"COL3", b"COL4"):
            break
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        body = blob[pos + 4:pos + 8 + size]  # offsets in COL2+ count from here (after the type)
        name = blob[pos + 8:pos + 30].split(b"\0")[0].decode("latin-1").lower()
        try:
            model = parse_col1(blob, pos + 32) if fourcc == b"COLL" else parse_col2plus(body, fourcc)
        except (struct.error, IndexError):
            model = None
        yield name, model
        pos += 8 + size


def parse_col1(blob, p):
    p += 40  # bounds: radius, centre, min, max
    spheres, boxes, verts, faces = [], [], [], []
    n = struct.unpack_from("<I", blob, p)[0]; p += 4
    for _ in range(n):
        r, cx, cy, cz = struct.unpack_from("<4f", blob, p)
        spheres.append(((cx, cy, cz), r, blob[p + 16])); p += 20
    p += 4  # unknown count (0)
    n = struct.unpack_from("<I", blob, p)[0]; p += 4
    for _ in range(n):
        v = struct.unpack_from("<6f", blob, p)
        boxes.append((v[:3], v[3:], blob[p + 24])); p += 28
    n = struct.unpack_from("<I", blob, p)[0]; p += 4
    for _ in range(n):
        verts.append(struct.unpack_from("<3f", blob, p)); p += 12
    n = struct.unpack_from("<I", blob, p)[0]; p += 4
    for _ in range(n):
        a, b, c = struct.unpack_from("<3I", blob, p)
        faces.append((a, b, c, blob[p + 12])); p += 16
    return dict(spheres=spheres, boxes=boxes, verts=verts, faces=faces)


def parse_col2plus(body, fourcc):
    # body starts at the u32 size; the header fields follow name (22) + id (2).
    p = 4 + 24 + 40  # size, name+id, bounds (min, max, centre, radius)
    ns, nb, nf, _nw, _pad, _flags = struct.unpack_from("<HHHBBI", body, p); p += 12
    o_sph, o_box, _o_susp, o_vert, o_face, _o_plane = struct.unpack_from("<6I", body, p)
    spheres, boxes, faces = [], [], []
    for i in range(ns):
        o = o_sph + i * 20
        cx, cy, cz, r = struct.unpack_from("<4f", body, o)
        spheres.append(((cx, cy, cz), r, body[o + 16]))
    for i in range(nb):
        o = o_box + i * 28
        v = struct.unpack_from("<6f", body, o)
        boxes.append((v[:3], v[3:], body[o + 24]))
    maxv = -1
    for i in range(nf):
        o = o_face + i * 8
        a, b, c = struct.unpack_from("<3H", body, o)
        faces.append((a, b, c, body[o + 6]))
        maxv = max(maxv, a, b, c)
    verts = []
    for i in range(maxv + 1):
        x, y, z = struct.unpack_from("<3h", body, o_vert + i * 6)
        verts.append((x / 128.0, y / 128.0, z / 128.0))
    return dict(spheres=spheres, boxes=boxes, verts=verts, faces=faces)


# ---------------------------------------------------------------- maths

def quat_matrix(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
            (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
            (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))


def apply(m, t, v):
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + t[0],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + t[1],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + t[2])


# Corner order of the shapes we build ourselves (boxes, spheres): the same
# order as GTA's own mesh faces, so that all faces point outwards once
# gta_to_thaw_map.py writes them (it reverses every face for THAW). Until
# 2026-10-08 boxes and spheres had the opposite order: in the THAW map their
# faces pointed inwards, so the skater could pass into a tree's box and then
# not get out (user got stuck inside a tree at GTA 1966, -1360).

def box_triangles(mn, mx):
    c = [(x, y, z) for x in (mn[0], mx[0]) for y in (mn[1], mx[1]) for z in (mn[2], mx[2])]
    quads = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    tris = []
    for a, b, cc, d in quads:
        tris += [(a, cc, b), (a, d, cc)]
    return c, tris


def sphere_shape(centre, r):
    cx, cy, cz = centre
    v = [(cx + r, cy, cz), (cx - r, cy, cz), (cx, cy + r, cz), (cx, cy - r, cz), (cx, cy, cz + r), (cx, cy, cz - r)]
    t = [(0, 4, 2), (2, 4, 1), (1, 4, 3), (3, 4, 0), (2, 5, 0), (1, 5, 2), (3, 5, 1), (0, 5, 3)]
    return v, t


# ---------------------------------------------------------------- main

def collect(gta, box, log=print):
    """Placements in the area (+MARGIN) and the collision models they use.
    Returns (used, cols): used = list of placements (dicts), cols = name -> model."""
    ides, ipls = read_gta_dat(gta)
    names = read_ides(gta, ides)
    text_inst = read_text_ipls(gta, ipls)
    log("map files:   %d IDE, %d IPL listed; %d model names; %d text placements" % (
        len(ides), len(ipls), len(names), len(text_inst)))

    img_path = find_case_insensitive(gta, "models/gta3.img")
    img = Img(img_path)
    log("gta3.img:    %d files" % len(img.entries))

    # Binary (stream) placements.
    bin_inst = []
    for ename, off, size in img.entries:
        if ename.lower().endswith(".ipl"):
            bin_inst += read_binary_ipl(img.read(off, size), ename)
    for inst in bin_inst:
        inst["name"] = names.get(inst["id"])
    log("placements:  %d from stream IPLs in gta3.img" % len(bin_inst))

    # Far-away (LOD) models: a stream placement's "lod" number points at the
    # placement, in the matching text IPL (lae2_stream3.ipl -> LAe2.ipl), that
    # stands in for it from far away. Those, and text placements other text
    # placements point at, are skipped (they would double the collision).
    lod_refs = set()
    for inst in bin_inst:
        if inst["lod"] >= 0:
            base = inst["source"].lower().split("_stream")[0] + ".ipl"
            lod_refs.add((base, inst["lod"]))
    for inst in text_inst:
        if inst["lod"] >= 0:
            lod_refs.add((inst["source"].lower(), inst["lod"]))
    for inst in text_inst:
        inst["is_lod"] = (inst["source"].lower(), inst["index"]) in lod_refs
    log("far-away:    %d text placements are LOD stand-ins (skipped)" % sum(1 for i in text_inst if i["is_lod"]))
    def wanted(inst):
        x, y, _z = inst["pos"]
        if inst["interior"] not in (0, 13):
            return False
        if not inst["name"] or inst["name"].startswith("lod") or inst.get("is_lod"):
            return False
        return box[0] - MARGIN <= x <= box[2] + MARGIN and box[1] - MARGIN <= y <= box[3] + MARGIN

    used = [i for i in text_inst + bin_inst if wanted(i)]
    needed = set(i["name"] for i in used)
    log("in area:     %d placements of %d different models" % (len(used), len(needed)))

    # Collision for the needed models.
    cols = {}
    archives = 0
    for ename, off, size in img.entries:
        if not ename.lower().endswith(".col"):
            continue
        archives += 1
        for mname, model in parse_col_models(img.read(off, size)):
            if mname in needed and mname not in cols and model:
                cols[mname] = model
    log("collision:   %d .col archives read; %d of %d models found" % (archives, len(cols), len(needed)))
    return used, cols


def world_triangles(inst, model, quat_as_is=False):
    """A placement's collision in GTA world metres: (vertices, [(a, b, c, surface)])."""
    qx, qy, qz, qw = inst["rot"]
    q = (qx, qy, qz, qw) if quat_as_is else (-qx, -qy, -qz, qw)
    m = quat_matrix(q)
    t = inst["pos"]
    verts, tris = [], []
    for mn, mx, surf in model["boxes"]:
        v, tr = box_triangles(mn, mx)
        tris += [(a + len(verts), b + len(verts), c + len(verts), surf) for a, b, c in tr]
        verts += v
    for centre, r, surf in model["spheres"]:
        v, tr = sphere_shape(centre, r)
        tris += [(a + len(verts), b + len(verts), c + len(verts), surf) for a, b, c in tr]
        verts += v
    off0 = len(verts)
    verts += model["verts"]
    tris += [(a + off0, b + off0, c + off0, s) for a, b, c, s in model["faces"]]
    return [apply(m, t, v) for v in verts], tris


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    project = os.path.dirname(here)
    ap = argparse.ArgumentParser(description="GTA SA collision for one area as a 3D file (read only).")
    ap.add_argument("gta", help="GTA San Andreas folder")
    ap.add_argument("--area", choices=sorted(AREAS), default="lse")
    ap.add_argument("--box", nargs=4, type=float, metavar=("XMIN", "YMIN", "XMAX", "YMAX"))
    ap.add_argument("--name", help="output name (default: the area name)")
    ap.add_argument("--quat-as-is", action="store_true", help="use IPL rotations as stored (not conjugated)")
    ap.add_argument("--out", help="output folder (default <project>/extracted/gta_<name>)")
    args = ap.parse_args()

    box = tuple(args.box) if args.box else AREAS[args.area]
    name = args.name or (args.area if not args.box else "box")
    out = args.out or os.path.join(project, "extracted", "gta_" + name)
    os.makedirs(out, exist_ok=True)
    print("GTA folder: ", args.gta)
    print("area:        x %.0f..%.0f, y %.0f..%.0f (+%.0f m margin)" % (box[0], box[2], box[1], box[3], MARGIN))
    print("output to:  ", out)

    used, cols = collect(args.gta, box)
    needed = set(i["name"] for i in used)

    surfaces = {}
    tri_count = 0
    with open(os.path.join(out, "collision_gta.obj"), "w", newline="\n") as fo, \
            open(os.path.join(out, "placements.csv"), "w", newline="\n") as fc:
        fo.write("# GTA SA collision, area x %.0f..%.0f y %.0f..%.0f, GTA world metres (Z up)\n" % (
            box[0], box[2], box[1], box[3]))
        fc.write("model,x,y,z,qx,qy,qz,qw,source,has_collision\n")
        base = 1
        for k, inst in enumerate(used):
            model = cols.get(inst["name"])
            qx, qy, qz, qw = inst["rot"]
            fc.write("%s,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%s,%s\n" % (
                inst["name"], inst["pos"][0], inst["pos"][1], inst["pos"][2], qx, qy, qz, qw,
                inst["source"], "yes" if model else "no"))
            if not model:
                continue
            fo.write("o %s_%d\n" % (inst["name"], k))
            verts, tris = world_triangles(inst, model, args.quat_as_is)
            for v in verts:
                fo.write("v %.4f %.4f %.4f\n" % v)
            for a, b, c, s in tris:
                fo.write("f %d %d %d\n" % (base + a, base + b, base + c))
                surfaces[s] = surfaces.get(s, 0) + 1
            tri_count += len(tris)
            base += len(verts)

    missing = sorted(n for n in needed if n not in cols)
    with open(os.path.join(out, "summary.txt"), "w", newline="\n") as fs:
        fs.write("area x %.0f..%.0f y %.0f..%.0f margin %.0f\n" % (box[0], box[2], box[1], box[3], MARGIN))
        fs.write("placements used: %d, models: %d, with collision: %d, triangles: %d\n" % (
            len(used), len(needed), len(cols), tri_count))
        fs.write("rotation: %s\n" % ("as stored" if args.quat_as_is else "conjugated (-x, -y, -z, w)"))
        fs.write("surface materials (id: triangles):\n")
        for s in sorted(surfaces):
            fs.write("  %3d: %d\n" % (s, surfaces[s]))
        fs.write("models without collision (%d):\n" % len(missing))
        for n in missing:
            fs.write("  %s\n" % n)
    print("written:     collision_gta.obj (%d triangles), placements.csv, summary.txt" % tri_count)
    print("done")


if __name__ == "__main__":
    sys.exit(main())
