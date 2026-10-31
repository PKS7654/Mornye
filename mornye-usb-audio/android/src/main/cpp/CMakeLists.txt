cmake_minimum_required(VERSION 3.18.1)

project("mornye_usb_audio" CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags for real-time audio
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fvisibility=hidden -ffast-math")

include_directories(
    include
)

add_library(
    mornye_usb_audio
    SHARED
    uac_parser.cpp
    uac_driver.cpp
    jni_bridge.cpp
)

find_library(log-lib log)

target_link_libraries(
    mornye_usb_audio
    ${log-lib}
)
