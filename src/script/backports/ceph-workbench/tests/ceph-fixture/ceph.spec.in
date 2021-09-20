Name:		ceph
Version:	10.0.4
Release:	2343.g75a410c%{?dist}
Epoch:		1
Summary:	User space components of the Ceph file system
License:	LGPL-2.1 and CC-BY-SA-1.0 and GPL-2.0 and BSL-1.0 and GPL-2.0-with-autoconf-exception and BSD-3-Clause and MIT
URL:		http://ceph.com/
Source0:	http://ceph.com/download/%{name}-%{version}.tar.bz2
BuildRequires: automake

%description
Ceph is a massively scalable, open-source, distributed storage system that runs
on commodity hardware and delivers object, block and file system storage.

%prep
%setup -q

%build
./autogen.sh
%{configure}
make

%install
make DESTDIR=$RPM_BUILD_ROOT install

%package base
Summary:       Ceph Base Package
Group:         System Environment/Base
%description base
Base is the package that includes all the files shared amongst ceph servers

%package -n ceph-common
Summary:Ceph Common
Group:System Environment/Base
%description -n ceph-common
Common utilities to mount and interact with a ceph storage cluster.
Comprised of files that are common to Ceph clients and servers.

%package mds
Summary:Ceph Metadata Server Daemon
Group:System Environment/Base
%description mds
ceph-mds is the metadata server daemon for the Ceph distributed file system.
One or more instances of ceph-mds collectively manage the file system
namespace, coordinating access to the shared OSD cluster.

%package mon
Summary:Ceph Monitor Daemon
Group:System Environment/Base
%description mon
ceph-mon is the cluster monitor daemon for the Ceph distributed file
system. One or more instances of ceph-mon form a Paxos part-time
parliament cluster that provides extremely reliable and durable storage
of cluster membership, configuration, and state.

%package fuse
Summary:Ceph fuse-based client
Group:System Environment/Base
%description fuse
FUSE based client for Ceph distributed network file system

%package -n rbd-fuse
Summary:Ceph fuse-based client
Group:System Environment/Base
%description -n rbd-fuse
FUSE based client to map Ceph rbd images to files

%package -n rbd-mirror
Summary:Ceph daemon for mirroring RBD images
Group:System Environment/Base
%description -n rbd-mirror
Daemon for mirroring RBD images between Ceph clusters, streaming
changes asynchronously.

%package -n rbd-nbd
Summary:Ceph RBD client base on NBD
Group:System Environment/Base
%description -n rbd-nbd
NBD based client to map Ceph rbd images to local device

%package radosgw
Summary:Rados REST gateway
Group:Development/Libraries
%description radosgw
This package is an S3 HTTP REST gateway for the RADOS object store. It
is implemented as a FastCGI module using libfcgi, and can be used in
conjunction with any FastCGI capable web server.

%package osd
Summary:Ceph Object Storage Daemon
Group:System Environment/Base
%description osd
ceph-osd is the object storage daemon for the Ceph distributed file
system.  It is responsible for storing objects on a local file system
and providing access to them over the network.

%package -n librados2
Summary:RADOS distributed object store client library
Group:System Environment/Libraries
License:LGPL-2.0
%description -n librados2
RADOS is a reliable, autonomic distributed object storage cluster
developed as part of the Ceph distributed storage system. This is a
shared library allowing applications to access the distributed object
store using a simple file-like interface.

%package -n librados2-devel
Summary:RADOS headers
Group:Development/Libraries
License:LGPL-2.0
%description -n librados2-devel
This package contains libraries and headers needed to develop programs
that use RADOS object store.

%package -n librgw2
Summary:RADOS gateway client library
Group:System Environment/Libraries
License:LGPL-2.0
%description -n librgw2
This package provides a library implementation of the RADOS gateway
(distributed object store with S3 and Swift personalities).

%package -n librgw2-devel
Summary:RADOS gateway client library
Group:Development/Libraries
License:LGPL-2.0
%description -n librgw2-devel
This package contains libraries and headers needed to develop programs
that use RADOS gateway client library.

%package -n python-rados
Summary:Python libraries for the RADOS object store
Group:System Environment/Libraries
License:LGPL-2.0
%description -n python-rados
This package contains Python libraries for interacting with Cephs RADOS
object store.

%package -n libradosstriper1
Summary:RADOS striping interface
Group:System Environment/Libraries
License:LGPL-2.0
%description -n libradosstriper1
Striping interface built on top of the rados library, allowing
to stripe bigger objects onto several standard rados objects using
an interface very similar to the rados one.

%package -n libradosstriper1-devel
Summary:RADOS striping interface headers
Group:Development/Libraries
License:LGPL-2.0
%description -n libradosstriper1-devel
This package contains libraries and headers needed to develop programs
that use RADOS striping interface.

