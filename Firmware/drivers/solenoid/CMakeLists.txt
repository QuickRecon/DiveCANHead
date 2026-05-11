zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SOLENOID solenoid.c)
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
