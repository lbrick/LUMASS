PROJECT(QtPropertyBrowser)

#-----------------------------------------------------------------------------
# Output directories.
#
SET(output_dir ${lumass_BINARY_DIR}/lib)
#SET(output_dir ${QtPropertyBrowser_BINARY_DIR}/bin)
#FOREACH(type LIBRARY RUNTIME ARCHIVE)
#  IF(NOT DEFINED CMAKE_${type}_OUTPUT_DIRECTORY)
#    SET(CMAKE_${type}_OUTPUT_DIRECTORY ${output_dir} CACHE INTERNAL "Single output directory for building all libraries.")
#  ENDIF()
#ENDFOREACH()

#-----------------------------------------------------------------------------
# Qt
#
#FIND_PACKAGE(Qt4 4.7 REQUIRED)
#INCLUDE(${QT_USE_FILE})

#-----------------------------------------------------------------------------
# Subdirectories
#
ADD_SUBDIRECTORY(src)
#IF(QtPropertyBrowser_BUILD_EXAMPLES)
#  ADD_SUBDIRECTORY(examples)
#ENDIF()

#-----------------------------------------------------------------------------
# Generate QtPropertyBrowserConfig file
#
CONFIGURE_FILE(UseQtPropertyBrowser.cmake.in
  ${QtPropertyBrowser_BINARY_DIR}/UseQtPropertyBrowser.cmake COPYONLY)

INCLUDE(GenerateQtPropertyBrowserConfig.cmake)
