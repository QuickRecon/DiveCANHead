cmake_minimum_required(VERSION 3.20.0)

# --wrap the bootutil entry points and the F-section counter accessors
# so the test fixture can drive the POST state machine deterministically.
# sys_reboot is wrapped so a POST failure path doesn't actually exit the
# host process — the wrap longjmps back out to the test body.
add_link_options(
    -Wl,--wrap=boot_write_img_confirmed
    -Wl,--wrap=sys_reboot
    -Wl,--wrap=divecan_send_get_tx_count
    -Wl,--wrap=divecan_rx_get_bus_init_count
    -Wl,--wrap=divecan_rx_get_bus_id_count
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_firmware_confirm)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/firmware_confirm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_channels.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

# CONFIG_CELL_COUNT lives in src/Kconfig's product-topology menu, which we
# don't source into this test build. oxygen_cell_channels.c sees the
# symbol via #if CONFIG_CELL_COUNT >= N — supply it here so the channel
# definitions for cells 2 and 3 are included.
target_compile_definitions(app PRIVATE CONFIG_CELL_COUNT=3)

