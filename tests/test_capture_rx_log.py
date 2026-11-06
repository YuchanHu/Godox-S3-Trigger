import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from capture_rx_log import record_for


class FakeSerial:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.writes = []

    def write(self, data):
        self.writes.append(data)

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, count):
        return self.chunks.pop(0) if self.chunks else b""


class CaptureLogTests(unittest.TestCase):
    def test_split_cursor_query_gets_one_response(self):
        connection = FakeSerial([b"\x1b[6", b"n", b"RXIQ END"])
        record_for(connection, io.BytesIO(), 5, until_dump=True)
        self.assertEqual(connection.writes, [b"\x1b[1;80R"])

    def test_split_end_marker_and_raw_bytes_saved(self):
        chunks = [b"status\r\n", b"RXIQ BEGIN\n", b"a"*9000, b"RXIQ E", b"ND\r\n"]
        output = io.BytesIO()
        self.assertTrue(record_for(FakeSerial(chunks), output, 5, until_dump=True))
        self.assertEqual(output.getvalue(), b"".join(chunks))

    def test_error_is_saved_before_failure(self):
        output = io.BytesIO()
        with self.assertRaises(RuntimeError):
            record_for(FakeSerial([b"RXIQ unavailable: no scan\n"]), output, 5, until_dump=True)
        self.assertIn(b"no scan", output.getvalue())

    def test_timeout_retains_partial_log(self):
        output = io.BytesIO()
        with patch("capture_rx_log.time.monotonic", side_effect=[0, 0, 10]):
            with self.assertRaises(TimeoutError):
                record_for(FakeSerial([b"partial"]), output, 5, until_dump=True)
        self.assertEqual(output.getvalue(), b"partial")
