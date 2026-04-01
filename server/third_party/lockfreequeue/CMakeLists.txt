
# third_party/lockfreequeue/CMakeLists.txt

add_library(lockfreequeue INTERFACE)

target_include_directories(lockfreequeue INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)