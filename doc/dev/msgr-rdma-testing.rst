======================================
 Testing the RDMA messenger (Soft-RoCE)
======================================

The RDMA transport (``ms_type=async+rdma``) has no automated test coverage.
This describes how to exercise it on a single machine with no RDMA hardware,
using the in-kernel Soft-RoCE driver (``rdma_rxe``).

.. important::

   Soft-RoCE implements RDMA verbs **in software**. Measured loopback
   bandwidth is around 1 Gb/s, which is *slower than TCP on the same link*.
   This setup validates **function and correctness only**. Any throughput or
   latency number produced on ``rxe`` is an artefact of the emulation and must
   not be reported as an RDMA result. Real numbers require RoCE or InfiniBand
   hardware.

Kernel prerequisites
====================

``rdma_rxe`` must be built as a module. Distribution *cloud* kernels often omit
it: Debian's ``linux-image-cloud-*`` ships the RDMA core and vendor drivers but
no ``drivers/infiniband/sw/``. Check before anything else::

  ls /lib/modules/$(uname -r)/kernel/drivers/infiniband/sw/rxe/

If absent, install the generic kernel (on Debian, ``linux-image-amd64`` or
``linux-image-arm64``) and boot it.

Packages
========

On Debian/Ubuntu::

  apt-get install rdma-core ibverbs-utils rdmacm-utils perftest \
                  libibverbs-dev librdmacm-dev

memlock
========

RDMA registers pinned memory. The default ``memlock`` limit (often 8 MiB) is
far below what the messenger will try to register: ``ms_async_rdma_receive_buffers``
(default 32768) times ``ms_async_rdma_buffer_size`` (default 128 KiB) is a 4 GiB
ceiling per daemon. Raise it in ``/etc/security/limits.conf``::

  <user> soft memlock unlimited
  <user> hard memlock unlimited

Log out and back in, then confirm with ``ulimit -l``.

Bringing up a Soft-RoCE device
==============================

::

  modprobe rdma_rxe
  rdma link add rxe0 type rxe netdev eth0
  rdma link show          # expect: state ACTIVE physical_state LINK_UP
  ibv_devices             # expect: rxe0

Validate the emulation first
============================

Always establish that ``rxe`` itself works before blaming Ceph. A failure here
is not a Ceph bug::

  # raw verbs send/recv
  ib_send_bw -d rxe0 -x 0 &
  ib_send_bw -d rxe0 -x 0 <host-ip>

  # rdma_cm connection manager
  rping -s -a <host-ip> -p 9999 -C 3 -v &
  rping -c -a <host-ip> -p 9999 -C 3 -v

Building
========

``WITH_RDMA`` is ``ON`` by default but requires the ``-dev`` packages above::

  ./do_cmake.sh -DWITH_RDMA=ON
  ninja -C build vstart-base ceph-osd ceph-mon ceph-mgr rados

Confirm the stack was actually compiled in, rather than silently skipped::

  grep HAVE_RDMA build/include/acconfig.h
  ldd build/bin/ceph-osd | grep -E 'ibverbs|rdmacm'

Running a cluster
=================

Establish a TCP baseline first, so that any RDMA failure is attributable::

  MON=1 OSD=3 MDS=0 MGR=1 ../src/vstart.sh -n -x -d

Then the RDMA run::

  MON=1 OSD=3 MDS=0 MGR=1 ../src/vstart.sh -n -x -d \
    -o 'ms_type=async+rdma' \
    -o 'ms_async_rdma_device_name=rxe0' \
    -o 'ms_async_rdma_receive_buffers=4096'

``receive_buffers`` is reduced from its default so a small test cluster does not
pin several GiB per daemon; a memory failure otherwise looks like a protocol
failure. Add ``-o 'debug_ms=10'`` when diagnosing a handshake problem.

Verifying RDMA is really in use
===============================

A cluster that comes up proves nothing on its own -- confirm the transport::

  ceph daemon osd.0 config get ms_type          # "async+rdma"
  ceph daemon osd.0 perf dump | grep -i rdma    # tx_chunks/rx_chunks non-zero

Thread layout, one polling thread per messenger worker::

  pid=$(pgrep -f 'ceph-osd.*-i 0')
  ps -L -p $pid -o comm= | grep -c rdma-polling
  ps -L -p $pid -o comm= | grep -c msgr-worker

Per-thread CPU during a workload, which is where completion-path bottlenecks
show up::

  snap() { for t in /proc/$pid/task/*; do \
    printf '%s-%s %s\n' "$(cat $t/comm)" "$(basename $t)" \
    "$(awk '{print $14+$15}' $t/stat)"; done; }
  snap > /tmp/a; rados -p testpool bench 30 write -b 4M --no-cleanup; snap > /tmp/b
  join -j1 <(sort /tmp/a) <(sort /tmp/b) | awk '{d=$3-$2; if(d>0) print d, $1}' | sort -rn | head

Always take an **idle** sample over the same interval. The polling thread sleeps
when idle (after ``ms_async_rdma_polling_us``), so a non-zero idle reading means
something other than completion processing is being measured.

Known papercuts
===============

Unrelated to RDMA, but hit on this path because it is rarely exercised:

* ``vstart.sh`` requires ``jq`` and does not check for it. Without it, vstart
  loops waiting for the mgr, creates **zero OSDs**, and still exits ``0``.
* ``install-deps.sh`` does not pull ``python3-prettytable``, which the ``ceph``
  CLI imports.
