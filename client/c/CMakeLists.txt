cmake_minimum_required(VERSION 3.20)
project(rccarclient C)

# Set the C standard
set(CMAKE_C_STANDARD 11)

configure_file(${CMAKE_SOURCE_DIR}/.env ${CMAKE_BINARY_DIR}/.env COPYONLY)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")

include_directories(/opt/homebrew/include /usr/include libs/client/c/utils)
link_directories(/opt/homebrew/lib /usr/lib libs/ client/c/utils)

# Add the executable
add_executable(rccarclient main.c joystick.h joystick.c websocket.h websocket.c udp.h udp.c rc-car.h rc-car.c libs/env/dotenv.c libs/env/dotenv.h utils/joystick.util.h utils/joystick.util.c utils/mavlink.util.c utils/mavlink.util.h)

# Link the libwebsockets library
target_link_libraries(rccarclient websockets ssl crypto SDL2 cjson)
