[中文说明](README.zh-CN.md) · **English**

> **Disclaimer**
>
> The RF tests and measurements presented in this repository were conducted with the devices
> placed inside an RF-shielded test enclosure. The code is provided solely for educational and
> learning purposes. Users must comply with all applicable local laws and regulations.

# ESP32 IQ TX — ESP32-S3 and ESP32-S31

**Preloaded arbitrary-IQ transmission and reproducible OFDM test waveforms.**

Transmit user-supplied complex IQ samples through the ESP32 RF SRAM playback path. This repository extracts the working transmit code from a much larger experimental tree and packages it as a small ESP-IDF component, three examples, and reproducible waveform/instrument tools.

The included OFDM waveforms contain a fixed-root PSS followed by QPSK or 16QAM test data.
Use them to test the RF playback path and an OFDM receiver with known symbols.

## Hardware used

| Target | Development board used | Notes |
|---|---|---|
| ESP32-S3 | [Freenove ESP32-S3 WROOM Board](https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board) | Native USB for flashing/console; the supplied firmware uses an 8 MB flash layout |
| ESP32-S31 | [ESP32-S31-Function-CoreBoard-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-function-coreboard-1/user_guide.html) | N16R16 board used here; IQ storage is internal RF SRAM, not PSRAM |
| Measurement receiver | ANTSDR E310 with Pluto-compatible firmware | Optional; connected to the PC through libiio, normally capturing at 4 Msps |

### Freenove ESP32-S3

<img src="https://raw.githubusercontent.com/Freenove/Freenove_ESP32_S3_WROOM_Board/main/Board.jpg" alt="Freenove ESP32-S3 WROOM Board, manufacturer reference photo" width="420">

Manufacturer reference image: [Freenove board repository](https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board).
The camera, SD card and other pictured accessories are not required for IQ transmission.

### ESP32-S31-Function-CoreBoard-1

<img src="https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/_images/esp32-s31-function-coreboard-1-isometric.png" alt="ESP32-S31-Function-CoreBoard-1, Espressif reference photo" width="420">

Manufacturer reference image: [Espressif board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-function-coreboard-1/user_guide.html).
These images identify the board families; memory markings and board revisions may differ.
The images are loaded from their manufacturers and retain their original ownership and terms.
The examples do not require an FPGA or external GPIO wiring.

## What is supported

| | ESP32-S3 | ESP32-S31 |
|---|---|---|
| Native IQ playback rates | **40 / 80 Msps** | **16 / 32 / 40 / 80 Msps** |
| Default rate | 40 Msps | 16 Msps |
| Packed sample | Signed I10 in bits 9:0, Q10 in bits 19:10, upper 12 bits zero | Same |
| Playback SRAM | `0x3fcd0000` | `0x2f060000`, B bank |
| Maximum accepted samples | 16128 | 16380 |
| Hardware loop alignment | 256 samples | 256 samples, conservative limit |
| Periodic hardware playback | Not exposed | GPTimer → ETM → REGDMA |
| Default OFDM example | A 403.2 µs hardware loop | A 400 µs record every 1 ms |
| Per-period CPU refill / ISR | None | None |

This is **preloaded arbitrary IQ**, not an endless host-fed sample stream. The RF engine reads reserved internal SRAM directly. No ordinary GDMA linked list transports the IQ in this implementation. On S31, a separate REGDMA chain writes control registers at timer events; it does not copy IQ data.

## How transmission works

1. Initialize the vendor PHY/RF test environment. On S3, briefly initialize Wi-Fi first and then unload its driver before taking exclusive control of the RF hardware.
2. Run the vendor `esp_phy_wifi_tx_tone()` preparation to obtain a working RF/PA configuration. This preparation briefly produces CW.
3. **Disable the NCO tone generators explicitly.** S3 clears bit 18 at `0x60006040`, `0x60006044`, and `0x6000604c`; S31 clears bit 18 at `0x2010041c` and `0x20100420`.
4. Select the verified playback clock configuration and give the preloaded SRAM bank to RF.
5. Write the sample count and PLAY enable. The resulting modulation comes from the user's SRAM samples rather than the Wi-Fi packet modem.
6. Stop PLAY and close the RF/bank gates before allowing CPU access to the sample memory again.

### Why the original raw-IQ attempt did not succeed

The historical ESP32SDR README described arbitrary-IQ transmission on S3 as a path that configured correctly but did not reach RF. Later local experiments demonstrated that this was not a hardware impossibility.

