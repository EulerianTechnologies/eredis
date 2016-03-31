
INCLUDE_DIRECTORIES (
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/src/hiredis" )

# our dependancy to hiredis
ADD_CUSTOM_COMMAND (
  OUTPUT  hiredis/hiredis.c
  COMMAND git clone https://github.com/redis/hiredis.git
  )
ADD_CUSTOM_TARGET( hiredis-src SOURCES hiredis/hiredis.c )

ADD_CUSTOM_COMMAND (
  OUTPUT  ${PROJECT_SOURCE_DIR}/include/eredis-hiredis.h
  COMMAND cat hiredis/read.h hiredis/sds.h hiredis/hiredis.h | grep -v 'include.*sds.h' | grep -v 'include.*read.h' > ../include/eredis-hiredis.h
  DEPENDS hiredis/hiredis.c )
ADD_CUSTOM_TARGET( hiredis-inc SOURCES
  ${PROJECT_SOURCE_DIR}/include/eredis-hiredis.h )

SET_DIRECTORY_PROPERTIES (PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
  ${PROJECT_SOURCE_DIR}/src/hiredis)

ADD_LIBRARY (eredis SHARED eredis.c)
ADD_DEPENDENCIES (eredis hiredis-src hiredis-inc)
IF(CMAKE_VERSION VERSION_LESS 3.0)
TARGET_LINK_LIBRARIES (eredis pthread ev)
ELSE(CMAKE_VERSION VERSION_LESS 3.0)
TARGET_LINK_LIBRARIES (eredis PRIVATE pthread ev)
ENDIF(CMAKE_VERSION VERSION_LESS 3.0)

ADD_EXECUTABLE (test-async test-async.c)
TARGET_LINK_LIBRARIES (test-async eredis)

ADD_EXECUTABLE (test-async-thr test-async-thr.c)
TARGET_LINK_LIBRARIES (test-async-thr eredis pthread)

ADD_EXECUTABLE (test-sync test-sync.c)
TARGET_LINK_LIBRARIES (test-sync eredis)

ADD_EXECUTABLE (test-sync-thr test-sync-thr.c)
TARGET_LINK_LIBRARIES (test-sync-thr eredis pthread)

INSTALL (TARGETS eredis LIBRARY DESTINATION lib)

