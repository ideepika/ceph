
This guide provides the steps for anyone who is interested in getting started with end to end tracing using Jaeger and Opentracing libraries. 

Note: Right now support is available for using Jaeger in development environment on centos8 or Ubuntu(Bionic or Focal)

## Jaeger & Opentracing : 
Jaeger along with Opentracing provides a standard solution for tracing the complex background transactions in Ceph. 
Opentracing is the framework which in this case works with Jaeger backend, consisting of an agent which listens for any span, upon receiving one sends them to the collector. 
The spans gathered by the collectors are weived together to form traces, which can then be rendered on Jaeger UI. 
![Architecture](https://www.jaegertracing.io/img/architecture-v1.png)

## Steps: 
The instructions work ideally for Ubuntu 18.04(Bionic), and have been tested on Fedora 31 as well as Centos8. 

### Installing Jaeger dependencies :

1. rerun: 

```
$ ./install_deps.sh
```
this will install all the distro package available dependencies that are additionally required for Jaeger.

Note: you should use boost v1.72+ and above for depedencies to work properly.
Note: for Bionic(Ubuntu 18.04) yaml-cpp v0.6.0+ is not available, the following instruction will help in building it from source: 

2. For enabling Jaeger, either create a new build for ceph using: 
```
$ ./do_cmake.sh -DWITH_JAEGER=ON
```
or rerun cmake from `build/` using  
```
$ cmake -DWITH_JAEGER=ON ..
```
<!-- sudo apt-get install automake bison flex g++ git libevent-dev libssl-dev libtool make pkg-config> -->

### Pull Jaeger UI docker image
```
docker run -d --name jaeger \
  -e COLLECTOR_ZIPKIN_HTTP_PORT=9411 \
  -p 5775:5775/udp \
  -p 6831:6831/udp \
  -p 6832:6832/udp \
  -p 5778:5778 \
  -p 16686:16686 \
  -p 14268:14268 \
  -p 9411:9411 \
  jaegertracing/all-in-one:1.12
```
<Note add port details here>

Test if tracing is working fine:

start a vstart cluster: 
```
MDS=0 MGR=1 OSD=1 MON=1 ../src/vstart.sh -n --without-dashboard --memstore  
```
perform a write operation using RADOS bench, use the following commands : 
```
// creating a pool
    $ bin/ceph osd pool create test 8
// writing to it
    $ bin/rados -p test bench 5 write --no-cleanup
```
You can then navigate to http://localhost:16686 to access the Jaeger UI to see the traces. 

### Tracer configuration

Based on the usecase, the tracer can be configured by passing suitable parameter in form of a yaml file( generally present in `$(ceph-dir)/src/jaegertracing` )


TODO: elaborate more about config.yaml


valid config parameters: 
https://www.jaegertracing.io/docs/1.16/client-features/

 <!-- TODO: 
 1. add traced path details
 2. tracer config options recommended
 
 
 Note: how to add new tracepath(functional summary in a blog to publish on ceph)
 either here or on tracker: common debugging issues
 -->