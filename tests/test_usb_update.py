"""Check image boundaries and USB device selection before any flash writes."""

from pathlib import Path
from runpy import run_path
from struct import pack, pack_into
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
with patch.object(sys, "path", [str(ROOT / "scripts"), *sys.path]):
    from fixfinder_image import GAME_BASE, LOADER_PID, MANIFEST_MAGIC, VID, combine_image, split_usb_image
    INSTALLER = run_path(str(ROOT / "scripts/Install-UsbGame.py"))

CONNECT = INSTALLER["connect"]
VIDEO_PID = INSTALLER["VIDEO_GAME_PID"]


class UsbImageTest(unittest.TestCase):
    def setUp(self):
        self.loader = (ROOT / "downloads/FixFinder-USB-Loader.bin").read_bytes()
        self.game = pack("<II", 0x2002FFF8, GAME_BASE + 9) + bytes(7)
        self.padded_game = self.game + b"\xff"

    def test_combined_returns_only_the_application(self):
        packed = combine_image(self.loader, self.game)
        self.assertEqual(split_usb_image(packed), (self.padded_game, True))

    def test_raw_image_can_contain_manifest_magic(self):
        game = bytearray(self.game.ljust(0x8100, b"\xff"))
        pack_into("<I", game, 0x4000, MANIFEST_MAGIC)
        self.assertEqual(split_usb_image(bytes(game)), (game, False))

    def test_bad_manifest_or_game_is_rejected(self):
        packed = combine_image(self.loader, self.game)
        for offset in (0x4000, 0x4004, 0x4008, 0x400C, len(packed) - 1):
            with self.subTest(offset=offset):
                broken = bytearray(packed)
                broken[offset] ^= 1
                with self.assertRaises(ValueError):
                    split_usb_image(bytes(broken))

    def test_standalone_loader_cannot_be_sent_as_application(self):
        with self.assertRaisesRegex(ValueError, "USB game slot"):
            split_usb_image(self.loader)


class UsbTargetTest(unittest.TestCase):
    def test_auto_selection_survives_reset_without_switching_devices(self):
        hid = Mock()
        hid.interfaces.side_effect = [
            [(VIDEO_PID, "A", "game-a")],
            [(LOADER_PID, "B", "loader-b")],
            [(LOADER_PID, "B", "loader-b"), (LOADER_PID, "A", "loader-a")],
        ]
        game, loader = Mock(), Mock()
        hid.Device.side_effect = (game, loader)
        with patch.object(INSTALLER["time"], "sleep"):
            self.assertEqual(CONNECT(hid, None, 1), (loader, "A"))
        self.assertEqual([call.args[0] for call in hid.Device.call_args_list], ["game-a", "loader-a"])
        self.assertEqual(hid.interfaces.call_args.args[0], VID)
        game.write.assert_called_once_with(b"\x14\xb7")
        game.close.assert_called_once()

    def test_two_scanners_require_a_serial(self):
        hid = Mock()
        hid.interfaces.return_value = [(VIDEO_PID, "A", "game-a"), (LOADER_PID, "B", "loader-b")]
        with self.assertRaisesRegex(ValueError, "--serial"):
            CONNECT(hid, None, 1)
        hid.Device.assert_not_called()
        self.assertEqual(CONNECT(hid, "B", 1), (hid.Device.return_value, "B"))


if __name__ == "__main__":
    unittest.main()
