#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# Autoconf
AC_PREREQ(2.59)

# NOTE: This version is _only_ used for naming the tarball.  The
# VERSION define is not used by the code.  It gets a version string
# from 'git describe'; see src/ceph_ver.[ch]

AC_INIT([ceph], [10.0.4], [ceph-devel@vger.kernel.org])

# Environment
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([foreign parallel-tests tar-ustar])
# enable make V=0 (if automake >1.11)
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_ARG_WITH([radosgw])
AC_ARG_WITH([fuse])
AC_ARG_WITH([libatomic-ops])
AC_ARG_WITH([gtk2])
AC_ARG_WITH([nss],
	[AS_HELP_STRING([--with-nss], [Use cryptographic functions from nss])],
	[],
	[with_nss=check])

AC_ARG_WITH([debug],
            [AS_HELP_STRING([--with-debug], [build extra debug binaries and tests])],
            [case "${withval}" in
		  yes) with_debug=yes ;;
		  no)  with_debug=no ;;
		  *) AC_MSG_ERROR([bad value ${withval} for --with-debug]) ;;
	     esac],
            [with_debug=no])

AC_ARG_WITH([librocksdb],
            [AS_HELP_STRING([--with-librocksdb], [build rocksdb support])],
            [],
            [with_librocksdb=no])

AC_ARG_WITH([ocf],
            [AS_HELP_STRING([--with-ocf], [build OCF-compliant cluster resource agent])],
            ,
            [with_ocf=no])

AC_ARG_ENABLE(cephfs-java,
    [AC_HELP_STRING([--enable-cephfs-java], [build libcephfs Java bindings])],
    [], [enable_cephfs_java=no])

AC_ARG_WITH([lttng],
	[AS_HELP_STRING([--with-lttng], [Trace with LTTng])])

AC_ARG_WITH([babeltrace],
	[AS_HELP_STRING([--with-babeltrace], [Enable Babeltrace])])

AC_ARG_WITH([tcmalloc],
	    [AS_HELP_STRING([--without-tcmalloc], [disable tcmalloc for memory allocations])],
	    [],
	    [with_tcmalloc=yes])

AC_ARG_WITH([cryptopp],
	[AS_HELP_STRING([--with-cryptopp], [Use cryptographic functions from cryptopp])],
	[],
	[with_cryptopp=check])

AC_CONFIG_HEADERS([acconfig.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
