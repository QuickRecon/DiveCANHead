if(CONFIG_GPIO_SIM)
    target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gpio_sim.c)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
