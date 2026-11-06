"""Reject ELF32 loaded segments overlapping the S3 RF SRAM or its IRAM alias."""
import argparse
import json
from pathlib import Path
import struct

RESERVED = ((0x3fcd0000, 0x3fce0000), (0x403c0000, 0x403d0000))


def check(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("expected little-endian ELF32")
    phoff = struct.unpack_from("<I", data, 28)[0]
    phsize, count = struct.unpack_from("<HH", data, 42)
    segments = []
    for n in range(count):
        kind, _, addr, _, _, size, flags, _ = struct.unpack_from("<8I", data, phoff + phsize * n)
        if kind != 1 or not size:
            continue
        for low, high in RESERVED:
            if addr < high and addr + size > low:
                raise ValueError(f"RF SRAM overlap: {addr:#x}..{addr+size:#x}")
        segments.append({"address": hex(addr), "end": hex(addr + size), "bytes": size, "flags": flags})
    if not segments:
        raise ValueError("no LOAD segments")
    return {"elf": str(path), "rf_sram_overlap": False, "segments": segments}


def check_resources(path, waves):
    """Check heap reservation and the three actual linked resources, using ELF symbols."""
    data = path.read_bytes()
    shoff = struct.unpack_from("<I", data, 32)[0]
    shsize, shcount = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<10I", data, shoff + n * shsize) for n in range(shcount)]
    symbols = {}
    for section in sections:
        if section[1] != 2: continue  # SHT_SYMTAB
        strings = sections[section[6]]
        table = data[strings[4]:strings[4] + strings[5]]
        for off in range(section[4], section[4] + section[5], section[9]):
            name, value, _, _, _, _ = struct.unpack_from("<IIIBBH", data, off)
            end = table.find(b"\0", name)
            symbols[table[name:end].decode()] = value

    def resource(first, last):
        low, high = symbols[first], symbols[last]
        for s in sections:
            if s[1] != 8 and s[3] <= low and high <= s[3] + s[5]:
                off = s[4] + low - s[3]
                return data[off:off + high - low]
        raise ValueError(f"unmapped ELF resource: {first}")

    reserved = resource("soc_reserved_memory_region_start", "soc_reserved_memory_region_end")
    if (0x3fcd0000, 0x3fce0000) not in list(struct.iter_unpack("<II", reserved)):
        raise ValueError("RF SRAM heap reservation missing from linked ELF")
    for name in ("prepare", "test", "unprepare"):
        linked = resource(f"_binary_{name}_bin_start", f"_binary_{name}_bin_end")
        if linked != (waves / (name + ".bin")).read_bytes():
            raise ValueError(f"linked waveform mismatch: {name}")
    return {"rf_heap_reservation": True, "embedded_waves_match": True}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--waves", type=Path)
    args = parser.parse_args()
    result = check(args.elf)
    if args.waves: result.update(check_resources(args.elf, args.waves))
    print(json.dumps(result, indent=2))
