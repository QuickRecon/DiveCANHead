if(CONFIG_QUICKRECON_SD_FLASH)
    zephyr_library()
    zephyr_library_sources(sd_flash.c)
endif()
