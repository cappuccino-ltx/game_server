

# sub directory
# add_subdirectory(proto)

find_package(Boost REQUIRED COMPONENTS system)

add_executable(tcp_test tcp_test.cc)
add_executable(server_etcd server_etcd.cc)
add_executable(client_etcd client_etcd.cc)

target_include_directories(tcp_test PRIVATE ${CMAKE_SOURCE_DIR}/common)
target_include_directories(server_etcd PRIVATE ${CMAKE_SOURCE_DIR}/common)
target_include_directories(client_etcd PRIVATE ${CMAKE_SOURCE_DIR}/common)

target_link_libraries(tcp_test PRIVATE 
    Boost::system
    akcp
    lockfreequeue
    proto_obj
)

target_link_libraries(server_etcd PRIVATE 
    Boost::system
    akcp
    lockfreequeue
    proto_obj
    etcd-cpp-api cpprest
)

target_link_libraries(client_etcd PRIVATE 
    Boost::system
    akcp
    lockfreequeue
    proto_obj
    etcd-cpp-api cpprest
)
