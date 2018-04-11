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
#
#.rst:
# FindLibev
# ---------
#
# Locate libev, the high-performance event loop library that is loosely modeled
# after libevent, if it is installed on the system.
#
# libev homepage: http://software.schmorp.de/pkg/libev.html
#
# Defines the following variables:
#
# ::
#
#    LIBEV_FOUND       - System has libev (include and library directories found)
#    LIBEV_INCLUDE_DIR - The libev include directories
#    LIBEV_LIBRARY     - The libev library for linking
#
#
#
# Accepts the following variables as input:
#
# ::
#
#    LIBEV_ROOT_DIR   - Set this variable to the root installation of libev if
#                       the module has problems finding the proper installation
#                       path. (This may be useful when cross-compiling, but is
#                       generally unnecessary otherwise.)
#

FIND_PATH( LIBEV_ROOT_DIR
    NAMES include/ev.h
)

FIND_PATH( LIBEV_INCLUDE_DIR
    NAMES ev.h
    HINTS ${LIBEV_ROOT_DIR}/include
    PATH_SUFFIXES libev
)

FIND_LIBRARY( LIBEV_LIBRARY
    NAMES ev
    HINTS ${LIBEV_ROOT_DIR}/lib
    PATH_SUFFIXES libev
)

INCLUDE( FindPackageHandleStandardArgs )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( libev DEFAULT_MSG
    LIBEV_INCLUDE_DIR
    LIBEV_LIBRARY
)

MARK_AS_ADVANCED(
    LIBEV_ROOT_DIR
    LIBEV_INCLUDE_DIR
    LIBEV_LIBRARY
)
