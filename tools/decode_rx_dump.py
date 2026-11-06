"""Validate an rx_dump serial log and decode all Godox CH candidates for its LO."""
import argparse
import ctypes as C
from pathlib import Path
import re


def parse_dump(text):
    header = re.search(r"RXIQ BEGIN rate=(\d+) count=(\d+) lo=(\d+) score=(\d+)", text)
    if not header:
        raise ValueError("RXIQ BEGIN missing")
    rate, count, lo, score = map(int, header.groups())
    if rate not in [16000000,40000000] or count != 16128 or lo not in range(2412, 2463, 5):
        raise ValueError("Unsupported capture metadata")
    end = text.find("RXIQ END", header.end())
    if end < 0:
        raise ValueError("RXIQ END missing (incomplete serial log)")
    words = []
    for match in re.finditer(r"RXIQ (\d{5}) ([0-9a-fA-F]{160})(?![0-9a-fA-F])",
                             text[header.end():end]):
        if int(match[1]) != len(words):
            raise ValueError("Missing or reordered IQ row")
        words.extend(int(match[2][i:i+5], 16) for i in range(0, 160, 5))
    if len(words) != count:
        raise ValueError(f"Incomplete IQ capture: {len(words)}/{count}")
    return lo, score, words


def dump_rate(text):
    match=re.search(r"RXIQ BEGIN rate=(\d+)",text)
    if not match:raise ValueError("RXIQ BEGIN missing")
    return int(match[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--library", type=Path,
                        default=Path(__file__).resolve().parents[1]/"artifacts/stage_host.dll")
    args = parser.parse_args()
    text=args.log.read_text(encoding="utf-8-sig")
    lo, score, words = parse_dump(text)
    rate=dump_rate(text)
    lib = C.CDLL(str(args.library.resolve()))
    lib.godox_decode_capture_ids.restype = C.c_uint
    lib.godox_scan_offset.restype = C.c_int32
    samples = (C.c_uint32*len(words))(*words)
    print(f"{rate/1000000:g} MS/s, {len(words)} IQ pairs, LO {lo} MHz, retained sync score {score}/32")
    iq8=(C.c_int8*(2*len(words)))()
    for k,word in enumerate(words):
        iq8[2*k]=C.c_int8((word>>2)&255).value
        iq8[2*k+1]=C.c_int8((word>>12)&255).value
    for ch in range(1, 33):
        offsets = {lib.godox_scan_offset(ch, variant, 1+(lo-2412)//5) for variant in [0, 1]}
        for offset in sorted(offsets):
            if abs(offset) > 5000000:
                continue
            ids = (C.c_uint*4)()
            diag = (C.c_uint32*5)()
            if rate==16000000:
                found=lib.godox_decode_capture_ids(samples,C.c_size_t(len(words)),offset,ids,4,diag)
            else:
                carriers=(C.c_int32*4)()
                found=lib.godox_decode_iq8_rate(iq8,C.c_size_t(len(words)),rate,offset,ids,4,diag,carriers)
            print(f"CH{ch:02} offset={offset} IDs={list(ids[:found])} "
                  f"trials/sync/frames/rejected/best_sync={list(diag)}")
            if rate==40000000 and hasattr(lib,'godox_verify_iq8_header'):
                found=lib.godox_verify_iq8_header(iq8,C.c_size_t(len(words)),offset,
                                                 ids,4,diag,carriers)
                print(f"  confirmation headers={list(ids[:found])}, native_Hz={list(carriers[:found])}")


if __name__ == "__main__":
    main()
