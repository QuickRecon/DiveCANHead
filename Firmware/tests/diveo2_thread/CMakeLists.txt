cmake_minimum_required(VERSION 3.20.0)

# Include the app tree as a DTS_ROOT so dts/bindings/vendor-prefixes.txt
# (registering the 'test' stub vendor prefix) is picked up.
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_diveo2_thread)

# Unlike tests/parsers (which links oxygen_cell_diveo2.c only for its pure parse
# helpers and lets the never-started thread body get dead-code-stripped), this
# module sets CONFIG_CELL_1_TYPE_DIVEO2 so the K_THREAD_DEFINE spawns a real
# diveo2 cell thread. That exercises the private static state machine
# (setup/detect/send/broadcast/process_rx/...) which is otherwise unreachable.
# It therefore needs every symbol the thread body references at link time:
#   - oxygen_cell_channels.c : chan_cell_1, chan_cal_response
#   - oxygen_cell_math.c     : ppo2_centibar_to_wire
#   - runtime_settings.c     : runtime_settings_get / set_volatile
#   - errors.c               : OP_ERROR*, zbus_pub_checked
#   - heartbeat.c            : heartbeat_register / kick
# power_get_vbus_voltage() and the POWER_DEVICE binding come from stub_power.c
# (NOT src/power_management.c, whose board-specific DT deps we don't want).
target_sources(app PRIVATE
    src/test_diveo2_thread.c
    src/stub_power.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_diveo2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_channels.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/runtime_settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/calibration_store.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/external_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/heartbeat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
