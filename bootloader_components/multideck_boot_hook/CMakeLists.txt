idf_component_register(SRCS "hooks.c"
                       PRIV_REQUIRES bootloader_support esp_rom log)
# The bootloader hooks are weak symbols: this keeps hooks.c in the bootloader link.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_hooks_include")
