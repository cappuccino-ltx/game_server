
# third_party/akcp/CMakeLists.txt

find_package(Boost REQUIRED COMPONENTS system)

add_library(akcp STATIC IMPORTED GLOBAL)

set_target_properties(akcp PROPERTIES 
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libakcp.a
)

target_include_directories(akcp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(akcp INTERFACE Boost::system)