One directly verified cause was **leaving the NCO enabled after vendor CW preparation**: the receiver observed the tone rather than the requested SRAM IQ. The working sequence retains vendor RF/PA preparation, then explicitly clears all three NCO enables before starting SRAM replay. Merely configuring PBUS and the DAC clock was also insufficient in the tested manual bring-up attempt.

Correct SRAM ownership, count encoding and loop alignment are also necessary. The working path uses `0x60033d64` for S3 playback, the register used by the vendor `dactrig` implementation; an unrelated descriptor experiment must not be treated as the same data path. The new driver copies bounded user data itself rather than calling a vendor helper that overwrites the buffer with a ramp.

This explains the successful path and an experimentally isolated failure cause. It does not claim that every historical failed experiment had the same cause. Source mappings and validation boundaries are recorded in [PROVENANCE.md](docs/PROVENANCE.md).

## Essential registers and sample rates

### ESP32-S3

| Address / function | Role |
|---|---|
| `0x600c101c[3:0]` | SRAM ownership: idle `0`, playback `4`, preserving other bits |
| `0x60033d64` | Playback count, low-count duplicate in bits 27:20, bit 19 loop, bit 31 enable |
| `0x60033d64[15]` | `0` for 40 Msps, `1` for 80 Msps, together with the ROM rate configuration |
| `rom_dac_rate_set()` | Argument `1` for this 40 Msps path, `0` for 80 Msps |
| `0x60033d5c` | ADC capture disabled during TX |
| `0x60006040/44/4c[18]` | NCO enables; explicitly cleared after CW preparation |

### ESP32-S31

The following matrix was verified in the source experiments with playback timing and independent over-the-air tones. It selects the SRAM IQ update rate; it does not imply direct measurement of the internal physical DAC clock.

| Native IQ rate | `0x20109c04[21]` clock mux | `0x20109008[24:21]` divider field |
|---:|---:|---:|
| 16 Msps | 0 | 1 |
| 32 Msps | 0 | 0 |
| 40 Msps | 1 | 1 |
| 80 Msps | 1 | 0 |

| Address | Role |
|---|---|
| `0x2010900c` | Sample count, bit 19 loop, bit 31 PLAY enable |
| `0x20586114` | B-bank RF access `0xff000000`, CPU access restored with `0` |
| `0x20100890[11:8]` | Existing verified TX-force request combinations: `0xe` on, `0x8` off |
| `0x201070dc[16]` | Cleared as part of the verified TX enable preparation |
| `0x2010041c/420[18]` | NCO enables cleared after vendor preparation |
| `0x20100408`, mask `0x00fe0000` | Restore the captured vendor TX power-control fields |

Full register descriptions, preserved initialization operations and the five-word REGDMA descriptor layout are in [REGISTERS.md](docs/REGISTERS.md).

## Public API

```c
#include "iq_tx.h"

iq_tx_config_t config = { .channel = 6, .rf_backoff_qdb = 60 };
ESP_ERROR_CHECK(iq_tx_init(&config));

// Choose one of the rates listed by iq_tx_capabilities().
ESP_ERROR_CHECK(iq_tx_set_sample_rate(40000000));
ESP_ERROR_CHECK(iq_tx_load(my_packed_iq, sample_count));
ESP_ERROR_CHECK(iq_tx_start_loop());
// ...
ESP_ERROR_CHECK(iq_tx_stop());
```

On S31, use `iq_tx_start_periodic(1000)` for one burst per millisecond. The first burst follows one full period. The driver computes the closing timer from the **selected rate** and sample count, with a 20 µs tail allowance. The period must be at least the waveform duration plus 25 µs, including 5 µs for control-chain completion. S3 returns `ESP_ERR_NOT_SUPPORTED` for periodic mode.

Rate changes and sample loads are accepted only while stopped. Changing the native rate **does not resample the buffer**: the same 64-sample circle produces `Fs / 64`, and the same OFDM samples change bandwidth and duration when played at another rate. The supplied OFDM files are generated for the default chip rate. Generate appropriately resampled data when a fixed RF bandwidth is required at another native rate.

The interface has one owning task. It does not support concurrent Wi-Fi/BT operation or another owner of the same RF hardware. S31 also owns PAU, the first two GPTimers and its ETM routes: call `iq_tx_init()` before allocating any GPTimer.

`rf_backoff_qdb` is the vendor CW preparation argument in quarter-dB units, **not a calibrated arbitrary-IQ output power in dBm**. Sample RMS and RF gain settings both affect the radiated signal.

## Examples

