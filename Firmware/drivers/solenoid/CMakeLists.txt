if(CONFIG_SOLENOID)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/solenoid.c)
endif()
