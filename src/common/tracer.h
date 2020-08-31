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

  static inline void setUpTracer(const char* serviceToTrace) {
    static auto configYAML = YAML::LoadFile("../src/jaegertracing/config.yml");
    static auto config = jaegertracing::Config::parse(configYAML);
    static auto tracer = jaegertracing::Tracer::make(
	serviceToTrace, config, jaegertracing::logging::consoleLogger());
    opentracing::Tracer::InitGlobal(
	std::static_pointer_cast<opentracing::Tracer>(tracer));
  }

  static inline std::string Inject(jspan& span, const char* name="inject_test") {
    std::stringstream ss;
    if (!span) {
      auto span = opentracing::Tracer::Global()->StartSpan(name);
    }
    auto err = opentracing::Tracer::Global()->Inject(span->context(), ss);
    assert(err);
    return ss.str();
    }

  static inline void Extract(jspan& span, const char* name="test_text",
	       std::string t_meta="t_meta", std::shared_ptr<opentracing::v3::Tracer>
   tracer=nullptr) {

    std::stringstream ss(t_meta);
    if(!tracer){
    setUpTracer("Extract-service");
       }
    auto span_context_maybe = opentracing::Tracer::Global()->Extract(ss);
    assert(span_context_maybe.error() == opentracing::v3::span_context_corrupted_error);
    // How to get a readable message from the error.
    std::cout << "Example error message: \"" << span_context_maybe.error().message() << "\"\n";

    // Propogation span
    auto _span = opentracing::Tracer::Global()->StartSpan(
	"propagationSpan", {ChildOf(span_context_maybe->get())});

    _span->Finish();
  }

}
#endif
