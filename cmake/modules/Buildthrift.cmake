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

  set(thrift_CMAKE_ARGS  -DCMAKE_POSITION_INDEPENDENT_CODE=OFF
			 -DBUILD_JAVA=OFF
			 -DBUILD_PYTHON=OFF
			 -DBUILD_TESTING=OFF
			 -DBUILD_TUTORIALS=OFF
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
  CHECK_C_COMPILER_FLAG("-Wno-stringop-truncation" HAS_WARNING_STRINGOP_TRUNCATION)
  if(HAS_WARNING_STRINGOP_TRUNCATION)
    list(APPEND thrift_CMAKE_ARGS -DCMAKE_C_FLAGS=-Wno-stringop-truncation)
  endif()
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-Wno-deprecated-copy" HAS_WARNING_DEPRECATED_COPY)
  if(HAS_WARNING_DEPRECATED_COPY)
    set(thrift_CXX_FLAGS -Wno-deprecated-copy)
  endif()
  check_cxx_compiler_flag("-Wno-pessimizing-move" HAS_WARNING_PESSIMIZING_MOVE)
  if(HAS_WARNING_PESSIMIZING_MOVE)
    set(thrift_CXX_FLAGS "${thrift_CXX_FLAGS} -Wno-pessimizing-move")
  endif()

  if(CMAKE_MAKE_PROGRAM MATCHES "make")
    # try to inherit command line arguments passed by parent "make" job
    set(make_cmd $(MAKE) thrift)
  else()
    set(make_cmd ${CMAKE_COMMAND} --build <BINARY_DIR> --target thrift)
  endif()

  set(thrift_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/libthrift.a)
  include(ExternalProject)
  ExternalProject_Add(thrift
    SOURCE_DIR ${thrift_SOURCE_DIR}
    CMAKE_ARGS ${thrift_CMAKE_ARGS}
    BINARY_DIR ${thrift_BINARY_DIR}
    BUILD_COMMAND ${make_cmd}
    BUILD_BYPRODUCTS "${thrift_LIBRARY}"
    DEPENDS ${dependencies}
    )
endfunction()
