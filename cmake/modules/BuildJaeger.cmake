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
#
# This module builds Jaeger after it's dependencies are installed and discovered
# opentracing: is built using cmake/modules/Buildopentracing.cmake
# Thrift: build using cmake/modules/Buildthrift.cmake
# yaml-cpp, nlhomann-json: are installed locally and then discovered using
# Find<package>.cmake
# Boost Libraries used for building thrift are build and provided by
# cmake/modules/BuildBoost.cmake

function(build_jaeger)
  set(jaeger_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/jaegertracing/jaeger-client-cpp")
  set(jaeger_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/jaeger")

  set(jaeger_CMAKE_ARGS -DCMAKE_POSITION_INDEPENDENT_CODE=ON
		        -DBUILD_SHARED_LIBS=OFF
                        -DHUNTER_ENABLED=OFF
                        -DBUILD_TESTING=OFF
                        -DJAEGERTRACING_BUILD_EXAMPLES=OFF
                        -DCMAKE_PREFIX_PATH=${CMAKE_CURRENT_BINARY_DIR}
			-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/jaeger
			-Dyaml-cpp_HOME=${CMAKE_CURRENT_BINARY_DIR}
			-Dthrift_HOME=${CMAKE_CURRENT_BINARY_DIR}/thrift
			-Dopentracing_HOME=${CMAKE_CURRENT_BINARY_DIR}/opentracing
			-DCMAKE_FIND_ROOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/opentracing)

  set(dependencies opentracing)
  include(BuildOpenTracing)
  set(dependencies opentracing)
  build_opentracing()
  #find_package(thrift 0.11.0)
  if(NOT thrift_FOUND)
    include(Buildthrift)
    build_thrift()
    list(APPEND dependencies thrift)
  endif()
  find_package(yaml-cpp 0.6.0)
  if(NOT yaml-cpp_FOUND)
    include(Buildyaml-cpp)
    build_yamlcpp()
    list(APPEND dependencies "yaml-cpp")
  endif()

  message(STATUS "DEPENDENCIES ${dependencies}")
  if(CMAKE_MAKE_PROGRAM MATCHES "make")
    # try to inherit command line arguments passed by parent "make" job
    set(make_cmd $(MAKE))
  else()
    set(make_cmd ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jaeger)
  endif()

  include(ExternalProject)
  ExternalProject_Add(jaeger
    SOURCE_DIR ${jaeger_SOURCE_DIR}
    UPDATE_COMMAND ""
    CMAKE_ARGS ${jaeger_CMAKE_ARGS}
    BINARY_DIR ${jaeger_BINARY_DIR}
    BUILD_COMMAND ${make_cmd}
    DEPENDS "${dependencies}"
    )

add_library(jaeger-static STATIC IMPORTED)
add_dependencies(jaeger-static jaeger)
set(jaeger_INCLUDE_DIR ${jaeger_SOURCE_DIR}/src/)
set(jaeger_LIBRARY ${jaeger_BINARY_DIR}/libjaegertracing.a)
include_directories(jaeger_INCLUDE_DIR)
set_target_properties(jaeger-static PROPERTIES
  INTERFACE_LINK_LIBRARIES "${jaeger_LIBRARY}"
  IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
  IMPORTED_LOCATION "${jaeger_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${jaeger_INCLUDE_DIR}")
endfunction()