All examples share one small IDF build project. Selecting another example changes the application source and reuses already compiled SDK objects.

| Example | Purpose | Console commands |
|---|---|---|
| [`ofdm_tx`](examples/ofdm_tx/) | PSS + QPSK/16QAM OFDM downlink test | `start qpsk`, `start qam16`, `info`, `stop` |
| [`sample_rates`](examples/sample_rates/) | Native playback rates using a 64-sample circle | `tone 40`, `tone 80`; also `tone 16`, `tone 32` on S31; `stop` |
| [`basic`](examples/basic/) | Pack caller-generated IQ10 and preload a simple QPSK staircase | `send`, `stop` |

Applications initialize RF and remain stopped until a command is entered. The CW preparation performed during initialization/start must be distinguished from the stopped application's normal operation.

### OFDM test waveform

- 4 Msps baseband, FFT 256, CP 32, subcarrier spacing 15.625 kHz.
- 120 active data-symbol carriers: ±1…±60, including 12 pilots and 108 data carriers.
- Five 72 µs symbols: one PSS, then four deterministic QPSK/16QAM symbols; 40 µs zero tail.
- PSS root 25; 62 occupied PSS carriers from a length-63 ZC construction with the center element removed.
- S31: the original eight-tap, four-phase Q14 interpolation to 16 Msps, 6400 samples.
- S3: offline band-limited ×10 interpolation to 40 Msps, then 128 zero samples for loop alignment, 16128 samples.
- Known symbols only; no IP framing, CRC, coding, scheduling, registration or HARQ.

### Generate the waveform `.bin` files

Run these commands from the **repository root**; no board or exported ESP-IDF environment is
needed to generate the samples:

```sh
python -m pip install -r tools/requirements.txt
python tools/generate_iq.py
python tools/check_iq.py
```

The generator creates **both modulations for both chips** in one run:

| Output directory | Files | Samples per file | Bytes per file | Intended playback rate |
|---|---|---:|---:|---:|
| `examples/ofdm_tx/waves/esp32s3/` | `qpsk.bin`, `qam16.bin` | 16128 | 64512 | 40 Msps |
| `examples/ofdm_tx/waves/esp32s31/` | `qpsk.bin`, `qam16.bin` | 6400 | 25600 | 16 Msps |

`examples/ofdm_tx/waves/manifest.json` records the format, seeds, carrier map, interpolation,
sample counts and SHA256 of each file. Generation is deterministic; `check_iq.py` regenerates
the files in a temporary directory and checks exact reproducibility, packing, CP and pilots.

The processing order is: fixed-root PSS and seeded QPSK/16QAM carrier grids → 256-point IFFT →
32-point CP → four data symbols and a zero tail → quantized 4 Msps IQ → chip-specific
interpolation → packed native-rate IQ10. S31 uses the eight-tap four-phase Q14 interpolator;
S3 uses band-limited DFT interpolation and zero padding for the loop-length alignment.
This interpolation runs **offline in Python**, not once per transmit period on the ESP32.

Each `.bin` is a **headerless sample array**, not an executable firmware image: every sample is
one little-endian `uint32`, with signed I10 in bits 9:0, signed Q10 in bits 19:10 and zero in
bits 31:20. There is no length header, packet framing or CRC inside these waveform files.

To generate a separate preview without changing the embedded waveforms:

```sh
python tools/generate_iq.py --output .build/generated_waves
```

That alternate directory is not used by the build. The default files under `examples/ofdm_tx/waves/`
are embedded into the firmware by CMake `EMBED_FILES`; changing them requires rebuilding and
flashing. `sample_rates` and `basic` generate their own samples in C and do not need these files.

**Firmware `.bin` files are a different output.** Build them with:

```sh
python tools/build.py --target esp32s31 --example ofdm_tx
```

This generates `.build/esp32s31/ofdm_tx.bin` and the bootloader/partition images.
Use `tools/flash.py` to flash that complete image set; do not flash `waves/*/*.bin` as firmware.
The waveform files are committed for reproducibility; firmware and build outputs remain ignored.

## Build and flash

Use an exported environment for the matching ESP-IDF version:

