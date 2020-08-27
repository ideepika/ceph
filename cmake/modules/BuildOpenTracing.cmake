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

function(build_opentracing)
  set(opentracing_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/jaegertracing/opentracing-cpp")
  set(opentracing_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/opentracing")

  set(opentracing_CMAKE_ARGS  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                              -DBUILD_MOCKTRACER=OFF
			      -DBUILD_SHARED_LIBS=OFF
			      -DBUILD_DYNAMIC_LOADING=OFF
			      -DENABLE_LINTING=OFF
			      -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/opentracing)

  if(CMAKE_MAKE_PROGRAM MATCHES "make")
    # try to inherit command line arguments passed by parent "make" job
    set(make_cmd "$(MAKE)")
  else()
    set(make_cmd ${CMAKE_COMMAND} --build <BINARY_DIR> --target opentracing)
  endif()

  set(opentracing_LIBRARY "${opentracing_BINARY_DIR}/lib/libopentracing.a")
  include(ExternalProject)
  ExternalProject_Add(opentracing
    SOURCE_DIR ${opentracing_SOURCE_DIR}
    UPDATE_COMMAND ""
    #BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/opentracing"
    BUILD_IN_SOURCE 1
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/opentracing"
    CMAKE_ARGS ${opentracing_CMAKE_ARGS}
    BUILD_COMMAND ${make_cmd}
    )
add_library(opentracing-static STATIC IMPORTED)
add_dependencies(opentracing-static opentracing)
set(opentracing_INCLUDE_DIR ${opentracing_SOURCE_DIR}/include
  ${opentracing_SOURCE_DIR}/3rd_party/include/)

set_target_properties(opentracing-static PROPERTIES
  INTERFACE_LINK_LIBRARIES "${opentracing_LIBRARY}"
  IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
  IMPORTED_LOCATION "${opentracing_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${opentracing_INCLUDE_DIR}")

endfunction()

