#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "inking::inking" for configuration "Release"
set_property(TARGET inking::inking APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(inking::inking PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/libinking.dll.a"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/libinking.dll"
  )

list(APPEND _cmake_import_check_targets inking::inking )
list(APPEND _cmake_import_check_files_for_inking::inking "${_IMPORT_PREFIX}/lib/libinking.dll.a" "${_IMPORT_PREFIX}/bin/libinking.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
