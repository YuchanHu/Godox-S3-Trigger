"""Save an automatic Godox scan and its complete IQ dump directly to a file."""
import argparse
from datetime import datetime
from pathlib import Path
import sys
import time

import serial
from serial.tools import list_ports


def select_port(requested):
    if requested:
        return requested
    ports = list(list_ports.comports())
    candidates = [p for p in ports if p.vid == 0x303A]
    if len(candidates) == 1:
        return candidates[0].device
    available = ", ".join(f"{p.device} ({p.description})" for p in ports) or "none"
    raise ValueError(f"Specify --port COMx. Available serial ports: {available}")


def record_for(connection, output, seconds, until_dump=False):
    deadline = time.monotonic() + seconds
    tail = b""
    terminal_tail = b""
    while time.monotonic() < deadline:
        data = connection.read(connection.in_waiting or 1)
        if not data:
            continue
        output.write(data)
        output.flush()
        # ESP-IDF linenoise asks the terminal for its cursor position. Without
        # a reply it can consume command bytes while waiting for the terminal.
        terminal_data = terminal_tail + data
        for _ in range(terminal_data.count(b"\x1b[6n")):
            connection.write(b"\x1b[1;80R")
        terminal_tail = terminal_data[-3:]
        tail = (tail + data)[-4096:]
        if until_dump and b"RXIQ END" in tail:
            return True
        if until_dump and (b"RXIQ unavailable:" in tail or b"RXIQ export:" in tail):
            raise RuntimeError("Device refused IQ export. The reason is saved in the log.")
    if until_dump:
        raise TimeoutError("Timed out waiting for RXIQ END. Partial log has been saved.")
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="COM port; auto-selects a single Espressif USB device")
    parser.add_argument("--seconds", type=int, default=60, help="Automatic scan duration, 1..120")
    parser.add_argument("--dump-only", action="store_true", help="Export the existing retained IQ")
    parser.add_argument("--channel", type=int, default=0, choices=range(33),
                        help="Diagnostic fixed CH1..32; default0 scans all channels")
    parser.add_argument("--output", type=Path, help="New log file; defaults to artifacts/rx-logs")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 120:
        parser.error("--seconds must be 1..120")
    port = select_port(args.port)
    path = args.output or Path(__file__).resolve().parents[1]/"artifacts"/"rx-logs"/(
        datetime.now().strftime("rx-%Y%m%d-%H%M%S-%f") + ".log")
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=.2, write_timeout=3)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    print(f"Close other serial monitors first. Port: {port}", flush=True)
    print(f"Saving directly to {path.resolve()}", flush=True)
    try:
        connection.open()
        with path.open("xb") as output:
            # Finish any cursor query issued before the port was opened, then
            # clear a partially typed console command before starting our own.
            connection.write(b"\x1b[1;80R\x03\r")
            record_for(connection, output, 2)
            connection.write(b"\x15status\r")
            record_for(connection, output, 2)
            if not args.dump_only:
                connection.write(b"\x15stop\r")
                record_for(connection, output, 3)
                command = f"scan {args.channel}" if args.channel else "scan"
                connection.write(b"\x15" + command.encode("ascii") + b"\r")
                print(f"Scanning {'CH'+str(args.channel) if args.channel else 'all CH'} for {args.seconds}s. Press transmitter TEST repeatedly.",
                      flush=True)
                record_for(connection, output, args.seconds)
            connection.write(b"\x15stop\r")
            record_for(connection, output, 5)
            connection.write(b"\x15status\r")
            record_for(connection, output, 2)
            connection.write(b"\x15rx_dump\r")
            print("Exporting IQ to file...", flush=True)
            record_for(connection, output, 90, until_dump=True)
        from decode_rx_dump import parse_dump, dump_rate
        saved=path.read_text(encoding="utf-8",errors="replace")
        lo, score, words = parse_dump(saved)
        print(f"Verified complete dump: rate={dump_rate(saved)} LO={lo} MHz, score={score}/32, {len(words)} samples.")
        print(f"Attach this file: {path.resolve()}")
    finally:
        connection.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, serial.SerialException) as error:
        print(f"Capture failed: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("Stopped. Captured bytes are saved in the log.", file=sys.stderr)
        sys.exit(130)
