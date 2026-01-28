# Define los ficheros fuente (.c) del componente.
set(COMPONENT_SRCS "bme68x.c")

# Define los directorios de cabeceras (.h) que deben ser públicos para otros componentes.
set(COMPONENT_ADD_INCLUDEDIRS ".")

# Registra el componente.
idf_component_register(SRCS "${COMPONENT_SRCS}"
                       INCLUDE_DIRS "${COMPONENT_ADD_INCLUDEDIRS}")