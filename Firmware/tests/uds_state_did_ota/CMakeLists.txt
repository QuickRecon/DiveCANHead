cmake_minimum_required(VERSION 3.20.0)

# Wrap MCUBoot, factory_image, firmware_confirm, and the ISO-TP send
# symbol so the OTA DID handlers (both read 0xF270-0xF274 and write
# 0xF275-0xF277) can be exercised against a controlled fixture. Must be
# set BEFORE find_package(Zephyr) so they apply to the final executable
# link, not just the `app` static library.
add_link_options(
    -Wl,--wrap=boot_read_bank_header
    -Wl,--wrap=boot_is_img_confirmed
    -Wl,--wrap=mcuboot_swap_type
    -Wl,--wrap=boot_fetch_active_slot
    -Wl,--wrap=boot_request_upgrade
    -Wl,--wrap=sys_reboot
    -Wl,--wrap=factory_image_is_captured
    -Wl,--wrap=factory_image_get_version
    -Wl,--wrap=factory_image_get_sem_ver
    -Wl,--wrap=factory_image_restore_to_slot1
    -Wl,--wrap=factory_image_force_capture_async
    -Wl,--wrap=firmware_confirm_get_state
    -Wl,--wrap=firmware_confirm_get_pass_mask
    -Wl,--wrap=ISOTP_Send
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_uds_state_did_ota)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/divecan/uds/uds.c
    ${APP_SRC}/divecan/uds/uds_state_did.c
    ${APP_SRC}/divecan/divecan_channels.c
    ${APP_SRC}/oxygen_cell_channels.c
    ${APP_SRC}/errors.c
)
# uds_ota.c, uds_settings.c are deliberately excluded — they pull in
# transports + DT-resolved devices we don't need to exercise here.
# Stubs for the symbols uds.c references from them live in main.c.

target_include_directories(app PRIVATE
    # Test-local include shadow MUST come first — overrides the
    # production power_management.h whose POWER_DEVICE macro depends on
    # a DT node that doesn't exist on native_sim.
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${APP_SRC}/divecan/include
    ${APP_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# uds_state_did.c uses CONFIG_CELL_COUNT to size the per-cell channel
# pointer table. oxygen_cell_channels.c uses the same Kconfig to gate
# chan_cell_2/3 definitions. Force the production value here.
target_compile_definitions(app PRIVATE CONFIG_CELL_COUNT=3)

# glibc gates strnlen() etc. behind _POSIX_C_SOURCE on the native_sim host
# build — the production STM32 picolibc exposes them unconditionally.
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)
