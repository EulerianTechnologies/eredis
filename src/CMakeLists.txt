
INCLUDE_DIRECTORIES( "${PROJECT_SOURCE_DIR}/include" )

# Resolve our dependency on hiredis from one of three places (in order of
# priority):
#
# 1. An external directory supplied by the where the hiredis source has already
#    been cloned
#
#    This is for use in environments where there is no access to the Internet
#    on the build host, or a patched version of hiredis has to be used for some
#    reason. This is generally not recommended. Use with caution.
#
# 2. The hiredis submodule maintained in the eredis Git repo
#
#    This is the recommended way to resolve hiredis if eredis is being built
#    from Git (as opposed to an unpacked tarball). The hiredis commit submoduled
#    into this repo is tested and guaranteed to work. Please note, however, that
#    you have to run "git submodule update --init" before invoking CMake! This
#    script will not clone the submodule for you. That is a step for the
#    maintainer/user, not the build system.
#
# 3. Clone the hiredis Git repo from GitHub
#
#    This is a last resort, and it should only be used if you are building
#    eredis out-of-tree (or possibly from an unpacked tarball, apart from Git
#    entirely). Otherwise it would clone a Git repo within another Git repo,
#    which is strongly discouraged. However to support our legacy build
#    behavior, it is not strictly forbidden, just discouraged.
#
IF(HIREDIS_SOURCE_DIR)
  IF(NOT EXISTS ${HIREDIS_SOURCE_DIR}/hiredis.c)
    MESSAGE( FATAL_ERROR "hiredis.c does not exist in provided HIREDIS_SOURCE_DIR" )
  ENDIF()
ELSEIF(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/hiredis/hiredis.c)
  SET( HIREDIS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hiredis )
ELSE()
  IF(NOT GIT_COMMAND)
    MESSAGE( STATUS "Checking for git" )
    FIND_PROGRAM( GIT_COMMAND
      NAMES git
      DOC "Git version control system command"
      )
    IF(NOT GIT_COMMAND)
      UNSET( GIT_COMMAND CACHE )
      MESSAGE( FATAL_ERROR "Checking for git - not found" )
    ELSE(NOT GIT_COMMAND)
      MESSAGE( STATUS "Checking for git - found (${GIT_COMMAND})" )
    ENDIF(NOT GIT_COMMAND)
  ENDIF(NOT GIT_COMMAND)

  ADD_CUSTOM_COMMAND(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/hiredis/hiredis.c
    COMMAND ${GIT_COMMAND} clone https://github.com/redis/hiredis.git
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cloning the upstream hiredis repo"
    )
  SET( HIREDIS_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/hiredis )

  SET_DIRECTORY_PROPERTIES( PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    ${HIREDIS_SOURCE_DIR} )
ENDIF()
ADD_CUSTOM_TARGET( hiredis-src SOURCES ${HIREDIS_SOURCE_DIR}/hiredis.c )
INCLUDE_DIRECTORIES( "${HIREDIS_SOURCE_DIR}" )

ADD_CUSTOM_COMMAND(
  OUTPUT  ${PROJECT_BINARY_DIR}/include/eredis-hiredis.h
  COMMAND
    ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/include &&
    cat
      ${HIREDIS_SOURCE_DIR}/read.h
      ${HIREDIS_SOURCE_DIR}/sds.h
      ${HIREDIS_SOURCE_DIR}/hiredis.h
    | grep -v -e 'include.*sds.h' -e 'include.*read.h'
    > ${PROJECT_BINARY_DIR}/include/eredis-hiredis.h
  DEPENDS ${HIREDIS_SOURCE_DIR}/hiredis.c )
ADD_CUSTOM_TARGET( hiredis-inc SOURCES
  ${PROJECT_BINARY_DIR}/include/eredis-hiredis.h )

FIND_PACKAGE(Threads REQUIRED)
IF(NOT (CMAKE_USE_PTHREADS_INIT OR CMAKE_HP_PTHREADS_INIT))
  MESSAGE( FATAL_ERROR "Only POSIX threads are supported by eredis" )
ENDIF(NOT (CMAKE_USE_PTHREADS_INIT OR CMAKE_HP_PTHREADS_INIT))

FIND_PACKAGE(Libev REQUIRED)
IF(NOT LIBEV_FOUND)
  MESSAGE( FATAL_ERROR "Could not find required package libev" )
ENDIF(NOT LIBEV_FOUND)

IF(NOT DEFINED BUILD_SHARED_LIBS)
  option(BUILD_SHARED_LIBS "Build eredis as a shared library" ON)
ENDIF(NOT DEFINED BUILD_SHARED_LIBS)

ADD_LIBRARY (eredis eredis.c)
ADD_DEPENDENCIES (eredis hiredis-src hiredis-inc)
TARGET_INCLUDE_DIRECTORIES( eredis PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  $<BUILD_INTERFACE:${LIBEV_INCLUDE_DIR}> )
MARK_AS_ADVANCED( EREDIS_INCLUDE_DIRECTORIES )
IF(CMAKE_VERSION VERSION_LESS 3.0)
  TARGET_LINK_LIBRARIES (eredis ${CMAKE_THREAD_LIBS_INIT} ${LIBEV_LIBRARY})
ELSE(CMAKE_VERSION VERSION_LESS 3.0)
  TARGET_LINK_LIBRARIES (eredis PUBLIC ${CMAKE_THREAD_LIBS_INIT} ${LIBEV_LIBRARY})
ENDIF(CMAKE_VERSION VERSION_LESS 3.0)

INSTALL (TARGETS eredis
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  )
