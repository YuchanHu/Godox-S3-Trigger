idf_component_register(SRCS "godox_waveform.c" "godox_dsp.c" INCLUDE_DIRS "include"
    REQUIRES godox_protocol
    EMBED_FILES "waves/prepare.bin" "waves/test.bin" "waves/unprepare.bin")
