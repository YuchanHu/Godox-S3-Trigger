import importlib.util
from pathlib import Path
import sys
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from godox_model import encode_id, frame, COMMANDS


class ProtocolTests(unittest.TestCase):
    def test_id_domain(self):
        self.assertEqual(encode_id(0), 0xc368)
        self.assertEqual(encode_id(99), 500)
        for bad in (-1, 100, 1.5):
            with self.assertRaises(ValueError):
                encode_id(bad)

    def test_golden_frames(self):
        for cmd in COMMANDS.values():
            raw, bits = frame(cmd)
            self.assertEqual(raw, bytes.fromhex("aa aa aa aa c3 68 c3 68 d5") + bytes([cmd]))
            self.assertEqual(len(bits), 81)
            self.assertEqual(bits[-1], 1)
            np.testing.assert_array_equal(bits[:32], [1, 0] * 16)

    def test_reference_all_ids_and_commands(self):
        source = ROOT / "third_party/godox-flash-radio/transmitter.py"
        if not source.exists():
            self.skipTest("reference checkout absent; see third_party/README.md")
        spec = importlib.util.spec_from_file_location("reference", source)
        ref = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ref)
        for netid in range(100):
            obj = ref.Command(netid)
            captured = []
            obj.rf_burst = lambda bits: captured.append(bits)
            for cmd in (ref.ShortCommand.FLASH_PREPARE, ref.ShortCommand.FLASH_TEST, ref.ShortCommand.FLASH_UNPREPARE):
                obj.short_command(cmd)
                raw, bits = frame(cmd.value, netid)
                np.testing.assert_array_equal(captured[-1], bits)
                self.assertEqual(np.packbits(captured[-1][:80]).tobytes(), raw)


if __name__ == "__main__":
    unittest.main()
