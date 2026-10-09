"""my-passthrough: look inside a reTHAWed user map (phase 4-2a). READ ONLY.

Opens a reTHAWed map's .pak.wpc, lists the files inside it, and writes:
  - pak_index.txt   every file in the PAK: type, size, name
  - collision.obj   the map's collision (what the skater can touch) as a 3D
                    file any 3D viewer opens (Blender, Windows 3D Viewer, ...)
  - collision_flags.txt  how many triangles carry each collision flag
  - rails.obj       the rails (RailNode links) as lines
  - rails.csv       every rail node: index, name, position, links, terrain

Nothing is written next to the game: everything goes to the output folder
(default: <project>/extracted/<level prefix>/, which git ignores). The game
files are only read (AGENTS rules 1 and 4).

Usage (Windows, from the project folder):
  python tools\\thaw_level_dump.py "D:\\Games\\reTHAWed\\data\\mod\\UserMods\\Maps\\Los Santos East"
  python tools\\thaw_level_dump.py <map folder or .pak.wpc> [--out <folder>] [--gta lse]

--gta lse : also write collision_gta.obj / rails_gta.obj in GTA San Andreas
            world coordinates (metres, Z up), using the Los Santos East
            alignment found in phase 4-0 (src/common/convert.h). Only correct
            for that map.

Format notes (worked out from the Los Santos East map, 2026-10-08; see
docs/DESIGN.md 8.9 / MODLOG):
  PAK: 32-byte entries (type checksum, offset relative to the entry, size,
       ?, full-name checksum, short-name checksum, parent, flags); flags 0x20
       = a 160-byte file name follows. Ends with type ".last".
       Types are checksums of the extension (seen: .col .geom .tex .nqb .qb
       .mdl .stex; the end marker is "last" without the dot).
  Collision (.col), version 10, little endian:
       0x00 header: version, object count, vertex count, face count, ...
       0x30 overall box (min xyzw, max xyzw)
       0x50 objects, 64 bytes each: name checksum, u16 flags, u16 vertex
            count, u16 face count, u8, u8, first face byte offset, box min
            (xyzw), box max (xyzw), first vertex byte offset, BSP offset,
            first light byte offset, padding
       then: vertices (3 floats), one light byte per vertex, faces (10 bytes:
            u16 flags, u16 terrain, 3 x u16 vertex index within the object),
            then the BSP trees (not read).
  Nodes (.nqb, a compiled script): the node list is an array item named
       <PREFIX>_NodeArray; each element is a struct of linked items
       (u32 type info, u32 name checksum, u32 value or pointer, u32 next).
       Types seen: 0x0D vector (pointer to: u32, 3 floats), 0x1B checksum,
       0x19 array (pointer to: u32 info, u32 count, then the value inline if
       count is 1, else a pointer to the values), 0x01 integer.
"""

import argparse
import os
import struct
import sys


def qbkey(text):
    """The checksum THPS games use for names (CRC-32, lower case, no final xor)."""
    c = 0xFFFFFFFF
    for b in text.lower().encode("ascii"):
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
    return c


# Entry types are checksums of the extension, with or without the dot.
TYPE_NAMES = {}
for _e in [".col", ".geom", ".tex", ".nqb", ".qb", ".mdl", ".stex", ".last", ".scn", ".img", ".ska", ".ske"]:
    TYPE_NAMES[qbkey(_e)] = _e
    TYPE_NAMES[qbkey(_e[1:])] = _e
KEY = {qbkey(n): n for n in ["Pos", "Angles", "Name", "Class", "Type", "TerrainType", "Links",
                             "RailNode", "CreatedAtStart", "TrickObject", "Cluster"]}

# Collision face flags, as named in THPS (believed, not confirmed for THAW).
FACE_FLAGS = ["skatable", "not skatable", "wall ridable", "vert", "non collidable", "decal",
              "trigger", "camera collidable", "no skater shadow", "skater shadow",
              "no skater shadow wall", "under ok", "invisible", "flag 0x2000", "flag 0x4000",
              "flag 0x8000"]

# Phase 4-0 alignment of Los Santos East with GTA SA (src/common/convert.h).
LSE_ALIGN = dict(scale=0.02422967, ysign=-1.0, offset=(1789.2105, -1397.8612, 24.9750))


def to_gta(p, a):
    """THAW level position (x, y up, z) -> GTA SA world (x, y, z up), metres."""
    x, y, z = p
    s = a["scale"]
    ox, oy, oz = a["offset"]
    return (ox + x * s, oy + z * s * a["ysign"], oz + y * s)


# ---------------------------------------------------------------- PAK

