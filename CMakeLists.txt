cmake_minimum_required(VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(cvserve VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

find_package(
    onnxruntime REQUIRED
    PATHS /opt/homebrew/opt/onnxruntime/lib/cmake/onnxruntime
)

add_executable(cvserve
    src/main.cpp
    src/TCPServer.cpp
    src/Router.cpp
    src/ThreadPool.cpp
    src/YOLODetector.cpp
)

target_include_directories(
    cvserve
    PRIVATE
    include
)

target_link_libraries(
    cvserve
    PRIVATE
    ${OpenCV_LIBS}
    onnxruntime::onnxruntime
)