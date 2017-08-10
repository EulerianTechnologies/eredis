
cmake_minimum_required(VERSION 2.6)
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

PROJECT ( EREDIS C )

# -Wno-strict-aliasing for hiredis libev (eredis libev is fine)
ADD_DEFINITIONS( "-O2 -Wall -Wno-strict-aliasing -ggdb")

ADD_SUBDIRECTORY( include )
ADD_SUBDIRECTORY( src )


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

FIND_PACKAGE(Doxygen)
IF(DOXYGEN_FOUND)
  CONFIGURE_FILE( ${CMAKE_CURRENT_SOURCE_DIR}/doc/doxyfile.in
    ${CMAKE_CURRENT_SOURCE_DIR}/doc/doxyfile @ONLY)
  ADD_CUSTOM_TARGET( doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doc/doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen" VERBATIM
    )
ENDIF(DOXYGEN_FOUND)

