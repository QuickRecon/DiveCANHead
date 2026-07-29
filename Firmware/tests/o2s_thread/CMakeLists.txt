cmake_minimum_required(VERSION 3.20.0)

# Include the app tree as a DTS_ROOT so dts/bindings/vendor-prefixes.txt
# (registering the 'test' stub vendor prefix) is picked up.
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_o2s_thread)

# Unlike tests/parsers (which links oxygen_cell_o2s.c only for its pure parse
# helpers and lets the never-started thread body get dead-code-stripped), this
# module sets CONFIG_CELL_1_TYPE_O2S so the K_THREAD_DEFINE spawns a real
# O2S cell thread. That exercises the private static state machine
# (setup/send/broadcast/process_rx/load_cal/uart_callback) which is otherwise
# unreachable. It therefore needs every symbol the thread body references at
# link time:
#   - oxygen_cell_channels.c : chan_cell_1, chan_cal_response
#   - oxygen_cell_math.c     : ppo2_centibar_to_wire
#   - errors.c               : OP_ERROR*, zbus_pub_checked
#   - heartbeat.c            : heartbeat_register / kick
# The O2S driver reads its calibration coefficient straight from
# settings_runtime_get("cal/cellN") (a test-registered handler), so unlike
# the DiveO2 module it does NOT need runtime_settings.c.
# power_get_vbus_voltage() and the POWER_DEVICE binding come from stub_power.c
# (NOT src/power_management.c, whose board-specific DT deps we don't want).
target_sources(app PRIVATE
    src/test_o2s_thread.c
    src/stub_power.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_o2s.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_channels.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/heartbeat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
