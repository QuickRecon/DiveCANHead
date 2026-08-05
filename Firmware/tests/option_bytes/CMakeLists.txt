cmake_minimum_required(VERSION 3.20.0)

# boot_is_img_confirmed is wrapped so the test can drive the MCUBoot
# test-mode gate without linking the DFU subsystem.
add_link_options(-Wl,--wrap=boot_is_img_confirmed)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_option_bytes)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/option_bytes.c
)

# The mocks directory MUST come first: it shadows stm32l4xx_hal*.h so the
# production source compiles unmodified on native_sim.
target_include_directories(app BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

# CONFIG_BOOTLOADER_MCUBOOT is not selectable on native_sim, but the
# test-mode deferral branch in option_bytes.c is guarded on it and is one of
# the behaviours most worth covering (an OB_Launch reset during MCUBoot's test
# window makes it revert the update). Define it for this build only;
# boot_is_img_confirmed is --wrap'd above so no DFU code is linked.
target_compile_definitions(app PRIVATE CONFIG_BOOTLOADER_MCUBOOT=1)
