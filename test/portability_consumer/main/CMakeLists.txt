set(EXAMPLES "${CMAKE_CURRENT_LIST_DIR}/../../../examples")
idf_component_register(
    SRCS "../main.cpp"
        "${EXAMPLES}/common/RtuRunner.cpp"
        "${EXAMPLES}/common/RtuBusOwner.cpp"
        "${EXAMPLES}/common/EssRtuValidator.cpp"
        "${EXAMPLES}/probe_cli/ProbeConsole.cpp"
    REQUIRES MotorControlRS)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_11)
