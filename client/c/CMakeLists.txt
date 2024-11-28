cmake_minimum_required(VERSION 3.20)
project(rccarclient C)

# Set the C standard
set(CMAKE_C_STANDARD 11)

configure_file(${CMAKE_SOURCE_DIR}/.env ${CMAKE_BINARY_DIR}/.env COPYONLY)

include_directories(/opt/homebrew/include /usr/include client/c/libs/env)
link_directories(/opt/homebrew/lib /usr/lib client/c/libs/env)

# Add the executable
add_executable(rccarclient main.c dualshock.h dualshock.c websocket.h websocket.c rc-car.h rc-car.c libs/env/dotenv.c libs/env/dotenv.h)

# Link the libwebsockets library
target_link_libraries(rccarclient websockets ssl crypto SDL2 cjson)
