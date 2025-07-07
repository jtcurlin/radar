cmake_minimum_required(VERSION 3.20)
project(radar_app)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

enable_language(OBJCXX)

find_library(APPLE_FWK_FOUNDATION Foundation REQUIRED)
find_library(APPLE_FWK_APPKIT AppKit REQUIRED)
find_library(APPLE_FWK_QUARTZ_CORE QuartzCore REQUIRED)
find_library(APPLE_FWK_METAL Metal REQUIRED)

add_executable(radar_app MACOSX_BUNDLE
        src/main.mm
)

set_target_properties(radar_app PROPERTIES
  MAXOSC_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
)

target_sources(radar_app
        PRIVATE ${CMAKE_SOURCE_DIR}/src/controller.cpp
                ${CMAKE_SOURCE_DIR}/src/platform.mm
                ${CMAKE_SOURCE_DIR}/src/radar.cpp
                ${CMAKE_SOURCE_DIR}/src/renderer.mm
                ${CMAKE_SOURCE_DIR}/src/serial.cpp
                ${CMAKE_SOURCE_DIR}/src/udp_client.cpp
                
        PUBLIC  include/controller.hpp
                include/platform.hpp
                include/radar.hpp
                include/renderer.hpp
                include/serial.hpp
                include/udp_client.hpp
)

target_include_directories(radar_app
        PRIVATE ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(radar_app
        ${APPLE_FWK_FOUNDATION}
        ${APPLE_FWK_QUARTZ_CORE}
        ${APPLE_FWK_METAL}
        ${APPLE_FWK_APPKIT}
)
