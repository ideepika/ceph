###############################################################################
#Ceph - scalable distributed file system
#
#Copyright (C) 2020 Red Hat Inc.
#
#This is free software; you can redistribute it and/or
#modify it under the terms of the GNU Lesser General Public
#License version 2.1, as published by the Free Software
#Foundation.  See file COPYING.
################################################################################

function(build_thrift)
  set(thrift_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/jaegertracing/thrift")
  set(thrift_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/thrift")

  set(thrift_CMAKE_ARGS  -DBUILD_JAVA=OFF
			 -DBUILD_PYTHON=OFF
			 -DBUILD_TESTING=OFF
			 -DBUILD_TUTORIALS=OFF
			 -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/thrift
			 -DCMAKE_POSITION_INDEPENDENT_CODE=ON
			 -DBUILD_SHARED_LIBS=OFF
			 )

  if(WITH_SYSTEM_BOOST)
    message(STATUS "thrift will be using system boost")
    set(dependencies "")
    list(APPEND thrift_CMAKE_ARGS -DBOOST_ROOT=/opt/ceph)
    list(APPEND thrift_CMAKE_ARGS -DCMAKE_FIND_ROOT_PATH=/opt/ceph)
  else()
    message(STATUS "thrift will be using external build boost")
    set(dependencies Boost)
    list(APPEND thrift_CMAKE_ARGS  -DCMAKE_FIND_ROOT_PATH=${CMAKE_BINARY_DIR}/boost)
    get_target_property(Boost_INCLUDE_DIRS Boost INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND thrift_CMAKE_ARGS  -DCMAKE_PREFIX_PATH=${Boost_INCLUDE_DIRS})
  endif()

  set(cxx_flags  -Wno-deprecated)
  list(APPEND thrift_CMAKE_ARGS ${cxx_flags})

  if(CMAKE_MAKE_PROGRAM MATCHES "make")
    # try to inherit command line arguments passed by parent "make" job
    set(make_cmd $(MAKE) thrift)
  else()
    set(make_cmd ${CMAKE_COMMAND} --build <BINARY_DIR> --target thrift)
  endif()

  set(thrift_LIBRARY ${thrift_BINARY_DIR}/lib/libthrift.a)
  include(ExternalProject)
  ExternalProject_Add(thrift
    SOURCE_DIR ${thrift_SOURCE_DIR}
    CMAKE_ARGS ${thrift_CMAKE_ARGS}
    BINARY_DIR ${thrift_BINARY_DIR}
    BUILD_COMMAND ${make_cmd}
    BUILD_BYPRODUCTS "${thrift_LIBRARY}"
    INSTALL_COMMAND cmake -E echo "Skipping install step."
    DEPENDS ${dependencies}
    )
add_library(thrift-static STATIC IMPORTED)
add_dependencies(thrift-static thrift)
set(thrift_INCLUDE_DIR ${thrift_SOURCE_DIR}/lib/cpp/src/thrift/)

set_target_properties(thrift-static PROPERTIES
  INTERFACE_LINK_LIBRARIES "${thrift_LIBRARY}"
  IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
  IMPORTED_LOCATION "${thrift_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${thrift_INCLUDE_DIR}")
endfunction()
