idf_component_register(SRCS "tetris.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_i2c u8g2 u8g2-hal-esp-idf)
