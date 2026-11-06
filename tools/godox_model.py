"""Independent implementation of the documented Godox X short-frame PHY.

Algorithm reference: mildsunrise/godox-flash-radio, revision in third_party/README.md.
No runtime dependency on the reference module. Units are Hz and samples.
"""
import numpy as np

FS = 40_000_000
BASE_FS = 2_000_000
OFFSET = 2_500_000
COUNT = 16128
ALIGNMENT = 256
AMPLITUDE = 384  # 10-bit headroom, not calibrated RF power
GUARD = 320  # 8 us before the shaped waveform
COMMANDS = {"prepare": 0x11, "test": 0x09, "unprepare": 0x21}


def encode_id(netid):
    if not isinstance(netid, int) or not 0 <= netid <= 99:
        raise ValueError("ID must be 0 (OFF)..99")
    return 50024 // (netid + 1)


def frame(command, netid=0):
    if command not in COMMANDS.values():
        raise ValueError("unsupported short command")
    ident = encode_id(netid).to_bytes(2, "big")
    data = b"\xaa" * 4 + ident * 2 + bytes([0xd5, command])
    bits = np.unpackbits(np.frombuffer(data, dtype=np.uint8))
    return data, np.append(bits, bits[-1])


def base_wave(bits):
    # Eight samples/symbol: one bipolar impulse followed by seven zeros.
    impulses = np.zeros(len(bits) * 8)
    impulses[::8] = 1 - 2 * np.asarray(bits, dtype=float)
    pulse = np.exp(-np.linspace(-3, 3, 17) ** 2)
    deviation = np.convolve(impulses, pulse) * (200_000 / 1.2)
    phase = np.cumsum(deviation) * (2 * np.pi / BASE_FS)
    fsk = np.exp(1j * phase)
    # 15-tap Hamming-windowed 150 kHz lowpass, independently derived.
    n = np.arange(-7, 8)
    fir = 0.15 * np.sinc(0.15 * n) * np.hamming(15)
    fir /= fir.sum()
    return np.convolve(fsk, fir), phase


def interpolate(signal, factor=20):
    # Zero-extended windowed-sinc interpolation; no periodic FFT wraparound.
    n = np.arange(-12 * factor, 12 * factor + 1)
    kernel = np.sinc(n / factor) * np.kaiser(len(n), 8.6)
    up = np.zeros((len(signal) - 1) * factor + 1, dtype=complex)
    up[::factor] = signal
    full = np.convolve(up, kernel)
    delay = len(kernel) // 2
    return full[delay:delay + len(signal) * factor]


def pack(i, q):
    i, q = np.asarray(i), np.asarray(q)
    if np.any(i < -512) or np.any(i > 511) or np.any(q < -512) or np.any(q > 511):
        raise ValueError("IQ10 overflow")
    return ((i.astype(np.int32) & 1023) | ((q.astype(np.int32) & 1023) << 10)).astype("<u4")


def unpack(words):
    words = np.asarray(words, dtype=np.uint32)
    if np.any(words >> 20):
        raise ValueError("nonzero reserved bits")
    i = (words & 1023).astype(np.int32)
    q = ((words >> 10) & 1023).astype(np.int32)
    return np.where(i >= 512, i - 1024, i) + 1j * np.where(q >= 512, q - 1024, q)


def waveform(command):
    _, bits = frame(command)
    base, _ = base_wave(bits)
    native = interpolate(base)
    native *= np.exp(2j * np.pi * OFFSET * np.arange(len(native)) / FS)
    if GUARD + len(native) > COUNT or COUNT % ALIGNMENT:
        raise ValueError("RF SRAM capacity/alignment")
    signal = np.zeros(COUNT, dtype=complex)
    signal[GUARD:GUARD + len(native)] = native * AMPLITUDE
    return pack(np.rint(signal.real), np.rint(signal.imag))
