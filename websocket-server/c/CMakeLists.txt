cmake_minimum_required(VERSION 3.20)
project(rccarwebsockecketserver C)

# Set the C standard
set(CMAKE_C_STANDARD 11)

include_directories(/opt/homebrew/include /usr/include)
link_directories(/opt/homebrew/lib /usr/lib)

# Add the executable
add_executable(rccarwebsockecketserver main.c)

# Link the libwebsockets library
target_link_libraries(rccarwebsockecketserver websockets ssl crypto SDL2 cjson)
