cmake_minimum_required(VERSION 3.16)
project(traffic_perception LANGUAGES CXX)

# 1. C++ STANDARD CONFIGURATION (Requires C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 2. FIND DEPENDENCIES
find_package(Threads REQUIRED)

find_package(OpenCV REQUIRED) # cv::Mat, cv::VideoCapture, and cv::imshow
if(NOT TARGET OpenCV::OpenCV)
    add_library(OpenCV::OpenCV INTERFACE IMPORTED)
    set_target_properties(OpenCV::OpenCV PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OpenCV_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${OpenCV_LIBS}"
    )
endif()

# 3. TARGET & SOURCE FILES DEFINITION
# Create the submodule as a static library target
add_library(traffic_perception_lib STATIC)

file(GLOB_RECURSE ALL_HEADERS 
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

set(ALL_SOURCES
    # Facade / Entry Point
    src/perception_module.cpp

    # Core Layer
    src/core/config_manager.cpp
    src/core/health_reporter.cpp

    # Ingestion Layer
    src/ingestion/safe_frame_queue.cpp
    src/ingestion/stream_watchdog.cpp
    src/ingestion/stream_worker.cpp

    # Inference Layer
    src/inference/inference_engine.cpp
    src/inference/model_backend.cpp

    # Pipeline Layer
    src/pipeline/traffic_analyzer.cpp
    src/pipeline/snapshot_publisher.cpp
    src/pipeline/pipeline_manager.cpp

    # IO / Sinks Layer
    src/io/multi_lane_viewer.cpp
    src/io/snapshot_sender.cpp
    src/io/telemetry_manager.cpp
)

target_sources(traffic_perception_lib PRIVATE ${ALL_SOURCES})

# 4. INCLUDE DIRECTORIES CONFIGURATION
target_include_directories(traffic_perception_lib
    PUBLIC
        # Allows internal/external components to include headers via <traffic_perception_lib/...>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        # Internal include path for source files within src/
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_precompile_headers(traffic_perception_lib PRIVATE <opencv2/opencv.hpp>)

# 5. TARGET LINK LIBRARIES
target_link_libraries(traffic_perception_lib
    PUBLIC
        OpenCV::OpenCV
        Threads::Threads
    PRIVATE
        # POSIX Message Queue requires linking the 'rt' library on Linux hosts
        $<$<PLATFORM_ID:Linux>:rt>
)

# 6. COMPILER WARNINGS (Enforce strict code compliance)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(traffic_perception_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(module_demo src/module_demo.cpp)
target_link_libraries(module_demo PRIVATE traffic_perception_lib ${OpenCV_LIBS} rt)

add_custom_target(lint
    COMMAND run-clang-tidy-18 -p ${CMAKE_BINARY_DIR} -header-filter=${CMAKE_BINARY_DIR} ${ALL_SOURCES} ${ALL_HEADERS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy on source code and lint test suite..."
)

add_custom_target(format
    COMMAND clang-format -i ${ALL_SOURCES} ${ALL_HEADERS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-format on source code..."
)
 
add_custom_target(check-int
    COMMAND clang-query-18
        -p "${CMAKE_BINARY_DIR}"
        -c "set traversal IgnoreUnlessSpelledInSource"
        -c [[match declaratorDecl(hasType(asString("^(signed )?(char|short|int|long|long long)$|^(unsigned )?(char|short|int|long|long long)$")) , unless(functionDecl(hasName('main'))), isExpansionInFileMatching("src/|include/"))]]
        ${ALL_SOURCES} ${ALL_HEADERS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running clang-query to enforce fixed-width integer types (CPP & Headers)..."
    VERBATIM
)
