# Copyright (c) 2018 by Karl Lenz <xorangekiller@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or
# without modification, are permitted provided that the following
# conditions are met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above
#   copyright notice, this list of conditions and the following
#   disclaimer in the documentation and/or other materials
#   provided with the distribution.
#
# * Neither the name of Eulerian Technologies nor the names of its
#   contributors may be used to endorse or promote products
#   derived from this software without specific prior written
#   permission.
#
# THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

#.rst:
# FindRedis
# ---------
#
# Find the ``redis-server`` executable for the Redis in-memory
# key-value database server.
#
# The module defines the following variables:
#
# ``REDIS_EXECUTABLE``
#   path to the ``redis-server`` program
#
# ``REDIS_VERSION``
#   version of ``redis``, if it could be determined (which is
#   unlikely if we are cross-compiling)
#
# ``REDIS_FOUND``
#   true if the program was found
#
# Redis homepage: https://redis.io
#

IF(REDIS_BIN_DIR)
    FIND_PROGRAM( REDIS_EXECUTABLE
        NAMES redis-server
        HINTS ${REDIS_BIN_DIR}
        DOC "path to the redis-server executable"
    )
ELSE(REDIS_BIN_DIR)
    FIND_PROGRAM( REDIS_EXECUTABLE
        NAMES redis-server
        DOC "path to the redis-server executable"
    )
ENDIF(REDIS_BIN_DIR)
MARK_AS_ADVANCED( REDIS_EXECUTABLE )

IF(REDIS_EXECUTABLE)
    UNSET( REDIS_VERSION )

    IF(NOT CROSS_COMPILING)
        EXECUTE_PROCESS( COMMAND ${REDIS_EXECUTABLE} --version
            RESULT_VARIABLE _REDIS_version_result
            OUTPUT_VARIABLE _REDIS_version_output
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        MARK_AS_ADVANCED( _REDIS_version_result _REDIS_version_output )

        IF(${_REDIS_version_result} EQUAL 0 AND "${_REDIS_version_output}" MATCHES "v=([0-9]+\\.[0-9]+[^\n\t ]*)")
            SET( REDIS_VERSION "${CMAKE_MATCH_1}" )
            MARK_AS_ADVANCED( REDIS_VERSION )
        ENDIF()
    ENDIF(NOT CROSS_COMPILING)

    IF(NOT REDIS_VERSION)
        FIND_PROGRAM( STRINGS_EXECUTABLE
            NAMES strings
            DOC "path to the strings executable"
        )
        MARK_AS_ADVANCED( STRINGS_EXECUTABLE )

        IF(STRINGS_EXECUTABLE)
            EXECUTE_PROCESS( COMMAND ${STRINGS_EXECUTABLE} ${REDIS_EXECUTABLE}
                RESULT_VARIABLE _REDIS_strings_result
                OUTPUT_VARIABLE _REDIS_strings_output
                ERROR_QUIET
            )
            MARK_AS_ADVANCED( _REDIS_strings_result _REDIS_strings_output )

            IF("${_REDIS_strings_output}" MATCHES "[\n\t ]([0-9]+\\.[0-9]+[^\n\t ]*)")
                SET( REDIS_VERSION "${CMAKE_MATCH_1}" )
                MARK_AS_ADVANCED( REDIS_VERSION )
            ENDIF()
        ENDIF(STRINGS_EXECUTABLE)
    ENDIF(NOT REDIS_VERSION)
ENDIF(REDIS_EXECUTABLE)

INCLUDE( FindPackageHandleStandardArgs )
IF(NOT REDIS_VERSION OR CMAKE_VERSION VERSION_LESS 3.0)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS( Redis DEFAULT_MSG
        REDIS_EXECUTABLE
    )
ELSE(NOT REDIS_VERSION OR CMAKE_VERSION VERSION_LESS 3.0)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS( Redis
        FOUND_VAR REDIS_FOUND
        VERSION_VAR REDIS_VERSION
        REQUIRED_VARS REDIS_EXECUTABLE
    )
ENDIF(NOT REDIS_VERSION OR CMAKE_VERSION VERSION_LESS 3.0)
