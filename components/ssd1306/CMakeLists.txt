set(COMPONENT_SRCS "ssd1306.c" "ssd1306_i2c.c" "ssd1306_spi.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")

idf_component_register(SRCS "${COMPONENT_SRCS}"
                       INCLUDE_DIRS "${COMPONENT_ADD_INCLUDEDIRS}"
                       REQUIRES driver)