def read_pak(path):
    data = open(path, "rb").read()
    entries = []
    pos = 0
    while pos + 32 <= len(data):
        t, off, size, _a, full, short, parent, flags = struct.unpack_from("<8I", data, pos)
        name = ""
        step = 32
        if flags & 0x20:
            name = data[pos + 32:pos + 32 + 160].split(b"\0")[0].decode("latin-1")
            step += 160
        if TYPE_NAMES.get(t) == ".last":
            break
        start = pos + off
        if start + size > len(data):
            raise ValueError("PAK entry at 0x%X points past the end of the file" % pos)
        entries.append(dict(type=TYPE_NAMES.get(t, "0x%08X" % t), start=start, size=size,
                            full=full, short=short, name=name))
        pos += step
    return data, entries


# ---------------------------------------------------------------- collision

def read_collision(blob):
    version, nobj, nverts, nfaces = struct.unpack_from("<4I", blob, 0)
    if version != 10:
        raise ValueError("collision version %d, only 10 is known" % version)
    objects = []
    for k in range(nobj):
        o = 0x50 + k * 64
        name, _flags, nv, nf, _s, _f, face_off = struct.unpack_from("<IHHHBBI", blob, o)
        vert_off = struct.unpack_from("<I", blob, o + 48)[0]
        objects.append(dict(name=name, nv=nv, nf=nf, face_off=face_off, vert_off=vert_off))
    vbase = 0x50 + nobj * 64
    lbase = vbase + nverts * 12
    fbase = (lbase + nverts + 3) & ~3
    return dict(version=version, objects=objects, vbase=vbase, fbase=fbase, nverts=nverts, nfaces=nfaces)


def write_collision(blob, col, out_path, transform=None, flag_counts=None):
    with open(out_path, "w", newline="\n") as f:
        f.write("# collision from a reTHAWed map (my-passthrough tools/thaw_level_dump.py)\n")
        base_index = 1
        for obj in col["objects"]:
            if obj["nv"] == 0:
                continue
            f.write("o obj_%08x\n" % obj["name"])
            v0 = col["vbase"] + obj["vert_off"]
            for i in range(obj["nv"]):
                p = struct.unpack_from("<3f", blob, v0 + i * 12)
                if transform:
                    p = transform(p)
                f.write("v %.4f %.4f %.4f\n" % p)
            f0 = col["fbase"] + obj["face_off"]
            for i in range(obj["nf"]):
                flags, _terrain, a, b, c = struct.unpack_from("<5H", blob, f0 + i * 10)
                if flag_counts is not None:
                    for bit in range(16):
                        if flags & (1 << bit):
                            flag_counts[bit] += 1
                f.write("f %d %d %d\n" % (base_index + a, base_index + b, base_index + c))
            base_index += obj["nv"]


# ---------------------------------------------------------------- nodes

def read_value(qb, info, value):
    t = (info >> 8) & 0xFF
    if t == 0x0D:  # vector: u32, 3 floats
        return struct.unpack_from("<3f", qb, value + 4)
    if t == 0x19:  # array
        _ainfo, count = struct.unpack_from("<2I", qb, value)
        if count == 0:
            return []
        if count == 1:
            return [struct.unpack_from("<I", qb, value + 8)[0]]
        ptr = struct.unpack_from("<I", qb, value + 8)[0]
        return list(struct.unpack_from("<%dI" % count, qb, ptr))
    return value  # integers, checksums: the value itself


def read_nodes(qb, prefix):
    key = struct.pack("<I", qbkey(prefix + "_NodeArray"))
    at = qb.find(key)
    if at < 0:
        raise ValueError(prefix + "_NodeArray not found")
    item = at - 4  # info, id, pak, pointer
    ptr = struct.unpack_from("<I", qb, item + 12)[0]
    _ainfo, count, list_ptr = struct.unpack_from("<3I", qb, ptr)
    nodes = []
    for i in range(count):
        sptr = struct.unpack_from("<I", qb, list_ptr + i * 4)[0]
        _sinfo, it = struct.unpack_from("<2I", qb, sptr)
        node = {}
        guard = 0
        while it and guard < 200:
            info, name, value, nxt = struct.unpack_from("<4I", qb, it)
            node[KEY.get(name, "0x%08x" % name) if name else "flag_%08x" % value] = read_value(qb, info, value)
            it = nxt
            guard += 1
        nodes.append(node)
    return nodes


