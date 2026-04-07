

# sub directory
# add_subdirectory(proto)

find_package(Boost REQUIRED COMPONENTS system)

file(GLOB_RECURSE battle_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

add_executable(battle ${battle_src})

target_include_directories(battle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(battle PRIVATE ${CMAKE_SOURCE_DIR}/common)


target_link_libraries(battle PRIVATE 
    Boost::system
    akcp
    lockfreequeue
    proto_obj
    etcd-cpp-api cpprest # etcd
    hiredis redis++ # redis
)
