cmake_minimum_required(VERSION 3.16)

set(MOTORCONTROLRS_SOURCES
    "src/Units.cpp"
    "src/Axis.cpp"
    "src/Traffic.cpp"
    "src/Discovery.cpp"
    "src/profiles/ess_rs/Traffic.cpp"
    "src/profiles/ess_rs/Registers.cpp"
    "src/profiles/ess_rs/Codec.cpp"
    "src/profiles/ess_rs/Reads.cpp"
    "src/profiles/ess_rs/Actions.cpp"
    "src/profiles/ess_rs/Position.cpp"
    "src/profiles/ess_rs/Velocity.cpp"
    "src/profiles/ess_rs/DriverSettings.cpp"
    "src/profiles/ess_rs/Segments.cpp"
    "src/profiles/ess_rs/ControlSettings.cpp"
    "src/profiles/ess_rs/Tuning.cpp"
    "src/profiles/ess_rs/Homing.cpp"
    "src/profiles/ess_rs/Communication.cpp"
    "src/profiles/ess_rs/Persistence.cpp")

# ESP-IDF component consumption requires no Arduino or FieldCore dependency.
if(COMMAND idf_component_register)
    list(TRANSFORM MOTORCONTROLRS_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
    idf_component_register(SRCS ${MOTORCONTROLRS_SOURCES}
        INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_11)
    return()
endif()

project(MotorControlRS LANGUAGES CXX)
add_library(MotorControlRS ${MOTORCONTROLRS_SOURCES})
add_library(MotorControlRS::MotorControlRS ALIAS MotorControlRS)
target_include_directories(MotorControlRS PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(MotorControlRS PUBLIC cxx_std_11)
set_target_properties(MotorControlRS PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(MotorControlRS PRIVATE /W4)
else()
    target_compile_options(MotorControlRS PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/units_preview/main.cpp")
        add_executable(units_preview examples/units_preview/main.cpp)
        target_link_libraries(units_preview PRIVATE MotorControlRS)
    endif()
    include(CTest)
    # Distributed source packages contain the core; repository tests stay here.
    if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/units_test.cpp")
        foreach(suite discovery discovery_app discovery_console persistence persistence_console persistence_app traffic motion_profile_app communication communication_console communication_app commissioning_owner units axis registers codec reads actions position velocity homing driver_settings segments control_settings control_app tuning tuning_app host_app host_console io io_app driver_app home_app runner bus_owner bus_scheduling probe_console esp32_uart probe_app probe_app_monitor_control move_app velocity_app capture_service)
            add_executable(${suite}_test "test/${suite}_test.cpp")
            target_link_libraries(${suite}_test PRIVATE MotorControlRS)
            set_target_properties(${suite}_test PROPERTIES
                CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
            # These small standalone tests use assert; keep them active in Release.
            if(MSVC)
                target_compile_options(${suite}_test PRIVATE /UNDEBUG)
            else()
                target_compile_options(${suite}_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
            endif()
            add_test(NAME ${suite} COMMAND ${suite}_test)
        endforeach()
        # Link the full codec fixture without the descriptive register catalogue.
        add_executable(codec_independent_test test/codec_test.cpp src/profiles/ess_rs/Codec.cpp)
        target_include_directories(codec_independent_test PRIVATE include)
        set_target_properties(codec_independent_test PROPERTIES
            CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
        if(MSVC)
            target_compile_options(codec_independent_test PRIVATE /UNDEBUG /W4)
        else()
            target_compile_options(codec_independent_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME codec_independent COMMAND codec_independent_test)
        target_sources(traffic_test PRIVATE examples/common/RtuRunner.cpp)
        target_sources(runner_test PRIVATE examples/common/RtuRunner.cpp)
        target_sources(bus_owner_test PRIVATE examples/common/RtuRunner.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(bus_scheduling_test PRIVATE examples/common/RtuRunner.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(probe_console_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuRunner.cpp)
        target_sources(host_console_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuRunner.cpp)
        target_sources(communication_console_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuRunner.cpp)
        target_sources(discovery_console_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuRunner.cpp)
        target_sources(discovery_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(persistence_console_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuRunner.cpp)
        target_sources(persistence_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(communication_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(commissioning_owner_test PRIVATE examples/common/RtuRunner.cpp examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        foreach(suite discovery_app persistence_app motion_profile_app communication_app esp32_uart probe_app probe_app_monitor_control move_app velocity_app driver_app home_app io_app control_app tuning_app host_app capture_service)
            target_sources(${suite}_test PRIVATE examples/common/Esp32S3Uart.cpp examples/common/RtuRunner.cpp
                test/fakes/esp32_uart/FakeEsp.cpp)
            target_include_directories(${suite}_test PRIVATE test/fakes/esp32_uart)
            target_compile_definitions(${suite}_test PRIVATE CONFIG_IDF_TARGET_ESP32S3=1)
        endforeach()
        target_sources(probe_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(probe_app_monitor_control_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(move_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(motion_profile_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(velocity_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(driver_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(control_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(host_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(tuning_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(io_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        target_sources(home_app_test PRIVATE examples/probe_cli/ProbeConsole.cpp
            examples/common/RtuBusOwner.cpp examples/common/EssRtuValidator.cpp)
        add_executable(probe_app_load_test test/probe_app_test.cpp
            examples/probe_cli/ProbeConsole.cpp examples/probe_cli/Esp32Load.cpp
            examples/common/RtuRunner.cpp examples/common/RtuBusOwner.cpp
            examples/common/EssRtuValidator.cpp examples/common/Esp32S3Uart.cpp
            test/fakes/esp32_uart/FakeEsp.cpp)
        target_link_libraries(probe_app_load_test PRIVATE MotorControlRS)
        set_target_properties(probe_app_load_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
        target_include_directories(probe_app_load_test PRIVATE test/fakes/esp32_uart)
        target_compile_definitions(probe_app_load_test PRIVATE CONFIG_IDF_TARGET_ESP32S3=1
            MOTORCONTROLRS_LOAD_FIXTURE=1 MOTORCONTROLRS_TIMER_CAPTURE=1)
        if(MSVC)
            target_compile_options(probe_app_load_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(probe_app_load_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME probe_app_load COMMAND probe_app_load_test)
        add_executable(idf_app_test test/idf_app_test.cpp
            examples/probe_cli/ProbeConsole.cpp examples/probe_cli/Esp32Load.cpp
            examples/common/RtuRunner.cpp examples/common/RtuBusOwner.cpp
            examples/common/EssRtuValidator.cpp examples/common/Esp32S3Uart.cpp
            test/fakes/esp32_uart/FakeEsp.cpp test/fakes/esp32_uart/FakeUsb.cpp)
        target_link_libraries(idf_app_test PRIVATE MotorControlRS)
        target_include_directories(idf_app_test PRIVATE test/fakes/esp32_uart examples/probe_cli)
        target_compile_definitions(idf_app_test PRIVATE CONFIG_IDF_TARGET_ESP32S3=1
            MOTORCONTROLRS_LOAD_FIXTURE=1 MOTORCONTROLRS_TIMER_CAPTURE=1)
        set_target_properties(idf_app_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
        if(MSVC)
            target_compile_options(idf_app_test PRIVATE /UNDEBUG /W4)
        else()
            target_compile_options(idf_app_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME idf_app COMMAND idf_app_test)
        # Reuse identical operation/failure assertions with each real USB adapter.
        foreach(suite move_app control_app)
            add_executable(idf_${suite}_test test/${suite}_test.cpp
                examples/probe_cli/ProbeConsole.cpp
                examples/common/RtuRunner.cpp examples/common/RtuBusOwner.cpp
                examples/common/EssRtuValidator.cpp examples/common/Esp32S3Uart.cpp
                test/fakes/esp32_uart/FakeEsp.cpp test/fakes/esp32_uart/FakeUsb.cpp)
            target_link_libraries(idf_${suite}_test PRIVATE MotorControlRS)
            target_include_directories(idf_${suite}_test PRIVATE test/fakes/esp32_uart examples/probe_cli)
            target_compile_definitions(idf_${suite}_test PRIVATE CONFIG_IDF_TARGET_ESP32S3=1 MOTORCONTROLRS_TEST_IDF=1)
            set_target_properties(idf_${suite}_test PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
            if(MSVC)
                target_compile_options(idf_${suite}_test PRIVATE /UNDEBUG /W4)
            else()
                target_compile_options(idf_${suite}_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
            endif()
            add_test(NAME idf_${suite} COMMAND idf_${suite}_test)
        endforeach()
        if(TARGET units_preview)
            add_test(NAME units_preview COMMAND units_preview)
        endif()
        find_package(Python3 3.10 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME idf_config COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/idf_config_test.py
                ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_ID})
            add_test(NAME bench_scenarios COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/bench_scenarios_test.py)
            add_test(NAME motion_campaign COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/motion_campaign_test.py)
            add_test(NAME communication_harness COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/communication_harness_test.py)
            add_test(NAME discovery_harness COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/discovery_harness_test.py $<TARGET_FILE:discovery_console_test>)
            add_test(NAME persistence_harness COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/persistence_harness_test.py)
            add_test(NAME version_generated COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version.py check)
            add_test(NAME registers_generated COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_ess_registers.py --check)
            add_test(NAME register_gaps COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/generator_test.py)
            add_test(NAME bench_probe COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/bench_probe_test.py)
            add_test(NAME velocity_console_parity COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/velocity_console_parity_test.py
                $<TARGET_FILE:probe_console_test>)
            add_test(NAME io_transport COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/io_transport_test.py)
            add_test(NAME segment_console_parity COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/segment_console_parity_test.py $<TARGET_FILE:probe_console_test>)
            add_test(NAME control_console_parity COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/control_console_parity_test.py
                $<TARGET_FILE:probe_console_test>)
            add_test(NAME tuning_console_parity COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/tuning_console_parity_test.py
                $<TARGET_FILE:probe_console_test>)
            add_test(NAME io_console_parity COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/io_console_parity_test.py
                $<TARGET_FILE:probe_console_test>)
            add_test(NAME driver_console_parity COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/driver_console_parity_test.py
                $<TARGET_FILE:probe_console_test>)
            add_test(NAME home_console_parity COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/home_console_parity_test.py
                $<TARGET_FILE:probe_console_test>)
            add_test(NAME operations_inventory COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_ess_operations.py)
            add_test(NAME operations_inventory_failures COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/operations_inventory_test.py)
        endif()
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS MotorControlRS EXPORT MotorControlRSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/MotorControlRS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT MotorControlRSTargets FILE MotorControlRSConfig.cmake
    NAMESPACE MotorControlRS:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MotorControlRS)
