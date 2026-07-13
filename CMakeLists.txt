cmake_minimum_required(VERSION 3.16)

project(
    adaptive_traffic_light
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_subdirectory(traffic_perception)
add_subdirectory(traffic_signal_controller)
add_subdirectory(traffic_timing_decision)

add_executable(
    adaptive_traffic_light

    main.cpp
)

target_link_libraries(
    adaptive_traffic_light
    PRIVATE
    traffic_perception_lib
    traffic_signal_control_lib
    traffic_timing_decision_lib
    
)