#!/usr/bin/env bash
set -ex

function expect_true()
{
        set -x
        if ! "$@"; then return 1; else return 0; fi
}

echo "Dismiss the could not load preferences file .gwcli/prefs.bin warning"
    gwcli ls >/dev/null 2>&1

echo "Check gateway exists on the host"
  check=$(gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
  test "$check" == "Auth: ACL_ENABLED, Hosts: 1]" || return 1

echo "Delete the host"
  expect_true gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/hosts delete client_iqn=iqn.1994-05.com.redhat:client
  check=$(gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
  test "$check" == "Auth: ACL_ENABLED, Hosts: 0]" || return 1

echo "Delete the iscsi-targets disk"
  expect_true gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/disks/ delete disk=datapool/block0
  check=$(gwcli ls iscsi-targets/ | grep 'o- disks' | awk -F'[' '{print $2}')
  test "$check" == "Disks: 0]" || return 1

echo "Delete the target IQN"
  expect_true gwcli iscsi-targets/ delete target_iqn=iqn.2003-01.com.redhat.iscsi-gw:ceph-gw
  check=$(gwcli ls iscsi-targets/ | grep 'o- iscsi-targets' | awk -F'[' '{print $2}')
  test "$check" == "DiscoveryAuth: None, Targets: 0]" || return 1

echo "Delete the disks"
  expect_true gwcli disks/ delete image_id=datapool/block0
  check=$(gwcli ls disks/ | grep 'o- disks' | awk -F'[' '{print $2}')
  test "$check" == "0.00Y, Disks: 0]" || return 1
