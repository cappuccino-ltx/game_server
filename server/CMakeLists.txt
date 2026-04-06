

cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(game_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)


include_directories(${CMAKE_CURRENT_LIST_DIR}/odb) # 全局配置
add_subdirectory(proto)
add_subdirectory(third_party)
add_subdirectory(gateway)
add_subdirectory(login_server)

# add_subdirectory(battle)
# add_subdirectory(test)
