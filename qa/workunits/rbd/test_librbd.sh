#!/bin/sh -e

if [ -n "${VALGRIND}" ]; then
  valgrind ${VALGRIND} --suppressions=${TESTDIR}/valgrind.supp \
    --error-exitcode=1 ceph_test_librbd --gtest_filter=*TestLibRBD.TestEncryptionLUKS2*
else
  ceph_test_librbd --gtest_filter=*TestLibRBD.TestEncryptionLUKS2*
fi
exit 0
