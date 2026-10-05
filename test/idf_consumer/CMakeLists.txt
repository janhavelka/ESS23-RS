cmake_minimum_required(VERSION 3.16)

# The caller stages only the installed core's CMakeLists.txt/include/src in a
# directory named MotorControlRS. This application never sees example/vendor
# include paths and never imports the standalone firmware component.
if(NOT MOTORCONTROLRS_COMPONENT_PATH)
    message(FATAL_ERROR "Set MOTORCONTROLRS_COMPONENT_PATH to the staged MotorControlRS core directory")
endif()
get_filename_component(MOTORCONTROLRS_COMPONENT_PATH "${MOTORCONTROLRS_COMPONENT_PATH}" ABSOLUTE)
foreach(required CMakeLists.txt include/MotorControlRS src)
    if(NOT EXISTS "${MOTORCONTROLRS_COMPONENT_PATH}/${required}")
        message(FATAL_ERROR "The staged core is missing ${required}")
    endif()
endforeach()
set(EXTRA_COMPONENT_DIRS "${MOTORCONTROLRS_COMPONENT_PATH}")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(motorcontrol_core_consumer)
