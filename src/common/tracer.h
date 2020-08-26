//
// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2020 Red Hat Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */
// Demonstrates basic usage of the OpenTracing API. Uses OpenTracing's
// mocktracer to capture all the recorded spans as JSON.

#ifndef TRACER_H_
#define TRACER_H_

#define SIGNED_RIGHT_SHIFT_IS 1
#define ARITHMETIC_RIGHT_SHIFT 1
#include <yaml-cpp/yaml.h>
#include "jaegertracing/Tracer.h"

using namespace opentracing;

typedef std::unique_ptr<opentracing::Span> jspan;

namespace JTracer {

  static void setUpTracer(const char* serviceToTrace) {
    static auto configYAML = YAML::LoadFile("../src/jaegertracing/config.yml");
    static auto config = jaegertracing::Config::parse(configYAML);
    static auto tracer = jaegertracing::Tracer::make(
	serviceToTrace, config, jaegertracing::logging::consoleLogger());
    opentracing::Tracer::InitGlobal(
	std::static_pointer_cast<opentracing::Tracer>(tracer));
}
}
#endif
