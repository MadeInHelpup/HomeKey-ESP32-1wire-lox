# Loxone 1-Wire bridge: emulates a DS1990A iButton on a GPIO after a successful
# HomeKey tap. Self-contained, it has no dependency on the application component.
# Enable with CONFIG_LOXONE_ONEWIRE (see Kconfig).
idf_component_register(
    SRCS "src/settings.cpp" "src/onewire_slave.cpp" "src/http.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_driver_gpio esp_timer
    PRIV_REQUIRES esp_rom nvs_flash json)
