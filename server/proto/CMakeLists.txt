

find_package(Protobuf REQUIRED)

# set(PROTO_FILES
#     ${CMAKE_CURRENT_SOURCE_DIR}/*.proto
# )
file(GLOB PROTO_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.proto"
)

# 生成 .pb.cc 和 .pb.h
protobuf_generate(
    LANGUAGE cpp
    PROTOS ${PROTO_FILES}
    OUT_VAR PROTO_SRCS
)

add_library(proto_obj OBJECT ${PROTO_SRCS})
# 生成的头文件目录
target_include_directories(proto_obj PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/)
# protobuffer的头文件目录和库
target_include_directories(proto_obj PUBLIC ${Protobuf_INCLUDE_DIRS})
target_link_libraries(proto_obj PUBLIC ${Protobuf_LIBRARIES})