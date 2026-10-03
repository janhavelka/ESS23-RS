cmake_minimum_required(VERSION 3.16)

set(MOTORCONTROLRS_SOURCES
    "src/Units.cpp"
    "src/profiles/ess_rs/Registers.cpp"
    "src/profiles/ess_rs/Codec.cpp")

# ESP-IDF component consumption requires no Arduino or FieldCore dependency.
if(COMMAND idf_component_register)
    idf_component_register(SRCS ${MOTORCONTROLRS_SOURCES} INCLUDE_DIRS "include")
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
        foreach(suite units registers codec)
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
        if(TARGET units_preview)
            add_test(NAME units_preview COMMAND units_preview)
        endif()
        find_package(Python3 3.10 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME version_generated COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version.py check)
            add_test(NAME registers_generated COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_ess_registers.py --check)
            add_test(NAME register_gaps COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test/generator_test.py)
        endif()
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS MotorControlRS EXPORT MotorControlRSTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/MotorControlRS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT MotorControlRSTargets FILE MotorControlRSConfig.cmake
    NAMESPACE MotorControlRS:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MotorControlRS)
