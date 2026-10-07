set(EXAMPLES "${CMAKE_CURRENT_LIST_DIR}/../..")
idf_component_register(
    SRCS "main.cpp" "IdfPlatform.cpp"
        "${EXAMPLES}/probe_cli/Esp32UsbConsole.cpp"
        "${EXAMPLES}/probe_cli/ProbeApp.cpp"
        "${EXAMPLES}/probe_cli/ProbeConsole.cpp"
        "${EXAMPLES}/probe_cli/ConsoleText.cpp"
        "${EXAMPLES}/probe_cli/Esp32Load.cpp"
        "${EXAMPLES}/common/RtuRunner.cpp"
        "${EXAMPLES}/common/RtuBusOwner.cpp"
        "${EXAMPLES}/common/EssRtuValidator.cpp"
        "${EXAMPLES}/common/Esp32S3Uart.cpp"
    INCLUDE_DIRS "${EXAMPLES}/probe_cli" "${EXAMPLES}/common"
    REQUIRES MotorControlRS esp_driver_uart esp_driver_gpio esp_driver_gptimer
        esp_driver_usb_serial_jtag esp_timer esp_hw_support esp_psram spi_flash espcoredump)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MOTORCONTROLRS_LOAD_FIXTURE=1 MOTORCONTROLRS_TIMER_CAPTURE=1)
