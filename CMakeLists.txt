
cmake_minimum_required(VERSION 2.6)
IF(NOT DEFINED CMAKE_INSTALL_RPATH_USE_LINK_PATH)
  SET( CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE )
ENDIF(NOT DEFINED CMAKE_INSTALL_RPATH_USE_LINK_PATH)

PROJECT ( EREDIS C )

# -Wno-strict-aliasing for hiredis libev (eredis libev is fine)
SET( OPTIMIZE_LEVEL -O2 )
SET( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-strict-aliasing" )
SET( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${OPTIMIZE_LEVEL}" )
SET( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb" )
SET( CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${OPTIMIZE_LEVEL} -ggdb" )

LIST( APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" )

ADD_SUBDIRECTORY( include )
ADD_SUBDIRECTORY( src )
ADD_SUBDIRECTORY( doc )

SET (PDIR ${PROJECT_SOURCE_DIR})
ADD_CUSTOM_TARGET (distclean
  COMMAND make clean
  COMMAND find ${PDIR} -name Makefile -delete
  COMMAND find ${PDIR} -name CMakeCache.txt -delete
  COMMAND find ${PDIR} -name cmake_install.cmake -delete
  COMMAND find ${PDIR} -name install_manifest.txt -delete
  COMMAND find ${PDIR} -name CMakeFiles -type d -print0 | xargs -0 rm -r --
  COMMAND rm -f ${PDIR}/doc/doxyfile ${PDIR}/doc/doxygen_sqlite3.db
  COMMAND rm -Rf ${PDIR}/doc/html ${PDIR}/doc/latex
  )
