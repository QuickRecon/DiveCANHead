cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_poseidon_accessories)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_compile_definitions(app PRIVATE
    CONFIG_POSEIDON_DS2782_SHUNT_UOHM=40000
)
