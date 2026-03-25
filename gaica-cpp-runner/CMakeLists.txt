cmake_minimum_required(VERSION 3.16)
project(gaica-cpp-runner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows optimizations
if(MSVC)
    add_compile_options(/O2 /W4 /std:c++17)
else()
    add_compile_options(-O3 -Wall -Wextra)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/game_simulator.cpp
    src/bot_manager.cpp
    src/socket_server.cpp
    src/json_utils.cpp
    src/logger.cpp
    src/web_server.cpp
)

set(HEADERS
    include/game_simulator.h
    include/bot_manager.h
    include/socket_server.h
    include/json_utils.h
    include/logger.h
    include/types.h
    include/web_server.h
)

# Main executable
add_executable(runner ${SOURCES} ${HEADERS})
target_include_directories(runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Threading
find_package(Threads REQUIRED)
target_link_libraries(runner PRIVATE Threads::Threads)

# Windows socket libraries
if(WIN32)
    target_link_libraries(runner PRIVATE ws2_32 wsock32)
endif()