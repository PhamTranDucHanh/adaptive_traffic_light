cmake_minimum_required(VERSION 3.16)

project(
    traffic_timing_decision
    VERSION 1.0
    LANGUAGES CXX
)

############################################################
# C++
############################################################

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_COMPILE_COMMAND ON)

############################################################
# Compile Warnings
############################################################

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic

    )
endif()

add_library(
    traffic_timing_decision_lib

    src/periodic_service.cpp
    src/traffic_data_receiver.cpp
    src/decision_engine.cpp
    src/timing_plan_publisher.cpp
    src/health_reporter.cpp
)

target_include_directories(
    traffic_timing_decision_lib
    PUBLIC
    include
)

add_executable(
    decision_demo
    src/decision_demo.cpp
)

target_link_libraries(
    decision_demo
    PRIVATE
    traffic_timing_decision_lib
)