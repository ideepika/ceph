#include "tracer.h"

Jaeger_Tracer tracer;

Jaeger_Tracer::~Jaeger_Tracer()
{
    if(this->tracer)
        this->tracer->Close();
    jaeger_initialized = false;
}
