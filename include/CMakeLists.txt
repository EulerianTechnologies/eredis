
ADD_CUSTOM_TARGET( eredis.h SOURCES eredis.h )

INSTALL( FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/eredis.h
  ${CMAKE_CURRENT_BINARY_DIR}/eredis-hiredis.h
  DESTINATION include )
