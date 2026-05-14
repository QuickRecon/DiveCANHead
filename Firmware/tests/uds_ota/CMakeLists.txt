cmake_minimum_required(VERSION 3.20.0)

# Wrap flash, MCUBoot, and ISO-TP send symbols so the OTA pipeline can be
# exercised without a real flash backend or CAN driver. Must be set BEFORE
# find_package(Zephyr) so they apply to the final executable link, not just
# the `app` static library.
add_link_options(
    -Wl,--wrap=flash_area_open
    -Wl,--wrap=flash_area_close
    -Wl,--wrap=flash_area_read
    -Wl,--wrap=flash_area_erase
    -Wl,--wrap=flash_img_init_id
    -Wl,--wrap=flash_img_buffered_write
    -Wl,--wrap=flash_img_check
    -Wl,--wrap=boot_read_bank_header
    -Wl,--wrap=boot_request_upgrade
    -Wl,--wrap=sys_reboot
    -Wl,--wrap=ISOTP_Send
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_uds_ota)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/divecan/uds/uds.c
    ${APP_SRC}/divecan/uds/uds_ota.c
    ${APP_SRC}/divecan/divecan_channels.c
    ${APP_SRC}/errors.c
)
# uds_state_did.c and uds_settings.c are deliberately excluded — they pull
# in DT-resolved power and oxygen-cell devices that don't exist on
# native_sim. Stubs for the symbols uds.c needs from them live in main.c.

target_include_directories(app PRIVATE
    ${APP_SRC}/divecan/include
    ${APP_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# glibc gates strnlen() etc. behind _POSIX_C_SOURCE on the native_sim host
# build — the production STM32 picolibc exposes them unconditionally.
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)
