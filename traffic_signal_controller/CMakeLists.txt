cmake_minimum_required(VERSION 3.16)

project(TrafficSignalSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
    src/main.cpp

    src/analytics_service/alert_reporter.cpp
    src/analytics_service/analytics.cpp
    src/analytics_service/dashboard.cpp
    src/analytics_service/event_logger.cpp
    src/analytics_service/output_simulator.cpp
    src/analytics_service/performance_report.cpp

    src/common/health_reporter.cpp

    src/traffic_signal_controller/plan_receiver.cpp
    src/traffic_signal_controller/publisher_collector.cpp
    src/traffic_signal_controller/signal_fsm_engine.cpp
    src/traffic_signal_controller/timer.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
    )
endif()