


find_package(Boost REQUIRED COMPONENTS system)


add_executable(generate generate.cc ${CMAKE_SOURCE_DIR}/build/odb/user-odb.cxx)

add_executable(auto auto.cc)
add_executable(manual manual.cc)

target_include_directories(generate PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/build/odb/
)
target_include_directories(auto PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/build/odb/
)
target_include_directories(manual PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/build/odb/
)


target_link_libraries(generate PRIVATE
    # odb
    leveldb odb odb-mysql 
)
target_link_libraries(auto PRIVATE
    Boost::system
    akcp
    lockfreequeue
    proto_obj
)
target_link_libraries(manual PRIVATE
    Boost::system
    akcp
    lockfreequeue
    proto_obj
)