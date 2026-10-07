============================
 Testing the RDMA messenger
============================

The RDMA transport (``ms_type=async+rdma``) has no automated test coverage.
This describes how to exercise it on a single machine with no RDMA hardware,
using the in-kernel Soft-RoCE driver (``rdma_rxe``), and then how to test and
measure it on real RDMA hardware (`Testing on RDMA hardware`_).

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

Testing on RDMA hardware
========================

Everything above runs on one machine and proves only that the code works.
Throughput, latency and CPU-per-op claims need real RDMA NICs, more than one
host, and a TCP control run on the same hardware.

Hardware
--------

Minimum useful setup:

* **Hosts:** three OSD hosts, so replica-3 placement crosses the network, plus
  one separate client host. A fourth OSD host lets you take a host down
  without dropping below three replicas.
* **NICs:** one RDMA-capable NIC per host, the same model and firmware on
  every host:

  - RoCEv2 (Ethernet): NVIDIA ConnectX-5 or newer (ConnectX-6 Dx and
    ConnectX-7 are the common choices, ``mlx5_ib`` driver), Broadcom
    BCM575xx (``bnxt_re``) or Intel E810 (``irdma``). 25 GbE works for
    function tests; use 100 GbE or faster for performance work, because at
    25 GbE the link saturates before the messenger does.
  - InfiniBand: ConnectX-6 HDR or ConnectX-7 NDR HCAs, plus a subnet manager
    (a managed switch, or ``opensm`` on one host).
  - iWARP (Chelsio T6, Intel E810 in iWARP mode) needs
    ``ms_async_rdma_type=iwarp`` and ``ms_async_rdma_cm=true``. This
    branch's changes have not been tested on that path.

* **Switch:** for RoCEv2, a switch configured for lossless traffic on one
  priority: PFC enabled on that priority, ECN marking, and DCQCN on the NICs.
  RoCE without flow control works, but loss-induced retransmits make
  performance numbers meaningless. Use the same MTU end to end (9000 on the
  Ethernet side gives a 4096-byte RoCE MTU).
* **Per host:** 16 or more cores, 64 GiB or more of RAM, and two or more NVMe
  drives for BlueStore runs. Each daemon pins
  ``ms_async_rdma_receive_buffers`` times ``ms_async_rdma_buffer_size``
  (4 GiB by default), so budget pinned memory per OSD. Place the OSDs on the
  NIC's NUMA node (``cat /sys/class/net/<if>/device/numa_node``).
* **Software:** a kernel with the in-tree RDMA driver for the NIC, plus
  ``rdma-core`` and ``perftest`` on every host.

Cloud alternatives: Azure HB-, HC- and ND-series VMs expose InfiniBand
through SR-IOV, and Oracle Cloud bare-metal shapes with an RDMA cluster
network expose RoCEv2. AWS EFA is not usable: it does not provide the
reliable-connected (RC) queue pairs the messenger uses.

Step 1: Validate the fabric
---------------------------

Do this before installing Ceph. The numbers it gives are the ceiling for
every later result, and a failure here is not a Ceph bug.

::

  ibv_devinfo -d mlx5_0              # state: PORT_ACTIVE, active_mtu: 4096
  rdma link show                     # netdev mapping for RoCE

