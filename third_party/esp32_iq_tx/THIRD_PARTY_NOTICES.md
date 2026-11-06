[中文说明](THIRD_PARTY_NOTICES.zh-CN.md) · **English**

# Attribution and licensing

Source in this repository is provided under GPL-3.0-or-later; see `LICENSE`.
Local extraction, integration and new API maintenance: mlinux.

## ESPARGOS / esp-sdr

- Project: [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr).
- Reference commit: `e74f2a470972ec163c247f1fe88d32e08579242a`.
- Files: `main/targets/esp32s31/burst/receiver.c` and `s31_spectrum.h`.
- License: GPL-3.0-or-later.
- Used for S31 RF SRAM initialization, bank access and clock setup, subsequently verified in local TX experiments.
- Original RX, USB and spectrum applications were not imported.

Original authors and contributors retain their rights. The referenced files have no separate author
copyright line; this notice preserves the project and commit attribution without inventing names.

## ESP32SDR and local experiments

The historical project is [lozaning/ESP32SDR](https://github.com/lozaning/ESP32SDR).
Extraction was based on local development commit `eaf41f3c`; this does not imply that the local commit
was published to that remote. File mappings are in `docs/PROVENANCE.md`.

The E310 diagnostic receiver reuses the local `analyze.py` and `qam16_diagnose.py` approach: CP CFO,
fixed-root PSS correlation, FFT and pilot LS with linear interpolation.

## Algorithmic references

[srsRAN 4G](https://github.com/srsran/srsRAN_4G) provided substantial guidance for PSS
acquisition/tracking, CP-based CFO estimation, synchronization validation and receiver organization.
[OpenOFDM](https://github.com/jhshi/openofdm) and [openwifi](https://github.com/open-sdr/openwifi)
were references for the broader OFDM synchronization, training and equalization architecture.
These acknowledgements describe ideas and study references; this extraction does not import their
decoder implementations or RTL. They do not replace the direct-source attribution above.

## ESP-IDF

ESP-IDF is supplied by Espressif and its contributors. This component links RF test libraries,
ROM symbols and peripheral drivers provided by the selected SDK. No SDK source or vendor library
binary is copied into this repository. Those dependencies retain their own licensing terms.
