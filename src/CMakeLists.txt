idf_component_register(SRCS "main.cpp" "sim_link.cpp" "../commands/block.cpp" "../sensors/alarm.cpp"
                       INCLUDE_DIRS "../include"
                       REQUIRES esp_driver_gpio esp_driver_uart esp_timer)
