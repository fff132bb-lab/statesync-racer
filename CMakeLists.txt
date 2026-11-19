cmake_minimum_required(VERSION 3.16)
project(statesync_racer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(racer_server src/server/main.cpp)
add_executable(racer_client src/client/main.cpp)
add_executable(racer_replay src/tools/replay.cpp)

foreach(target racer_server racer_client racer_replay)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
endforeach()
