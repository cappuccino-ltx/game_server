

# sub directory
# add_subdirectory(proto)

find_package(Boost REQUIRED COMPONENTS system)

file(GLOB_RECURSE gateway_src ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)

add_executable(gateway ${gateway_src})

target_include_directories(gateway PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(gateway PRIVATE ${CMAKE_SOURCE_DIR}/common)


target_link_libraries(gateway PRIVATE 
    Boost::system
    akcp
    lockfreequeue
    proto_obj
)
