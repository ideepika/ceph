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
#include <jaegertracing/Tracer.h>
#include <arpa/inet.h>
#include <yaml-cpp/yaml.h>
#include <jaegertracing/Tracer.h>

typedef std::unique_ptr<opentracing::Span> Span;

class Jager_Tracer{
  public:
    Jager_Tracer(){}
    ~Jager_Tracer(){
      if(this->tracer == NULL)
	return;
      if(!this->isTracerClosed)
        this->tracer->Close();
        this->isTracerClosed=true;
    }

 void init_tracer(const char* tracerName,const char* filePath){
     auto yaml = YAML::LoadFile(filePath);
     auto configuration = jaegertracing::Config::parse(yaml);
     this->isTracerClosed=false;
     this->tracer = jaegertracing::Tracer::make(
     tracerName,
     configuration,
     jaegertracing::logging::consoleLogger());
     opentracing::Tracer::InitGlobal(
     std::static_pointer_cast<opentracing::Tracer>(tracer));
     auto parent_span = tracer->StartSpan("parent");
     assert(parent_span);

     parent_span->Finish();
     tracer->Close();
 }
 inline void finish_tracer(){
   if(!this->isTracerClosed){
      this->isTracerClosed=true;
      this->tracer->Close();
    }
 }
 Span new_span(const char* spanName){
   Span span=opentracing::Tracer::Global()->StartSpan(spanName);
   return std::move(span);
 }
 Span child_span(const char* spanName,const Span& parentSpan){
   Span span = opentracing::Tracer::Global()->StartSpan(spanName, {opentracing::ChildOf(&parentSpan->context())});
   return std::move(span);
 }
 Span followup_span(const char *spanName, const Span& parentSpan){
 Span span = opentracing::Tracer::Global()->StartSpan(spanName, {opentracing::FollowsFrom(&parentSpan->context())});
 return std::move(span);
}
private:
  std::shared_ptr<opentracing::v2::Tracer> tracer = NULL;
  bool isTracerClosed;
};
#endif