%package -n librbd1
Summary:RADOS block device client library
Group:System Environment/Libraries
License:LGPL-2.0
%description -n librbd1
RBD is a block device striped across multiple distributed objects in
RADOS, a reliable, autonomic distributed object storage cluster
developed as part of the Ceph distributed storage system. This is a
shared library allowing applications to manage these block devices.

%package -n librbd1-devel
Summary:RADOS block device headers
Group:Development/Libraries
License:LGPL-2.0
%description -n librbd1-devel
This package contains libraries and headers needed to develop programs
that use RADOS block device.

%package -n python-rbd
Summary:Python libraries for the RADOS block device
Group:System Environment/Libraries
License:LGPL-2.0
%description -n python-rbd
This package contains Python libraries for interacting with Cephs RADOS
block device.

%package -n libcephfs1
Summary:Ceph distributed file system client library
Group:System Environment/Libraries
License:LGPL-2.0
%description -n libcephfs1
Ceph is a distributed network file system designed to provide excellent
performance, reliability, and scalability. This is a shared library
allowing applications to access a Ceph distributed file system via a
POSIX-like interface.

%package -n libcephfs1-devel
Summary:Ceph distributed file system headers
Group:Development/Libraries
License:LGPL-2.0
%description -n libcephfs1-devel
This package contains libraries and headers needed to develop programs
that use Cephs distributed file system.

%package -n python-cephfs
Summary:Python libraries for Ceph distributed file system
Group:System Environment/Libraries
License:LGPL-2.0
%description -n python-cephfs
This package contains Python libraries for interacting with Cephs distributed
file system.

%package -n ceph-test
Summary:Ceph benchmarks and test tools
Group:System Environment/Libraries
License:LGPL-2.0
%description -n ceph-test
This package contains Ceph benchmarks and test tools.

%package selinux
Summary:SELinux support for Ceph MON, OSD and MDS
Group:System Environment/Base
%description selinux
This package contains SELinux support for Ceph MON, OSD and MDS. The package
also performs file-system relabelling which can take a long time on heavily
populated file-systems.

%package libs-compat
Summary:Meta package to include ceph libraries
Group:System Environment/Libraries
License:LGPL-2.0
Provides:	ceph-libs
%description libs-compat
This is a meta package, that pulls in librados2, librbd1 and libcephfs1. It
is included for backwards compatibility with distributions that depend on the
former ceph-libs package, which is now split up into these three subpackages.
Packages still depending on ceph-libs should be fixed to depend on librados2,
librbd1 or libcephfs1 instead.

%package devel-compat
Summary:Compatibility package for Ceph headers
Group:Development/Libraries
License:LGPL-2.0
Provides:	ceph-devel
%description devel-compat
This is a compatibility package to accommodate ceph-devel split into
librados2-devel, librbd1-devel and libcephfs1-devel. Packages still depending
on ceph-devel should be fixed to depend on librados2-devel, librbd1-devel,
libcephfs1-devel or libradosstriper1-devel instead.

%package -n python-ceph-compat
Summary:Compatibility package for Cephs python libraries
Group:System Environment/Libraries
License:LGPL-2.0
Provides:	python-ceph
%description -n python-ceph-compat
This is a compatibility package to accommodate python-ceph split into
python-rados, python-rbd and python-cephfs. Packages still depending on
python-ceph should be fixed to depend on python-rados, python-rbd or
python-cephfs instead.

%package -n libcephfs_jni1
Summary:	Java Native Interface library for CephFS Java bindings
Group:		System Environment/Libraries
License:	LGPL-2.0
%description -n libcephfs_jni1
This package contains the Java Native Interface library for CephFS Java
bindings.

%package -n libcephfs_jni1-devel
Summary:	Development files for CephFS Java Native Interface library
Group:		System Environment/Libraries
License:	LGPL-2.0
%description -n libcephfs_jni1-devel
This package contains the development files for CephFS Java Native Interface
library.

%package -n cephfs-java
Summary:	Java libraries for the Ceph File System
Group:		System Environment/Libraries
License:	LGPL-2.0
%description -n cephfs-java
This package contains the Java libraries for the Ceph File System.

%files
/usr/share/doc/ceph/README.md
%{_bindir}/ceph

%files base
%files common
%files mds
%files mon
%files fuse
%files -n rbd-fuse
%files -n rbd-mirror
%files -n rbd-nbd
%files radosgw
%files osd
%files -n librados2
%files -n librados2-devel
%files -n python-rados
%files -n libradosstriper1
%files -n libradosstriper1-devel
%files -n librbd1
%files -n librbd1-devel
%files -n librgw2
%files -n librgw2-devel
%files -n python-rbd
%files -n libcephfs1
%files -n libcephfs1-devel
%files -n python-cephfs
%files -n ceph-test
%files selinux
%files libs-compat
%files devel-compat
%files -n python-ceph-compat
%files -n cephfs-java
%files -n libcephfs_jni1-devel
%files -n libcephfs_jni1
