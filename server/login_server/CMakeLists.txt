


# 生成odb文件
set(odb_file_list "")
set(odbfiles "user.hh")
set(odb_path ${CMAKE_SOURCE_DIR}/odb)
set(compile_output_path ${CMAKE_BINARY_DIR}/odb)
file(MAKE_DIRECTORY ${compile_output_path})
find_package(Boost REQUIRED COMPONENTS system)

foreach(file ${odbfiles})

    string(REPLACE ".hh" "-odb.hxx" hfile ${file}) 
    string(REPLACE ".hh" "-odb.cxx" sfile ${file}) 

    set(input_file ${odb_path}/${file})
    set(output_cxx ${compile_output_path}/${sfile})
    set(output_hxx ${compile_output_path}/${hfile})

    add_custom_command(
        OUTPUT ${output_cxx} ${output_hxx}
        COMMAND odb
            -d mysql
            --std c++11
            --generate-query
            --generate-schema
            --profile boost/date-time
            --output-dir ${compile_output_path}
            ${input_file}
        DEPENDS ${input_file}
        COMMENT "Generating ODB for ${file}"
    )

    list(APPEND odb_file_list ${output_cxx})

endforeach()

file(GLOB_RECURSE login_src ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

add_executable(login ${login_src} ${odb_file_list})

target_include_directories(login PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${compile_output_path}  
)


target_link_libraries(login PRIVATE
    # odb
    leveldb odb odb-mysql 
    # redis
    -lhiredis -lredis++
    # proto file
    proto_obj
    # etcd-cpp-api
    etcd-cpp-api cpprest 
    #boost
    Boost::system
    # lock free
    lockfreequeue
)