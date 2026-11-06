# RX reference

ESPARGOS/esp-sdr, GPL-3.0-or-later. Commit:
`9b61e7465bee6da15209c5c09308043d08c46a1d`.

https://github.com/ESPARGOS/esp-sdr/blob/9b61e7465bee6da15209c5c09308043d08c46a1d/main/targets/esp32s3/receiver.c

`receiver.c` and `LICENSE` preserve the upstream source and license for review; they are not compiled.
The local `components/rf_transmitter/rf_receiver.c` adapts the burst snapshot register sequence
and RX setup. It uses the existing TX-reserved 64KiB RF aperture with exclusive worker ownership.
It does not import the upstream 192KiB continuous ring, USB transport, FFT, or extra GPIO logic.

The local decoder and CH/ID validator are in `components/godox_protocol/godox_receive.c`.
Godox validation uses the project's own protocol implementation and synthetic TX DSP tests.
This is an experimental integration on ESP-IDF v5.5.5: successful linking is not proof of
capture timing, receive sensitivity, or decoding on the user's hardware.
