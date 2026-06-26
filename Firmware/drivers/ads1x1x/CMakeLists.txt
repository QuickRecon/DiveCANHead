if(CONFIG_ADC_ADS1X1X_LOCAL)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/adc_ads1x1x_local.c)
  # adc_context.h lives in the Zephyr ADC driver dir; the fork includes it the
  # same way the upstream driver does ("adc_context.h").
  target_include_directories(app PRIVATE ${ZEPHYR_BASE}/drivers/adc)
endif()
