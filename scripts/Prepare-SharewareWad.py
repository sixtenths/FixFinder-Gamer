"""Reduce a local DOOM 1.9 shareware IWAD before GBADoom conversion."""

import argparse
import hashlib
from pathlib import Path
import re
import struct


HEADER = struct.Struct("<4sII")
ENTRY = struct.Struct("<II8s")
SHAREWARE_19_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
MAP_NAMES = tuple(f"E1M{number}".encode() for number in range(1, 10))
MAP_LUMPS = (
    b"THINGS", b"LINEDEFS", b"SIDEDEFS", b"VERTEXES", b"SEGS",
    b"SSECTORS", b"NODES", b"SECTORS", b"REJECT", b"BLOCKMAP",
)
# Texture animation intervals from the pinned engine's p_spec.c.
ANIMATIONS = (
    (b"BLODGR1", b"BLODGR4"), (b"SLADRIP1", b"SLADRIP3"),
    (b"BLODRIP1", b"BLODRIP4"), (b"FIREWALA", b"FIREWALL"),
    (b"GSTFONT1", b"GSTFONT3"), (b"FIRELAV3", b"FIRELAVA"),
    (b"FIREMAG1", b"FIREMAG3"), (b"FIREBLU1", b"FIREBLU2"),
    (b"ROCKRED1", b"ROCKRED3"), (b"BFALL1", b"BFALL4"),
    (b"SFALL1", b"SFALL4"), (b"WFALL1", b"WFALL4"), (b"DBRAIN1", b"DBRAIN4"),
)


def name8(raw):
    return raw.rstrip(b"\0")


def reduce_iwad(raw, map_count):
    if hashlib.sha256(raw).hexdigest() != SHAREWARE_19_SHA256:
        raise ValueError("Expected the original DOOM 1.9 shareware IWAD")
    magic, count, directory = HEADER.unpack_from(raw)
    lumps = [
        (name8(name), raw[offset:offset + size])
        for offset, size, name in ENTRY.iter_unpack(raw[directory:directory + count * ENTRY.size])
    ]
    selected = MAP_NAMES[:map_count]
    map_indexes = {name: i for i, (name, _) in enumerate(lumps) if name in MAP_NAMES}
    used_textures = {b"SKY1"}
    for name in selected:
        first = map_indexes[name] + 1
        map_lumps = lumps[first:first + len(MAP_LUMPS)]
        if tuple(name for name, _ in map_lumps) != MAP_LUMPS:
            raise ValueError(f"Unexpected map layout for {name.decode()}")
        for side in struct.iter_unpack("<hh8s8s8sh", map_lumps[2][1]):
            used_textures.update(name8(raw_name).upper() for raw_name in side[2:5])
    used_textures -= {b"", b"-"}
    for name in tuple(used_textures):
        if name.startswith((b"SW1", b"SW2")):
            other = b"2" if name[2:3] == b"1" else b"1"
            used_textures.add(name[:2] + other + name[3:])

    named = dict(lumps)
    texture_data = named[b"TEXTURE1"]
    texture_count, = struct.unpack_from("<I", texture_data)
    texture_offsets = struct.unpack_from(f"<{texture_count}I", texture_data, 4)
    textures = []
    for offset in texture_offsets:
        patch_count, = struct.unpack_from("<H", texture_data, offset + 20)
        textures.append((name8(texture_data[offset:offset + 8]),
                         bytearray(texture_data[offset:offset + 22 + patch_count * 10])))
    names = [name for name, _ in textures]
    for first, last in ANIMATIONS:
        if first in names and last in names:
            frames = names[names.index(first):names.index(last) + 1]
            if used_textures.intersection(frames):
                used_textures.update(frames)
    missing = used_textures - set(names)
    if missing:
        raise ValueError(f"Required textures missing: {sorted(missing)}")

    kept = [data for name, data in textures if name in used_textures]
    patch_locations = [(data, i) for data in kept for i in range(26, len(data), 10)]
    used_patch_ids = sorted({struct.unpack_from("<H", data, i)[0] for data, i in patch_locations})
    patch_ids = {old: new for new, old in enumerate(used_patch_ids)}
    for data, i in patch_locations:
        old, = struct.unpack_from("<H", data, i)
        struct.pack_into("<H", data, i, patch_ids[old])
    pnames = named[b"PNAMES"]
    patches = [pnames[4 + i * 8:12 + i * 8] for i in used_patch_ids]
    patch_names = {name8(name).upper() for name in patches}
    offset = 4 + 4 * len(kept)
    offsets = bytearray()
    for data in kept:
        offsets += struct.pack("<I", offset)
        offset += len(data)
    replacements = {
        b"PNAMES": struct.pack("<I", len(patches)) + b"".join(patches),
        b"TEXTURE1": struct.pack("<I", len(kept)) + offsets + b"".join(kept),
    }

    skipped = {
        i for name in MAP_NAMES[map_count:]
        for i in range(map_indexes[name], map_indexes[name] + len(MAP_LUMPS) + 1)
    }
    output = bytearray(HEADER.size)
    entries = []
    in_patches = False
    for i, (name, data) in enumerate(lumps):
        if name == b"P_START":
            in_patches = True
        if name == b"P_END":
            in_patches = False
        if i in skipped or name.startswith((b"DS", b"DP", b"D_", b"DEMO")):
            continue
        if name in (b"ENDOOM", b"GENMIDI", b"DMXGUS") or re.fullmatch(rb"WILV[12]\d", name):
            continue
        if in_patches and name.upper() not in patch_names and not name.endswith((b"_START", b"_END")):
            continue
        data = replacements.get(name, data)
        entries.append(ENTRY.pack(len(output), len(data), name))
        output += data
    HEADER.pack_into(output, 0, magic, len(entries), len(output))
    return output + b"".join(entries)


def deduplicate_converted(raw):
    magic, count, directory = HEADER.unpack_from(raw)
    data_start = directory + count * ENTRY.size
    if magic != b"IWAD" or directory != HEADER.size or data_start > len(raw):
        raise ValueError("Expected a WAD produced by GbaWadUtil")
    output = bytearray(raw[:data_start])
    locations = {}
    for i, (offset, size, name) in enumerate(ENTRY.iter_unpack(raw[directory:data_start])):
        if size == 0:
            continue
        if offset < data_start or offset + size > len(raw):
            raise ValueError(f"Lump outside WAD data: {name8(name)!r}")
        payload = raw[offset:offset + size]
        if payload not in locations:
            output += b"\0" * (-len(output) % 4)
            locations[payload] = len(output)
            output += payload
        ENTRY.pack_into(output, directory + i * ENTRY.size, locations[payload], size, name)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--maps", type=int, choices=range(1, 10))
    mode.add_argument("--deduplicate-converted", action="store_true",
                      help="Share identical lump bytes after GbaWadUtil conversion")
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error("Choose a separate output path for the reduced IWAD")
    try:
        raw = args.input.read_bytes()
        if args.deduplicate_converted:
            reduced = deduplicate_converted(raw)
            message = f"Deduplicated converted IWAD ({len(raw) - len(reduced):,} bytes saved)"
        else:
            reduced = reduce_iwad(raw, args.maps)
            message = f"Prepared E1M1 through E1M{args.maps} ({len(reduced):,} bytes before conversion)"
    except (ValueError, struct.error) as error:
        parser.error(str(error))
    args.output.write_bytes(reduced)
    print(message)


if __name__ == "__main__":
    main()