For RoCEv2, find the GID index of the RoCE v2 entry for the host's IPv4
address::

  for i in /sys/class/infiniband/mlx5_0/ports/1/gid_attrs/types/*; do
    echo "$(basename $i) $(cat $i 2>/dev/null) \
      $(cat /sys/class/infiniband/mlx5_0/ports/1/gids/$(basename $i))"
  done | grep -i 'v2'

The GID index can differ between hosts. Then, between every pair of hosts::

  ib_write_bw -d mlx5_0 -x <gid_idx> --report_gbits          # server
  ib_write_bw -d mlx5_0 -x <gid_idx> --report_gbits <peer>   # client
  ib_send_lat -d mlx5_0 -x <gid_idx> <peer>
  rping -s -a <ip> -C 10 -v   /   rping -c -a <peer-ip> -C 10 -v

Expect close to line rate (above 90 Gb/s on 100 GbE) and a send latency of a
few microseconds. Run a congested test too: start ``ib_write_bw`` from two
hosts to a third at the same time, and confirm the switch and NIC pause or ECN
counters move (``ethtool -S <if> | grep -E 'pause|ecn|out_of_buffer'``) and no
drops appear. If the counters stay at zero under congestion, flow control is
not configured on the priority RoCE uses.

Step 2: Deploy the branch
-------------------------

``vstart.sh`` cannot span hosts. Build packages or a container image from the
branch, and deploy it with cephadm or by hand. Build ``upstream main`` the
same way to use as the control.

Containers need the RDMA devices, pinned memory and the ``IPC_LOCK``
capability. With cephadm, add this to the OSD, MON and MGR service specs::

  extra_container_args:
    - "--device=/dev/infiniband"
    - "--cap-add=IPC_LOCK"
    - "--ulimit=memlock=-1:-1"

Without these the daemon fails to register memory, which looks like a
transport failure. Bare-metal daemons need ``memlock`` raised as described
above.

Step 3: Configure RDMA
----------------------

Start with RDMA on the cluster network only and keep the public network on
TCP. This limits a failure to replication traffic, and clients need no RDMA
NICs. The transport options are read at startup, so put them in
``ceph.conf`` or the config database before (re)starting the daemons::

  ceph config set global ms_cluster_type async+rdma
  ceph config set global ms_async_rdma_device_name mlx5_0
  ceph config set global ms_async_rdma_port_num 1
  # RoCEv2 only:
  ceph config set global ms_async_rdma_roce_ver 2
  ceph config set global ms_async_rdma_gid_idx <gid_idx>

If device names or GID indexes differ between hosts, set them per host with a
mask, for example ``ceph config set osd/host:node2 ms_async_rdma_gid_idx 5``.
For InfiniBand, ``ms_async_rdma_sl`` selects the service level. For RoCE,
``ms_async_rdma_dscp`` must map to the priority that has PFC enabled on the
switch and NICs. Once the cluster network passes all of the steps below, move
the public network too (``ms_public_type async+rdma``) with RDMA-capable
clients.

Step 4: Verify RDMA is in use
-----------------------------

::

  ceph daemon osd.0 config get ms_cluster_type
  ceph daemon osd.0 perf dump | jq 'with_entries(select(.key |
    startswith("AsyncMessenger::RDMADispatcher")))'

There is one ``RDMADispatcher-<n>`` section per messenger worker
(``ms_async_op_threads``). Under load, ``tx_total_wc`` and ``rx_total_wc``
must rise on **every** worker, not just one. If only one rises, accepted
connections are not reaching their owning worker. ``handshake_errors``,
``tx_total_wc_errors`` and ``rx_total_wc_errors`` must stay at zero. Also
confirm the traffic is on the RDMA port: its counters
(``/sys/class/infiniband/mlx5_0/ports/1/counters/port_xmit_data``) should
rise while the kernel TCP counters for that interface stay flat.

Step 5: Measure
---------------

Compare three arms on the same hosts: TCP (``ms_cluster_type async+posix``),
upstream-main RDMA, and this branch's RDMA. Interleave the arms instead of
running each one as a block, run five or more rounds per arm, and report
the spread, not one run.

* Use ``osd_op_queue=wpq`` for messenger measurements. mClock's per-OSD
  capacity limit can cap IOPS well below what the network carries, and every
  arm then looks the same.
* Run each workload twice: once with ``osd_objectstore=memstore`` to isolate
  the messenger, and once with BlueStore on NVMe to check the gain survives
  real disks.
* Workloads (``fio`` with the ``rbd`` engine, or ``rados bench``):

  - 4 KiB random write, queue depth 32 and 64. This exercises the completion
    path, where per-worker completion queues matter most.
  - 4 MiB sequential write and read, for bandwidth against the
    ``ib_write_bw`` ceiling.
  - 4 KiB with 8, 32 and 64 concurrent clients, to scale connection count.

* Record: IOPS, p50 and p99 latency, OSD CPU per op (``perf stat`` or
  ``/proc/<pid>/stat``), per-thread CPU split across the ``rdma-polling``
  threads (the ``snap`` script above), and NIC pause, ECN and drop counters.
  A result taken during NIC drops or pause storms is a fabric result, not a
  messenger result.

Step 6: Stability
-----------------

Run these on the RDMA cluster network with ``fio`` running throughout:

* A soak of 12 hours or more at a steady load.
* Kill and restart OSDs during IO
  (``ceph orch daemon restart osd.<n>``, or ``kill -9``).
* Flap one host's link for 30 seconds (``ip link set <if> down``, then
  ``up``).
* Mark an OSD out and back in, so backfill runs over RDMA.
* Reboot one OSD host.

After each step, the cluster must return to ``HEALTH_OK`` with no daemon
aborts. ``active_queue_pair`` must return to its pre-test value (if it climbs,
queue pairs are leaking), and ``rx_bufs_in_use`` must stay well below
``rx_bufs_total``. The ``rx`` and ``tx`` error counters may rise while a link
is down, but must stop rising once it is back.

What to record
--------------

Report results together with: the NIC model, firmware and driver
(``ethtool -i <if>``), the kernel version, the switch model and its PFC and
ECN settings, the MTU, the ``ib_write_bw`` and ``ib_send_lat`` baselines,
``ceph config dump``, and the commit of each arm. Without these, nobody else
can reproduce the numbers.

Known papercuts
===============

Unrelated to RDMA, but hit on this path because it is rarely exercised:

* ``vstart.sh`` requires ``jq`` and does not check for it. Without it, vstart
  loops waiting for the mgr, creates **zero OSDs**, and still exits ``0``.
* ``install-deps.sh`` does not pull ``python3-prettytable``, which the ``ceph``
  CLI imports.
