#!/usr/bin/env bash
set -ex

function expect_true() {
  set -x
  if ! "$@"; then return 1; else return 0; fi
}

function expect_val() {
  local expected_val check
  expected_val=$1
  check=$2

  if [[ "$check" != "$expected_val" ]]; then
    echo "expected '$expected_val', got '$check'"
    exit 1
  fi
}

echo "Dismiss the could not load preferences file .gwcli/prefs.bin warning"
    gwcli ls >/dev/null 2>&1

echo "Check gateway exists on the host"
  check=$(gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
  expect_val "Auth: ACL_ENABLED, Hosts: 1]" "$check"

echo "Delete the host"
  expect_true gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/hosts delete client_iqn=iqn.1994-05.com.redhat:client
  check=$(gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
  expect_val "Auth: ACL_ENABLED, Hosts: 0]" "$check"

echo "Delete the iscsi-targets disk"
  expect_true gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/disks/ delete disk=datapool/block0
  check=$(gwcli ls iscsi-targets/ | grep 'o- disks' | awk -F'[' '{print $2}')
  expect_val "Disks: 0]" "$check"

echo "Delete the target IQN"
  expect_true gwcli iscsi-targets/ delete target_iqn=iqn.2003-01.com.redhat.iscsi-gw:ceph-gw
  check=$(gwcli ls iscsi-targets/ | grep 'o- iscsi-targets' | awk -F'[' '{print $2}')
  expect_val "DiscoveryAuth: None, Targets: 0]" "$check"

echo "Delete the disks"
  expect_true gwcli disks/ delete image_id=datapool/block0
  check=$(gwcli ls disks/ | grep 'o- disks' | awk -F'[' '{print $2}')
  expect_val "0.00Y, Disks: 0]" "$check"
