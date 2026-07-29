cmake_minimum_required(VERSION 3.20.0)

# Wrap MCUBoot, flash-area, reboot, and ISO-TP send symbols so the write-DID
# handlers in uds.c can be exercised against a controlled fixture. Must be
# set BEFORE find_package(Zephyr) so they apply to the final executable
# link, not just the `app` static library.
add_link_options(
    -Wl,--wrap=boot_read_bank_header
    -Wl,--wrap=boot_request_upgrade
    -Wl,--wrap=boot_is_img_confirmed
    -Wl,--wrap=flash_area_open
    -Wl,--wrap=flash_area_erase
    -Wl,--wrap=flash_area_close
    -Wl,--wrap=sys_reboot
    -Wl,--wrap=ISOTP_Send
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_uds_dids)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/divecan/uds/uds.c
    ${APP_SRC}/divecan/divecan_channels.c
    ${APP_SRC}/errors.c
)
# uds_ota.c, uds_settings.c, uds_state_did.c, uds_log_download.c and the
# solenoid/flash-log drivers are deliberately excluded — this module targets
# the DID dispatch + write-handler layer of uds.c itself. Stubs for every
# symbol uds.c references from those modules live in main.c.

target_include_directories(app PRIVATE
    # Test-local include shadow MUST come first — overrides the production
    # solenoid_roles.h whose SOL_DEVICE macro depends on a DT node that
    # doesn't exist on native_sim.
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${APP_SRC}/divecan/include
    ${APP_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Unlike tests/uds_ota (which builds uds.c with the log/solenoid/DiveO2
# sections compiled OUT), this module forces the production feature gates ON
# so the CONFIG_FLASH_LOG / CONFIG_HAS_O2_SOLENOID / CONFIG_HAS_DIVEO2_CELL
# handler blocks (0xF242/0xF243/0xF282-0xF284/0xF4x0D and the log-download
# routing) are compiled and covered.
target_compile_definitions(app PRIVATE
    CONFIG_FLASH_LOG=1
    CONFIG_HAS_O2_SOLENOID=1
    CONFIG_HAS_DIVEO2_CELL=1
)

# glibc gates strnlen() etc. behind _POSIX_C_SOURCE on the native_sim host
# build — the production STM32 picolibc exposes them unconditionally.
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)
