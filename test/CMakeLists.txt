
IF(NOT DEFINED BUILD_TESTS)
  IF(CROSS_COMPILING)
    OPTION( BUILD_TESTS "Build test programs for eredis" OFF )
  ELSE(CROSS_COMPILING)
    OPTION( BUILD_TESTS "Build test programs for eredis" ON )
  ENDIF(CROSS_COMPILING)
ENDIF(NOT DEFINED BUILD_TESTS)

IF(BUILD_TESTS)
  ADD_EXECUTABLE (test-async test-async.c)
  TARGET_LINK_LIBRARIES (test-async eredis)

  ADD_EXECUTABLE (test-async-thr test-async-thr.c)
  TARGET_LINK_LIBRARIES (test-async-thr eredis)

  ADD_EXECUTABLE (test-sync test-sync.c)
  TARGET_LINK_LIBRARIES (test-sync eredis)

  ADD_EXECUTABLE (test-sync-thr test-sync-thr.c)
  TARGET_LINK_LIBRARIES (test-sync-thr eredis)

  ADD_EXECUTABLE (eredis-drop-noexpire eredis-drop-noexpire.c)
  TARGET_LINK_LIBRARIES (eredis-drop-noexpire eredis)

  IF(NOT CROSS_COMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
    FIND_PACKAGE(Redis 3.0)
    IF(REDIS_FOUND)
      MACRO(ADD_EREDIS_TEST TEST_NAME)
        ADD_TEST( NAME ${TEST_NAME}
          COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test-eredis.sh
            -r ${REDIS_EXECUTABLE}
            -t ${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME}
          )
        IF(CMAKE_VERSION VERSION_GREATER 3.0)
          SET_TESTS_PROPERTIES( ${TEST_NAME} PROPERTIES RUN_SERIAL TRUE )
        ENDIF(CMAKE_VERSION VERSION_GREATER 3.0)
      ENDMACRO(ADD_EREDIS_TEST)

      ADD_EREDIS_TEST( test-async )
      ADD_EREDIS_TEST( test-async-thr )
      ADD_EREDIS_TEST( test-sync )
      ADD_EREDIS_TEST( test-sync-thr )
      ADD_EREDIS_TEST( eredis-drop-noexpire )
    ENDIF(REDIS_FOUND)
  ENDIF(NOT CROSS_COMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
ENDIF(BUILD_TESTS)
