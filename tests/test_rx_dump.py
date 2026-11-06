import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from decode_rx_dump import parse_dump


class DumpTests(unittest.TestCase):
    def test_40msps_verification_dump(self):
        lo,score,words=parse_dump(self.log().replace('rate=16000000','rate=40000000'))
        self.assertEqual((lo,score,len(words)),(2422,28,16128))
    def log(self):
        lines = ["RXIQ BEGIN rate=16000000 count=16128 lo=2422 score=28"]
        for i in range(0, 16128, 32):
            lines.append(f"RXIQ {i:05} " + "".join(f"{j:05x}" for j in range(i, i+32)))
        lines.append("RXIQ END")
        return "\n".join(lines)

    def test_complete_dump_with_interleaved_panel_log(self):
        text = self.log().replace("RXIQ 00032", "I (123) panel: battery\nRXIQ 00032")
        lo, score, words = parse_dump(text)
        self.assertEqual((lo, score), (2422, 28))
        self.assertEqual(words, list(range(16128)))

    def test_missing_rows_or_end_rejected(self):
        for text in [self.log().replace("RXIQ END", ""),
                     self.log().replace("RXIQ 00032", "RXIQ 00064"),
                     self.log().replace("lo=2422", "lo=2400")]:
            with self.assertRaises(ValueError):
                parse_dump(text)