| Target | Tested SDK | PHY library SHA256 |
|---|---|---|
| S3 | ESP-IDF v5.3.5 | `29dcc18a035801bd41486d5d59f3caeb62f8ed153533c488828bffef1abb99fd` |
| S31 | ESP-IDF 6.2 development [commit 624ded6069c4399e190d62b39ea67d2789ea496f](https://github.com/espressif/esp-idf/commit/624ded6069c4399e190d62b39ea67d2789ea496f) | `116c7c3a55e2ddfd7937edad2667b5d64717f1967519a0201ca6354246f0b1a2` |

The SDK provides the private RF functions and vendor libraries. They are not redistributed here.

**S31 requires `CONFIG_HEAP_TLSF_USE_ROM_IMPL=n`.** The ROM heap-lock pointer at
`0x2f07f9e4` lies inside the SRAM group handed to RF during playback. Using the ROM heap
implementation can hang the control console while transmission continues. The supplied defaults
disable it, and the backend rejects an incompatible configuration at compile time. The SDK already
reserves the ROM tail; do not extend the application reservation across it.

```sh
python -m pip install -r tools/requirements.txt
python tools/check_iq.py
python tools/build.py --target esp32s31 --example ofdm_tx
python tools/flash.py --target esp32s31 --port YOUR_PORT --serial YOUR_USB_SERIAL
```

For S3, use its exported SDK environment and replace the target with `esp32s3`. Select another application with `--example sample_rates` or `--example basic` for both build and flash. The flash tool checks the connected USB identity, chip, selected example and image hash.

Build outputs and separate chip configurations are in `.build/<target>/`. The last selected example replaces that target's active image. Builds use one compiler job, stop below 300 MiB free RAM, and verify static ELF segments do not overlap the reserved RF SRAM, including S3 IRAM/DRAM aliases.

Standard IDF commands work with the same project. From `examples/ofdm_tx`, use an absolute build directory:

```sh
idf.py -B /absolute/path/to/build -DIDF_TARGET=esp32s31 build
idf.py -B /absolute/path/to/build -p YOUR_PORT flash monitor
```

`menuconfig` → **IQ transmitter example** selects channel and RF preparation backoff. S3 defaults to an 8 MB flash layout; S31 uses a 2 MB layout suitable for larger flash chips too. The S31 N16R16 configuration keeps the PSRAM domain at 1.8 V, while PSRAM itself is not used for IQ storage.

## Real E310 measurements

Both boards below were measured with E310 using this repository's transmitter. See
[validation and measurement notes](docs/VALIDATION.md) for conditions and saved raw data.

### S31: latest actual RX spectrum

![E310 actual RX spectrum of the S31 16QAM example](docs/measurements/esp32s31/spectrum_actual_rx.png)

This is generated from the latest saved **E310 RF-on IQ buffer**, not an ideal waveform or an old
simulation. S31 plays a 400 µs record at 16 Msps once per millisecond. Capture time is printed
on the image; the raw array and matching instrument
settings are in `docs/measurements/esp32s31/qam16/`. The [two-modulation RF-on/off comparison](docs/measurements/esp32s31/spectrum.png)
is retained separately.

### S31: decoded constellation

![E310 decoded QPSK and 16QAM constellations](docs/measurements/esp32s31/constellation.png)

The instrument tool records real IQ in a single libiio buffer, saves the untouched samples and hardware readbacks, then generates `spectrum.png` and **`constellation.png`**. Blue constellation dots are real equalized RX data; red crosses show ideal positions. Pilots estimate the channel; the known payload is used only to count errors and calculate EVM.

### S3: actual RX spectrum — Freenove board, 40 Msps playback

![E310 actual RX spectrum of the Freenove ESP32-S3 16QAM example](docs/measurements/esp32s3/spectrum_actual_rx.png)

This capture uses the **Freenove ESP32-S3 WROOM Board** running `ofdm_tx`: 16128 native IQ samples
at **40 Msps**, repeating as a **403.2 µs hardware loop**. It is a separate S3 experiment, not an
S31 image. E310 received at **4 Msps**, 3 MHz bandwidth and manual gain 20 dB, with its LO about
250 kHz above the 2437 MHz TX center. Capture time is printed on the actual RX spectrum.

### S3: decoded QPSK and 16QAM constellations

![E310 decoded QPSK and 16QAM constellations from the Freenove ESP32-S3](docs/measurements/esp32s3/constellation.png)

| S3 waveform | Complete decoded bursts | Hard-bit errors | Known-data EVM | ADC rails |
|---|---:|---:|---:|---:|
| QPSK | 20 | 0 / 17280 | 5.22% | 0 |
| 16QAM | 20 | 0 / 34560 | 5.24% | 0 |

S3 raw RF-on/off IQ, instrument settings, decoded points and the flashed-image build report are
saved under `docs/measurements/esp32s3/`. The [two-modulation on/off spectrum comparison](docs/measurements/esp32s3/spectrum.png)
is also available. Reproduce the capture with `tools.e310.capture` and `--period-us 403.2`.
This run verifies the 40 Msps OFDM path; it is not a new RF measurement of the 80 Msps selection.
S3 and S31 have different replay duty cycles and were captured separately; the receiver-code PSD
plots are not a calibrated TX-power or range comparison.

### Live decoding, constellation and spectrum

```sh
python -m pip install -r tools/requirements-instrument.txt
python -m tools.e310.live --uri YOUR_IIO_URI --modulation qam16
```

Run `ofdm_tx` with matching modulation first. Optionally add
`--tx-port YOUR_PORT --tx-serial YOUR_TX_USB_SERIAL` to start and manage it automatically.
Window keys **1 / 2** select QPSK / 16QAM; they also switch the managed transmitter when TX control
is enabled. For the S3 default loop, add `--period-us 403.2`.

The receiver performs CP/PSS synchronization, CFO correction, FFT, pilot LS/interpolation and hard
decisions. It shows PSS, CFO, known-data EVM, BER and missing-detection counters. No FEC/IP decoder
is implied. The GUI displays snapshots; it does not claim lossless decoding of every RF slot.
See [the E310 guide](tools/e310/README.md) for Windows setup, a timed hardware check and saved output.

```sh
python -m pip install -r tools/requirements-instrument.txt
python -m tools.e310.capture --uri YOUR_IIO_URI --port YOUR_PORT \
    --serial YOUR_TX_USB_SERIAL --output docs/measurements/new_capture
```

On Windows, add `--dll-dir PATH_TO_MATCHING_LIBIIO_DLLS` if required. E310 capture defaults to 4 Msps, manual gain and a small LO offset to separate receiver DC from the TX center. The instrument tool stops TX on completion.

## Repository layout

```text
firmware/include/              Public TX API
firmware/src/targets/          Separate S3 and S31 register backends
examples/ofdm_tx/              IDF build project and OFDM application
examples/sample_rates/         Native-rate tone application
examples/basic/                Caller-provided packed-IQ application
tools/                        Build and waveform tools
tools/e310/                   Live OFDM decoder, constellation/spectrum and measurements
docs/                         Registers, provenance and measured results
```

Waveforms can be reproduced with `python tools/generate_iq.py`; the committed manifest contains formats, counts, rates and hashes. No local network address, key, board serial or dependency on the original workspace is required by the examples.

## Acknowledgements

Thank you to the authors and contributors of these projects for sharing their implementations
and explanations:

| Project | What informed this work |
|---|---|
| [lozaning/ESP32SDR](https://github.com/lozaning/ESP32SDR) | Starting point for RF/register exploration, SRAM IQ paths and the local experiments from which the working TX implementation was extracted |
| [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr) | S31 RF SRAM initialization, bank ownership and clock setup; source-level derivation is recorded in the provenance notes |
| [srsRAN 4G](https://github.com/srsran/srsRAN_4G) | Many insights for receiver decoding: PSS acquisition/tracking, CP-based CFO estimation, synchronization quality checks and channel-estimation organization; see its [PSS](https://github.com/srsran/srsRAN_4G/blob/master/lib/src/phy/sync/pss.c) and [synchronization](https://github.com/srsran/srsRAN_4G/blob/master/lib/src/phy/sync/sync.c) implementations |
| [OpenOFDM](https://github.com/jhshi/openofdm) and [openwifi](https://github.com/open-sdr/openwifi) | References for the OFDM synchronization, training, channel-estimation and equalization pipeline in the wider modem work; no RTL from those projects is included here |
| [ESP-IDF](https://github.com/espressif/esp-idf) | PHY/RF preparation, ROM functions, GPTimer, ETM and peripheral-driver infrastructure |
| [pyadi-iio](https://github.com/analogdevicesinc/pyadi-iio) and [libiio](https://github.com/analogdevicesinc/libiio) | E310/Pluto sample acquisition and instrument control for the real-RF checks |

The srsRAN acknowledgement describes algorithmic guidance, not a claim that its complete decoder
was ported into this repository. Direct source derivations and licenses are listed separately in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [PROVENANCE.md](docs/PROVENANCE.md).

## License and attribution

[GPL-3.0-or-later](LICENSE). S31 RF SRAM initialization derives from [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr); the transmit paths, NCO fix and timing chains were extracted from the local ESP32SDR experiments. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [PROVENANCE.md](docs/PROVENANCE.md).
