cmake_minimum_required(VERSION 3.20)
project(websocketserver C)

# Set the C standard
set(CMAKE_C_STANDARD 11)

include_directories(/opt/homebrew/include /usr/include /usr/local/include)
link_directories(/opt/homebrew/lib /usr/lib /usr/local/lib)

# Add the executable
add_executable(websocketserver main.c)

# Link the libwebsockets library
target_link_libraries(websocketserver websockets ssl crypto cjson)
