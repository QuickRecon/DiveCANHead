cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_parsers)

# We compile only the parse-function source files, not the full cell modules
# (which have K_THREAD_DEFINE and hardware dependencies). The parse functions
# call OP_ERROR macros, so we need zbus + errors infrastructure.
target_sources(app PRIVATE
    src/test_diveo2.c
    src/test_o2s.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_diveo2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_o2s.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_channels.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
