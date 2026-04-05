

cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(game_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)

add_subdirectory(proto)
add_subdirectory(third_party)
add_subdirectory(gateway)
# add_subdirectory(battle)
add_subdirectory(test)
