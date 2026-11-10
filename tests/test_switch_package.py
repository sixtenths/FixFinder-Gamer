"""Check that eMMC packages are linked for the protected local game slot."""

from pathlib import Path
from runpy import run_path
from struct import pack, unpack_from
import sys
import unittest
from unittest.mock import patch
from zlib import crc32


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
with patch.object(sys, "path", [str(SCRIPTS), *sys.path]):
    PACKER = run_path(str(SCRIPTS / "Prepare-SwitchPackage.py"))

BASE, LIMIT = PACKER["APP_BASE"], PACKER["APP_LIMIT"]
PACKAGE = PACKER["package"]


class SwitchPackageTest(unittest.TestCase):
    def test_package_records_the_padded_application_crc(self):
        source = pack("<II", 0x2002FFF8, BASE + 9) + b"abc"
        payload = source + b"\xff"
        result = PACKAGE("sonic", source)
        self.assertEqual(unpack_from("<4I", result), (PACKER["MAGIC"], 2, len(payload), crc32(payload)))
        self.assertEqual(result[16:], payload)

    def test_rejects_other_link_addresses_and_the_doom_map_region(self):
        for data in (
            pack("<II", 0x2002FFF8, 0x08008009) + b"\x00" * 4,
            pack("<II", 0x2002FFF8, BASE + 9).ljust(LIMIT - BASE + 1, b"\xff"),
        ):
            with self.subTest(size=len(data)), self.assertRaisesRegex(ValueError, "-SwitchApp"):
                PACKAGE("arcade", data)
