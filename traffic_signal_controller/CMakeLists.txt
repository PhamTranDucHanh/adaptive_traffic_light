cmake_minimum_required(VERSION 3.16)

project(TrafficSignalSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
    traffic_signal_control_lib

    # Analytics Service
    src/analytics_service/alert_reporter.cpp
    src/analytics_service/analytics.cpp
    src/analytics_service/dashboard.cpp
    src/analytics_service/event_logger.cpp
    src/analytics_service/output_simulator.cpp
    src/analytics_service/performance_report.cpp

    # Common
    src/common/health_reporter.cpp

    # Traffic Signal Controller
    src/traffic_signal_controller/plan_receiver.cpp
    src/traffic_signal_controller/publisher_collector.cpp
    src/traffic_signal_controller/signal_fsm_engine.cpp
    src/traffic_signal_controller/timer.cpp
)

target_include_directories(
    traffic_signal_control_lib
    PUBLIC
        include
)

# ==========================================================
# Demo executable (for standalone testing)
# ==========================================================

add_executable(
    traffic_signal_controller_demo
    src/traffic_signal_controller_demo.cpp
)

target_link_libraries(
    traffic_signal_controller_demo
    PRIVATE
        traffic_signal_control_lib
)

