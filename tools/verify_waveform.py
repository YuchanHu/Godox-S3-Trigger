"""Run offline protocol and waveform verification, including optional reference comparisons."""
from pathlib import Path
import sys
import unittest

suite = unittest.defaultTestLoader.discover(str(Path(__file__).resolve().parents[1] / "tests"))
result = unittest.TextTestRunner(verbosity=2).run(suite)
sys.exit(not result.wasSuccessful())
