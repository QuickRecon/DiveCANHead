cmake_minimum_required(VERSION 3.20.0)

# Wrap the production slot0/slot1 flash_area APIs and bootutil so the
# test runs entirely against in-RAM mock buffers. boot_request_upgrade
# is wrapped to count invocations; sys_reboot's wrap longjmps back to
# the test body so the host process doesn't exit.
add_link_options(
    -Wl,--wrap=flash_area_open
    -Wl,--wrap=flash_area_close
    -Wl,--wrap=flash_area_read
    -Wl,--wrap=flash_area_write
    -Wl,--wrap=flash_area_erase
    -Wl,--wrap=boot_request_upgrade
    -Wl,--wrap=sys_reboot
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_factory_image)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/factory_image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/heartbeat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
