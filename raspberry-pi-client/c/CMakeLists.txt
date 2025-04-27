cmake_minimum_required(VERSION 3.20)
project(raspberrypiclient  C)

# Set the C standard
set(CMAKE_C_STANDARD 11)

configure_file(${CMAKE_SOURCE_DIR}/.env ${CMAKE_BINARY_DIR}/.env COPYONLY)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")

find_library(PIGPIO_LIBRARY pigpio REQUIRED)

include_directories(/opt/homebrew/include /usr/include client/c/libs/env)
link_directories(/opt/homebrew/lib /usr/lib client/c/libs/env)

# Add the executable
add_executable(raspberrypiclient main.c websocket.h websocket.c rc-car.c rc-car.h libs/env/dotenv.c libs/env/dotenv.h)

# Link the libwebsockets library
target_link_libraries(raspberrypiclient PRIVATE ${PIGPIO_LIBRARY} pthread websockets ssl crypto cjson m libgps-dev)
