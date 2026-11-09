"""Exercise the public map-pack format without a game IWAD."""

from hashlib import sha256
from pathlib import Path
from runpy import run_path
from struct import iter_unpack, pack, unpack_from
import unittest
from unittest.mock import patch
from zlib import crc32


TOOLS = run_path(str(Path(__file__).resolve().parents[1] / "scripts/Prepare-SharewareWad.py"))
HEADER, ENTRY = TOOLS["HEADER"], TOOLS["ENTRY"]


def converted_wad(lumps):
    offset = HEADER.size + ENTRY.size * len(lumps)
    directory = bytearray()
    payload = bytearray()
    for name, data in lumps:
        directory += ENTRY.pack(offset + len(payload), len(data), name)
        payload += data
    return HEADER.pack(b"IWAD", len(lumps), HEADER.size) + directory + payload


def synthetic_lumps():
    yield b"PLAYPAL", b"shared graphics"
    for map_index, name in enumerate(TOOLS["MAP_NAMES"], start=1):
        yield name, b""
        for index, kind in enumerate(TOOLS["MAP_LUMPS"]):
            yield kind, bytes((map_index, index)) * (index + 1)


class EpisodePackTest(unittest.TestCase):
    def test_split_preserves_all_payloads_and_checksums(self):
        original = list(synthetic_lumps())
        assets, maps = TOOLS["split_episode_maps"](converted_wad(original))
        magic, count, start = HEADER.unpack_from(assets)
        self.assertEqual(magic, b"IWAD")
        self.assertEqual(count, len(original) + 1)
        records = list(ENTRY.iter_unpack(assets[start:start + count * ENTRY.size]))
        offset, size, name = records[-1]
        self.assertEqual(name, b"FFMAPTAB")
        self.assertEqual(assets[offset:offset + size], maps[:size])
        self.assertEqual(unpack_from("<8sI", maps), (b"FFDOOM1\0", 9))
        map_records = list(iter_unpack("<III", maps[12:size]))
        self.assertEqual(len(map_records), 9)
        cursor = size
        for start, length, checksum in map_records:
            self.assertEqual(start, cursor)
            self.assertEqual(crc32(maps[start:start + length]), checksum)
            cursor += length
        self.assertEqual(cursor, len(maps))

        map_index = -1
        external = TOOLS["EXTERNAL_MAP"]
        for (original_name, data), (offset, length, encoded_name) in zip(original, records):
            self.assertEqual(original_name, encoded_name.rstrip(b"\0"))
            self.assertEqual(length, len(data))
            if original_name in TOOLS["MAP_NAMES"]:
                map_index += 1
            elif map_index >= 0:
                self.assertTrue(offset & external)
                base, map_length, _ = map_records[map_index]
                relative = offset & ~external
                self.assertEqual(relative % 4, 0)
                self.assertLessEqual(relative + length, map_length)
                self.assertEqual(maps[base + relative:base + relative + length], data)
            else:
                self.assertEqual(assets[offset:offset + length], data)

    def test_split_rejects_a_map_larger_than_the_flash_slot(self):
        lumps = list(synthetic_lumps())
        lumps[2] = (b"THINGS", bytes(TOOLS["MAP_SLOT_SIZE"] + 1))
        with self.assertRaisesRegex(ValueError, "E1M1 exceeds the map slot"):
            TOOLS["split_episode_maps"](converted_wad(lumps))

    def test_reducer_preserves_texture_zero(self):
        patches = pack("<I8s", 1, b"PATCH")
        textures = pack("<III", 2, 12, 44)
        for name in (b"AASTINKY", b"SKY1"):
            textures += pack("<8sIhhIhhhhhh", name, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0)
        lumps = [(b"TEXTURE1", textures), (b"PNAMES", patches)]
        for name in TOOLS["MAP_NAMES"]:
            lumps.append((name, b""))
            lumps.extend((kind, b"") for kind in TOOLS["MAP_LUMPS"])
        raw = converted_wad(lumps)
        reduce_iwad = TOOLS["reduce_iwad"]
        with patch.dict(reduce_iwad.__globals__, SHAREWARE_19_SHA256=sha256(raw).hexdigest()):
            reduced = reduce_iwad(raw, 1)
        _, _, start = HEADER.unpack_from(reduced)
        offset, _, name = ENTRY.unpack_from(reduced, start)
        self.assertEqual(name, b"TEXTURE1")
        count, first, second = unpack_from("<III", reduced, offset)
        self.assertEqual(count, 2)
        self.assertEqual(reduced[offset + first:offset + first + 8], b"AASTINKY")
        self.assertEqual(reduced[offset + second:offset + second + 4], b"SKY1")
