# Shared raw-SD flash module for the MCUBoot child image.  The application adds
# the same module from its CMakeLists.txt; sysbuild images are separate CMake
# projects and therefore need the path passed explicitly.
set(mcuboot_EXTRA_ZEPHYR_MODULES
    "${CMAKE_CURRENT_LIST_DIR}/drivers/sd_flash"
    CACHE INTERNAL "DiveCAN shared MCUBoot modules")

# MainPCB replaces SPI NOR with an SDMMC-backed raw flash device.  Keep these
# overrides board-specific so the established DiveCAN Jr MCUBoot image and RTT
# logging behaviour are unchanged.
if(BOARD MATCHES "^divecan_main")
    # SDMMC plus its raw flash adapter is slightly larger than the SPI-NOR
    # stack it replaces.  Keep this optimisation and the smaller RTT buffer
    # local to MainPCB; the Jr binary and its RTT control-block address remain
    # byte-for-byte configuration compatible with existing tooling.
    set_config_bool(${DEFAULT_IMAGE} CONFIG_LTO TRUE)
    set_config_bool(${DEFAULT_IMAGE} CONFIG_ISR_TABLES_LOCAL_DECLARATION TRUE)
    set_config_int(${DEFAULT_IMAGE} CONFIG_SEGGER_RTT_BUFFER_SIZE_UP 3584)

    set_config_bool(mcuboot CONFIG_SPI FALSE)
    set_config_bool(mcuboot CONFIG_SPI_NOR FALSE)
    set_config_bool(mcuboot CONFIG_SPI_NOR_SFDP_RUNTIME FALSE)
    set_config_bool(mcuboot CONFIG_I2C FALSE)
    set_config_bool(mcuboot CONFIG_ADC FALSE)
    set_config_bool(mcuboot CONFIG_SENSOR FALSE)
    set_config_bool(mcuboot CONFIG_CAN FALSE)
    set_config_bool(mcuboot CONFIG_SERIAL FALSE)
    set_config_bool(mcuboot CONFIG_DMA FALSE)
    set_config_bool(mcuboot CONFIG_COUNTER FALSE)
    set_config_bool(mcuboot CONFIG_SETTINGS FALSE)
    set_config_bool(mcuboot CONFIG_NVS FALSE)
    set_config_bool(mcuboot CONFIG_PM_DEVICE FALSE)

    set_config_bool(mcuboot CONFIG_FLASH TRUE)
    set_config_bool(mcuboot CONFIG_FLASH_MAP TRUE)
    set_config_bool(mcuboot CONFIG_FLASH_PAGE_LAYOUT TRUE)
    set_config_bool(mcuboot CONFIG_DISK_ACCESS TRUE)
    set_config_bool(mcuboot CONFIG_DISK_DRIVERS TRUE)
    set_config_bool(mcuboot CONFIG_DISK_DRIVER_SDMMC TRUE)
    set_config_bool(mcuboot CONFIG_SDMMC_STM32 TRUE)
    set_config_bool(mcuboot CONFIG_QUICKRECON_SD_FLASH TRUE)
    set_config_bool(mcuboot CONFIG_REGULATOR TRUE)

    # The 36 KiB boot partition is part of the deployed OTA contract.  MainPCB
    # therefore has no boot-time text output: structured logging, printk, the
    # boot banners, and RTT are all application-only.  Boot outcome remains
    # observable through the image state and the application's boot history.
    set_config_bool(mcuboot CONFIG_LOG FALSE)
    set_config_bool(mcuboot CONFIG_PRINTK FALSE)
    set_config_bool(mcuboot CONFIG_BOOT_BANNER FALSE)
    set_config_bool(mcuboot CONFIG_MCUBOOT_BOOT_BANNER FALSE)
    set_config_bool(mcuboot CONFIG_CONSOLE FALSE)
    set_config_bool(mcuboot CONFIG_RTT_CONSOLE FALSE)
    set_config_bool(mcuboot CONFIG_USE_SEGGER_RTT FALSE)
endif()
