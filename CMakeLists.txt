cmake_minimum_required(VERSION 3.16)

set(RS485MOTION_SOURCES
    "src/Units.cpp"
    "src/profiles/ess_rs/Registers.cpp")

# ESP-IDF component consumption requires no Arduino or FieldCore dependency.
if(COMMAND idf_component_register)
    idf_component_register(SRCS ${RS485MOTION_SOURCES} INCLUDE_DIRS "include")
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_11)
    return()
endif()

project(RS485Motion LANGUAGES CXX)
add_library(RS485Motion ${RS485MOTION_SOURCES})
add_library(RS485Motion::RS485Motion ALIAS RS485Motion)
target_include_directories(RS485Motion PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(RS485Motion PUBLIC cxx_std_11)
set_target_properties(RS485Motion PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(RS485Motion PRIVATE /W4)
else()
    target_compile_options(RS485Motion PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/units_preview/main.cpp")
        add_executable(units_preview examples/units_preview/main.cpp)
        target_link_libraries(units_preview PRIVATE RS485Motion)
    endif()
    include(CTest)
    # Distributed source packages contain the core; repository tests stay here.
    if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/units_test.cpp")
        foreach(suite units registers)
            add_executable(${suite}_test "test/${suite}_test.cpp")
            target_link_libraries(${suite}_test PRIVATE RS485Motion)
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
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME version_generated COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version.py check)
            add_test(NAME registers_generated COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_ess_registers.py --check)
        endif()
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS RS485Motion EXPORT RS485MotionTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/RS485Motion DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT RS485MotionTargets FILE RS485MotionConfig.cmake
    NAMESPACE RS485Motion:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RS485Motion)
