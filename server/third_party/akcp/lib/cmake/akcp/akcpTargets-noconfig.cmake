#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "akcp::akcp" for configuration ""
set_property(TARGET akcp::akcp APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(akcp::akcp PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C;CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libakcp.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS akcp::akcp )
list(APPEND _IMPORT_CHECK_FILES_FOR_akcp::akcp "${_IMPORT_PREFIX}/lib/libakcp.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
