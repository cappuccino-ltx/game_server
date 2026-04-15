


find_package(Boost REQUIRED COMPONENTS system)


add_executable(generate generate.cc ${CMAKE_SOURCE_DIR}/build/odb/user-odb.cxx)

target_include_directories(generate PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/build/odb/
)


target_link_libraries(generate PRIVATE
    # odb
    leveldb odb odb-mysql 
)