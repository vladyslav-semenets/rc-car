cmake_minimum_required(VERSION 3.20)
project(raspberrypiclient C)

set(CMAKE_C_STANDARD 11)

# Copy .env file
configure_file(${CMAKE_SOURCE_DIR}/.env ${CMAKE_BINARY_DIR}/.env COPYONLY)

# Add debug and address sanitizer flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")

# Include paths
include_directories(
        /opt/homebrew/include
        /usr/include
        libs/
)

# Linker search paths
link_directories(
        /opt/homebrew/lib
        /usr/lib
        libs/
)

# Source files (common)
set(SOURCES
        main.c
        websocket.h websocket.c
        rc-car.c rc-car.h
        udp.c udp.h
        libs/env/dotenv.c libs/env/dotenv.h
)

# Use real pigpio on Linux, mock pigpio on macOS
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(PIGPIO_LIBRARY pigpio REQUIRED)
    add_executable(raspberrypiclient ${SOURCES})
    target_link_libraries(raspberrypiclient PRIVATE
            ${PIGPIO_LIBRARY}
            pthread websockets ssl crypto cjson m gps
    )
else()  # Assume macOS or other
    message(STATUS "Building with mock pigpio for development environment")
    list(APPEND SOURCES pigpio-mock.c)
    include_directories(${CMAKE_SOURCE_DIR})
    add_executable(raspberrypiclient ${SOURCES})
    target_link_libraries(raspberrypiclient PRIVATE
            pthread websockets ssl crypto cjson m gps
    )
endif()
