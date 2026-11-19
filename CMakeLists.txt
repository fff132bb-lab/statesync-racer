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
add_executable(sim_test tests/sim_test.cpp)

foreach(target racer_server racer_client racer_replay sim_test)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
endforeach()

# Optional raylib viewer. raylib is fetched and built from source, so this
# needs the usual X11/OpenGL development packages.
option(RACER_VIEWER "Build the raylib viewer (racer_viewer)" OFF)
if(RACER_VIEWER)
  enable_language(C)
  include(FetchContent)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
  FetchContent_MakeAvailable(raylib)
  add_executable(racer_viewer src/viewer/main.cpp)
  target_link_libraries(racer_viewer PRIVATE raylib)
  target_compile_options(racer_viewer PRIVATE -Wall -Wextra)
endif()
