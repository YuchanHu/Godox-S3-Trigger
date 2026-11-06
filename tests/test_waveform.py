import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from godox_model import *


class WaveformTests(unittest.TestCase):
    def test_pack_boundaries(self):
        grid = np.arange(-512, 512)
        np.testing.assert_array_equal(unpack(pack(grid, -grid - 1)), grid + 1j * (-grid - 1))
        for i, q in ((512, 0), (-513, 0), (0, 512), (0, -513)):
            with self.assertRaises(ValueError): pack([i], [q])
        with self.assertRaises(ValueError): unpack([1 << 20])

    def test_files_and_reproducibility(self):
        directory = ROOT / "components/godox_waveform/waves"
        manifest = json.loads((directory / "manifest.json").read_text())
        for name, cmd in COMMANDS.items():
            data = (directory / (name + ".bin")).read_bytes()
            self.assertEqual(data, waveform(cmd).tobytes())
            self.assertEqual(hashlib.sha256(data).hexdigest(), manifest["waves"][name]["sha256"])
            words = np.frombuffer(data, dtype="<u4")
            self.assertEqual(len(words), COUNT)
            self.assertLessEqual(len(words), 16128)
            self.assertEqual(len(words) % 256, 0)
            self.assertTrue(np.all(words >> 20 == 0))
            self.assertTrue(np.all(words[:GUARD] == 0))
            self.assertTrue(np.all(words[GUARD + 13560:] == 0))
            self.assertAlmostEqual(len(words) / FS * 1e6, 403.2)

    def test_phase_accumulation_and_polarity(self):
        for bit, sign in ((0, 1), (1, -1)):
            _, phase = base_wave(np.full(81, bit))
            freq = np.diff(phase) * BASE_FS / (2 * np.pi)
            self.assertLess(np.max(np.abs(np.diff(phase))), np.pi)
            self.assertTrue(np.all(freq * sign >= -1e-8))
            # RZ pulse peak is approx 200 kHz, lower between impulses.
            self.assertGreater(np.max(np.abs(freq)), 160000)
            self.assertLess(np.max(np.abs(freq)), 210000)

    def test_resampling(self):
        base, _ = base_wave(frame(0x09)[1])
        up = interpolate(base)
        self.assertEqual(len(base), 678)
        self.assertEqual(len(up), 13560)
        np.testing.assert_allclose(up[::20], base, atol=1e-12)

    def test_rf_offset_and_bit_recovery(self):
        # Recover every bit from the actual quantized 40MS/s resource, after
        # removing the intended carrier; excludes filter edge transients.
        for cmd in COMMANDS.values():
            bits = frame(cmd)[1]
            z = unpack(waveform(cmd))[GUARD:GUARD + 13560]
            t = np.arange(len(z)) / FS
            mixed_down = z * np.exp(-2j * np.pi * OFFSET * t)
            freq = np.angle(mixed_down[1:] * mixed_down[:-1].conj()) * FS / (2 * np.pi)
            # Pulse centre: 8 input samples + FIR delay of 7 input samples.
            centres = (np.arange(81) * 8 + 15) * 20
            recovered = np.array([np.mean(freq[c-15:c+15]) < 0 for c in centres])
            np.testing.assert_array_equal(recovered, bits)
            raw_freq = np.angle(z[1:] * z[:-1].conj()) * FS / (2 * np.pi)
            self.assertAlmostEqual(float(np.mean(raw_freq[600:-600] - freq[600:-600])), OFFSET, delta=1)
            # Unwanted image around -2.5 MHz must be well below intended band.
            spec = np.abs(np.fft.fft(z, 65536)) ** 2
            hz = np.fft.fftfreq(len(spec), 1 / FS)
            wanted = spec[np.abs(hz - OFFSET) < 500000].sum()
            image = spec[np.abs(hz + OFFSET) < 500000].sum()
            self.assertGreater(10 * np.log10(wanted / image), 40)

    def test_reference_baseband(self):
        path = ROOT / "third_party/godox-flash-radio/transmitter.py"
        if not path.exists(): self.skipTest("reference checkout absent")
        spec = importlib.util.spec_from_file_location("reference_phy", path)
        ref = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ref)
        for cmd in COMMANDS.values():
            bits = frame(cmd)[1]
            out = io.BytesIO()
            class Stdout:
                buffer = out
            with patch.object(ref.sys, "stdout", Stdout()):
                ref.Command().rf_burst(bits.tolist())
            native, _ = base_wave(bits)
            ours = np.rint(native.astype("complex64").view("float32") * 127).astype("int8")
            np.testing.assert_array_equal(ours, np.frombuffer(out.getvalue(), dtype="int8"))


if __name__ == "__main__": unittest.main()
