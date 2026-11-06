[中文说明](PROVENANCE.zh-CN.md) · **English**

# Extraction map and scope

The source development tree was pinned at `eaf41f3c`. Paths below are relative to that old repository;
the new project has no runtime or build dependency on it.

| New file | Existing implementation |
|---|---|
| `firmware/src/targets/esp32s3.c` | `firmware/rf_timing_probe/main/main.c`: `init_radio`, `tx_prepare`, `stop_radio`; `experiments/modem_tx_probe/main/iq_probe.c`: `iq_sdr_prepare_tx`; `iq_stream.c`: loop control word |
| `firmware/src/targets/esp32s31.c` | `experiments/fesoc_ue_slot/firmware/main/rf_profile.c`; `experiments/s31_bs_downlink/firmware/main/main.c`; `firmware/s31_rf_probe/main/test_slot_tr.c`: `slotloop` |
| `tools/generate_iq.py` | `experiments/s31_bs_downlink/generate_wave.py`; `vivado/ue_ofdm_rx_ps_capture/reference/profile.py`; `experiments/s31_rf_research/tx_fir_model.py` |
| `tools/e310/receiver.py` | `vivado/ue_ofdm_rx_ps_capture/system/host/e310/analyze.py` and `qam16_diagnose.py` |

## What changed

- One init/load/rate/start/stop/status API selects a separate chip backend at build time.
- S3 exposes native 40/80 Msps; S31 exposes 16/32/40/80 Msps, based on existing measured rate fields.
- S31 retains the verified one-time writer reset and temporary RX profile comparison needed to restore TX PBUS differences. It does not start an RX capture or provide an RX application API.
- The S31 GPIO25 marker, PCNT counter, diagnostic CRC, experimental gain presets and register-debug commands were removed.
- `CONFIG_HEAP_TLSF_USE_ROM_IMPL=n` from `firmware/s31_rf_probe/sdkconfig.defaults` is mandatory and checked during compilation.
- The two targets share the existing five-symbol test profile. S31 preserves Q14 FIR8 interpolation; S3 adds offline band-limited ×10 interpolation and loop alignment.
- Loads are bounded and accepted only while stopped. Reserved upper IQ bits are rejected before SRAM is changed.
- No generic GDMA IQ descriptor chain is claimed. The S31 five-word REGDMA nodes perform register writes.
- No Wi-Fi video, MAC hook, FPGA/PS runtime, GPIO bridge, OTA, HARQ or exploratory register-sweep code was migrated.
- E310 demodulation uses pilots to estimate the channel. The known payload is used only to report errors and EVM.

## Historical hardware evidence

Old `docs/DAC_REPLAY.md` records independent S3 TX/RX measurements, including waveform correlation,
tone direction/rate changes and the NCO failure control. `docs/IQ_STREAMING.md` records loop and
length-alignment experiments.

Old `experiments/s31_bs_downlink/BOARD_REPORT.md` and `LIVE_VERIFICATION_20261015.md` record S31
periodic preloaded transmission and independent reception. Historical receiver/DC/lock-quality
limitations are not automatically resolved by extracting the TX code. New measurements are
reported separately in [VALIDATION.md](VALIDATION.md).
