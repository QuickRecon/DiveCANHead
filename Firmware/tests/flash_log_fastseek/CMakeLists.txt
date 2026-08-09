cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flash_log_fastseek)

# Cursor-parity proof for the bulk-read FCB seek: the SAME flash image is
# mounted twice — once with the stock fcb_init entry walk, once with
# FCB_FLAGS_INIT_SKIP_WALK + fl_fast_seek_active() — and the resulting write
# cursors must be bit-identical, across adversarial rings (2-byte lengths,
# torn garbage tails, chunk-boundary entries) and pathological scratch sizes.
# This is the permanent regression guard for the two cursor bugs the parser
# shipped with (terminator chunk-progress loss; stall-instead-of-hop).
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log/flash_log_fastseek.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log
)
