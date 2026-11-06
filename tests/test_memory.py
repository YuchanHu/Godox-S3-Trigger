from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_elf import check


class MemoryTests(unittest.TestCase):
    def run_elf(self, addr, size):
        blob = bytearray(84)
        blob[:6] = b"\x7fELF\x01\x01"
        struct.pack_into("<I", blob, 28, 52)
        struct.pack_into("<HH", blob, 42, 32, 1)
        struct.pack_into("<8I", blob, 52, 1, 0, addr, addr, 0, size, 6, 4)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.elf"
            path.write_bytes(blob)
            return check(path)

    def test_dram_overlap_rejected(self):
        with self.assertRaisesRegex(ValueError, "RF SRAM overlap"):
            self.run_elf(0x3fccfff0, 32)

    def test_iram_alias_rejected(self):
        with self.assertRaisesRegex(ValueError, "RF SRAM overlap"):
            self.run_elf(0x403c0000, 4)

    def test_boundaries_accepted(self):
        self.assertFalse(self.run_elf(0x3fcc0000, 65536)["rf_sram_overlap"])
        self.assertFalse(self.run_elf(0x403d0000, 16)["rf_sram_overlap"])
