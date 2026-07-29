cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_uds_log_transport)

# Exercises the real uds_log_download.c state machine (RoutineControl selectors
# and the 0x34/0x36/0x37 reader path) against a real FCB on the native_sim flash
# simulator. The reader stack (flash_log_reader.c + flash_log_index.c +
# maintenance_arena.c) is linked for real; the producer side (flash_log.c, zbus,
# threads) is NOT — main.c stubs flash_log_internal_get_fcb()/sector_count()/
# index_epoch() to hand the reader a test-controlled FCB and seeds entries the
# same way the writer does.
#
# uds.c is deliberately NOT linked either: uds_log_download.c only reaches uds.c
# through UDS_SendResponse()/UDS_SendNegativeResponse(), so main.c provides
# recording stubs for those two and captures the assembled response directly —
# no ISOTP_Send wrap needed. flash_log_pause/resume, UDS_LogPush_SetSuspended
# and op_error_publish are likewise stubbed.
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/uds/uds_log_download.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log/flash_log_reader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log/flash_log_index.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/maintenance_arena.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)
