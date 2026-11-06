/* Static placement must not reach RF SRAM, including the IRAM alias.
 * Runtime heap exclusion is provided by upstream SOC_RESERVE_MEMORY_REGION.
 * S3 alias offset: 0x40378000 - 0x3fc88000 = 0x6f0000. */
ASSERT(_iram_end <= 0x403c0000, "Application IRAM overlaps RF SRAM alias")
ASSERT(_bss_end <= 0x3fcd0000, "Application DRAM overlaps RF SRAM")