def write_rails(nodes, obj_path, csv_path, transform=None):
    rail_class = qbkey("RailNode")
    segments = 0
    with open(obj_path, "w", newline="\n") as fo:
        fo.write("# rails from a reTHAWed map (lines between linked RailNodes)\n")
        for i, n in enumerate(nodes):
            p = n.get("Pos", (0.0, 0.0, 0.0))
            if transform:
                p = transform(p)
            fo.write("v %.4f %.4f %.4f\n" % p)
        for i, n in enumerate(nodes):
            if n.get("Class") != rail_class:
                continue
            for link in n.get("Links", []):
                # Only rail -> rail links: rail nodes also link to other node
                # kinds (seen as long stray lines in the first picture).
                if isinstance(link, int) and 0 <= link < len(nodes) and nodes[link].get("Class") == rail_class:
                    fo.write("l %d %d\n" % (i + 1, link + 1))
                    segments += 1
    if csv_path:
        with open(csv_path, "w", newline="\n") as fc:
            fc.write("index,name,x,y,z,links,terrain\n")
            for i, n in enumerate(nodes):
                if n.get("Class") != rail_class:
                    continue
                p = n.get("Pos", (0.0, 0.0, 0.0))
                links = " ".join(str(x) for x in n.get("Links", []))
                fc.write("%d,0x%08x,%.2f,%.2f,%.2f,%s,0x%08x\n" % (
                    i, n.get("Name", 0), p[0], p[1], p[2], links, n.get("TerrainType", 0)))
    return segments


# ---------------------------------------------------------------- main

def find_pak(path):
    if os.path.isfile(path):
        return path, None
    ini = os.path.join(path, "level.ini")
    prefix = None
    if os.path.isfile(ini):
        for line in open(ini, encoding="latin-1"):
            if line.strip().lower().startswith("levelprefix="):
                prefix = line.split("=", 1)[1].strip()
    if prefix and os.path.isfile(os.path.join(path, prefix + ".pak.wpc")):
        return os.path.join(path, prefix + ".pak.wpc"), prefix
    for name in os.listdir(path):
        low = name.lower()
        if low.endswith(".pak.wpc") and "_sfx" not in low and "_sky" not in low:
            return os.path.join(path, name), name.split(".")[0]
    raise FileNotFoundError("no level .pak.wpc in " + path)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    project = os.path.dirname(here)
    ap = argparse.ArgumentParser(description="Look inside a reTHAWed user map (read only).")
    ap.add_argument("map", help="map folder (with level.ini) or a .pak.wpc file")
    ap.add_argument("--out", help="output folder (default: <project>/extracted/<prefix>)")
    ap.add_argument("--gta", choices=["lse"], help="also write files in GTA SA coordinates")
    args = ap.parse_args()

    pak_path, prefix = find_pak(args.map)
    if not prefix:
        prefix = os.path.basename(pak_path).split(".")[0]
    out = args.out or os.path.join(project, "extracted", prefix)
    os.makedirs(out, exist_ok=True)
    print("map file:   ", pak_path)
    print("output to:  ", out)

    data, entries = read_pak(pak_path)
    with open(os.path.join(out, "pak_index.txt"), "w", newline="\n") as f:
        for e in entries:
            f.write("%-8s %10d bytes  at 0x%08X  %s\n" % (e["type"], e["size"], e["start"], e["name"]))
    print("PAK:         %d files" % len(entries))
    for e in entries:
        print("   %-8s %10d bytes  %s" % (e["type"], e["size"], e["name"]))

    transform = None
    if args.gta == "lse":
        transform = lambda p: to_gta(p, LSE_ALIGN)  # noqa: E731

    col_entry = next((e for e in entries if e["type"] == ".col"), None)
    if col_entry:
        blob = data[col_entry["start"]:col_entry["start"] + col_entry["size"]]
        col = read_collision(blob)
        flags = [0] * 16
        write_collision(blob, col, os.path.join(out, "collision.obj"), None, flags)
        if transform:
            write_collision(blob, col, os.path.join(out, "collision_gta.obj"), transform)
        with open(os.path.join(out, "collision_flags.txt"), "w", newline="\n") as f:
            f.write("triangles: %d\n" % col["nfaces"])
            for bit in range(16):
                f.write("%-24s %8d\n" % (FACE_FLAGS[bit], flags[bit]))
        print("collision:   version %d, %d objects, %d corners, %d triangles -> collision.obj" % (
            col["version"], len(col["objects"]), col["nverts"], col["nfaces"]))
        for bit in range(16):
            if flags[bit]:
                print("   %-24s %8d triangles" % (FACE_FLAGS[bit], flags[bit]))
    else:
        print("collision:   none found")

    nqb_entry = next((e for e in entries if e["type"] == ".nqb"), None)
    if nqb_entry:
        qb = data[nqb_entry["start"]:nqb_entry["start"] + nqb_entry["size"]]
        nodes = read_nodes(qb, prefix)
        rails = sum(1 for n in nodes if n.get("Class") == qbkey("RailNode"))
        segs = write_rails(nodes, os.path.join(out, "rails.obj"), os.path.join(out, "rails.csv"))
        if transform:
            write_rails(nodes, os.path.join(out, "rails_gta.obj"), None, transform)
        print("nodes:       %d, of which %d rail nodes, %d rail segments -> rails.obj, rails.csv" % (
            len(nodes), rails, segs))
    else:
        print("nodes:       none found")
    print("done")


if __name__ == "__main__":
    sys.exit(main())
