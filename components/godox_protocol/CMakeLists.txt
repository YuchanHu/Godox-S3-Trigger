idf_component_register(SRCS "godox_scan.c" "godox_protocol.c" "godox_receive.c" INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffp-contract=off)
