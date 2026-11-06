"""Generate deterministic embedded IQ10 files and a manifest."""
import argparse
import hashlib
import json
from pathlib import Path
from godox_model import COMMANDS, FS, OFFSET, COUNT, GUARD, frame, waveform


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    manifest = {"sample_rate_hz": FS, "wifi_channel": 3, "lo_hz": 2422000000,
                "baseband_offset_hz": OFFSET, "target_hz": 2424500000,
                "id": 0, "bitrate": 250000, "samples": COUNT,
                "loop_us": COUNT / FS * 1e6, "guard_samples": GUARD,
                "format": "little-endian u32: I10[9:0], Q10[19:10]", "waves": {}}
    for name, cmd in COMMANDS.items():
        data = waveform(cmd).tobytes()
        (output / (name + ".bin")).write_bytes(data)
        raw, bits = frame(cmd)
        manifest["waves"][name] = {"bytes_hex": raw.hex(" "), "last_bit": int(bits[-1]),
                                   "bits": len(bits), "sha256": hashlib.sha256(data).hexdigest()}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "components/godox_waveform/waves")
    print(json.dumps(generate(parser.parse_args().output), indent=2))
