cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_divecan_ppo2_tx)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# Link the REAL PPO2 broadcast thread against lightweight stubs for its TX
# composers, calibration query, and error reporting, plus a test-local
# chan_consensus. This isolates the thread's read-failure ("fail loud") path
# without pulling in the full CAN/settings/calibration graph.
target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/divecan/divecan_ppo2_math.c
    ${APP_SRC}/divecan/divecan_ppo2_tx.c
)
target_include_directories(app PRIVATE
    ${APP_SRC}/divecan/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Exercise the production thread's opt-in two-cell broadcast transformation.
# The production Kconfig dependency is validated by the real variant builds;
# this isolated test app defines the feature directly because it intentionally
# does not import the complete product Kconfig topology.
target_compile_definitions(app PRIVATE
    CONFIG_DIVECAN_PPO2_SLOT_3_CONSENSUS=1
    CONFIG_HAS_PRESSURE_TRANSDUCER=1
    CONFIG_O2_TRANSDUCER_CHANNEL=0
    CONFIG_DIL_TRANSDUCER_CHANNEL=1
)
