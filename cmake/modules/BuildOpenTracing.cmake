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
  set(OpenTracing_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/jaegertracing/opentracing-cpp")
  set(OpenTracing_BINARY_DIR "${CMAKE_BINARY_DIR}/external/opentracing-cpp")

  set(OpenTracing_CMAKE_ARGS  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                              -DBUILD_MOCKTRACER=OFF
                              -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/external
                              -DCMAKE_INSTALL_RPATH=${CMAKE_BINARY_DIR}/external
			       -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE
                              -DCMAKE_INSTALL_LIBDIR=${CMAKE_BINARY_DIR}/external/lib
                              -DCMAKE_PREFIX_PATH=${CMAKE_BINARY_DIR}/external)

  if(CMAKE_MAKE_PROGRAM MATCHES "make")
    # try to inherit command line arguments passed by parent "make" job
    set(make_cmd "$(MAKE)")
  else()
    set(make_cmd ${CMAKE_COMMAND} --build <BINARY_DIR> --target OpenTracing)
  endif()
  set(install_cmd $(MAKE) install DESTDIR=)

  include(ExternalProject)
  ExternalProject_Add(OpenTracing
    SOURCE_DIR ${OpenTracing_SOURCE_DIR}
    UPDATE_COMMAND ""
    INSTALL_DIR "${CMAKE_BINARY_DIR}/external"
    PREFIX "${CMAKE_BINARY_DIR}/external/opentracing-cpp"
    CMAKE_ARGS ${OpenTracing_CMAKE_ARGS}
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${make_cmd}
    INSTALL_COMMAND ${install_cmd}
    )
endfunction()
