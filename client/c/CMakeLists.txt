cmake_minimum_required(VERSION 3.20)
project(rccarclient C)

# Set the C standard
set(CMAKE_C_STANDARD 11)

include_directories(/opt/homebrew/include /usr/include)
link_directories(/opt/homebrew/lib /usr/lib)

# Add the executable
add_executable(rccarclient main.c dualshock.h dualshock.c websocket.h websocket.c rc-car.h rc-car.c)

# Link the libwebsockets library
target_link_libraries(rccarclient websockets ssl crypto SDL2 cjson)
