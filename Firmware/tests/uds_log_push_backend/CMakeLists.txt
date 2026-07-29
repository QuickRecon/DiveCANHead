cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_uds_log_push_backend)

# Exercises the real uds_log_push_backend.c Zephyr log-backend adapter. The
# backend is auto-registered (LOG_BACKEND_DEFINE autostart=true) and driven by
# emitting LOG_x messages from helper modules and pumping the deferred log
# queue with log_process() (CONFIG_LOG_PROCESS_THREAD=n keeps it deterministic).
#
# The two collaborators the backend reaches out to are stubbed in main.c:
#   - UDS_LogPush_SendLogMessage()  -> captures the pushed line
#   - flash_log_get_rtt_level()     -> the test-controlled severity threshold
#
# Helper TUs register distinct LOG modules so the backend's source-ID logic
# (self-drop list, force-list elevation) can be steered:
#   - main.c            registers "uds_log_push"   (a self-drop name)
#   - helper_forced.c   registers "test_forced_mod" (on the force list)
#   - helper_normal.c   registers "test_normal_mod" (ordinary module)
target_sources(app PRIVATE
    src/main.c
    src/helper_forced.c
    src/helper_normal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/uds/uds_log_push_backend.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)
