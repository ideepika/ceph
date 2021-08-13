#!/usr/bin/env bash
set -ex

function expect_true()
{
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

echo "Create a datapool/block0 disk..."
    expect_true gwcli disks/ create pool=datapool image=block0 size=300M wwn=36001405da17b74481464e9fa968746d3

    check=$(gwcli ls disks/ | grep 'o- disks' | awk -F'[' '{print $2}')
    expect_val "300M, Disks: 1]" "$check"

    check=$(gwcli ls disks/ | grep 'o- datapool' | awk -F'[' '{print $2}')
    expect_val "datapool (300M)]" "$check"

    check=$(gwcli ls disks/ | grep 'o- block0' | awk -F'[' '{print $2}')
    expect_val "datapool/block0 (Online, 300M)]" "$check"

echo "Create the target IQN..."
    expect_true gwcli iscsi-targets/ create target_iqn=iqn.2003-01.com.redhat.iscsi-gw:ceph-gw

    check=$(gwcli ls iscsi-targets/ | grep 'o- iscsi-targets' | awk -F'[' '{print $2}')
    expect_val "DiscoveryAuth: None, Targets: 1]" "$check"

    check=$(gwcli ls iscsi-targets/ | grep 'o- iqn.2003-01.com.redhat.iscsi-gw:ceph-gw' | awk -F'[' '{print $2}')
    expect_val "Auth: None, Gateways: 0]" "$check"

    check=$(gwcli ls iscsi-targets/ | grep 'o- disks' | awk -F'[' '{print $2}')
    expect_val "Disks: 0]" "$check"

    check=$(gwcli ls iscsi-targets/ | grep 'o- gateways' | awk -F'[' '{print $2}')
    expect_val "Up: 0/0, Portals: 0]" "$check"

    check=$(gwcli ls iscsi-targets/ | grep 'o- host-groups' | awk -F'[' '{print $2}')
    expect_val "Groups : 0]" "$check"

    check=$(gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
    expect_val "Auth: ACL_ENABLED, Hosts: 0]" "$check"

echo "Create the first gateway"
    export HOST=`python3 -c "import socket; print(socket.getfqdn())"`
    export IP=`hostname -i | awk '{print $NF}'`

    expect_true gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/gateways create ip_addresses=$IP gateway_name=$HOST
    check=$(gwcli ls iscsi-targets/ | grep 'o- gateways' | awk -F'[' '{print $2}')
    expect_val "Up: 1/1, Portals: 1]" "$check"

echo "Create the second gateway"
  IP=`cat /etc/ceph/iscsi-gateway.cfg |grep 'trusted_ip_list' | awk -F'[, ]' '{print $3}'`
  if [ "$IP" != `hostname -i | awk '{print $1}'` ]; then
    HOST=`python3 -c "import socket; print(socket.getfqdn('$IP'))"`
    sudo gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/gateways create ip_addresses=$IP gateway_name=$HOST

  IP=`cat /etc/ceph/iscsi-gateway.cfg |grep 'trusted_ip_list' | awk -F'[, ]' '{print $4}'`
  if [ "$IP" != `hostname -i | awk '{print $1}'` ]; then
    HOST=`python3 -c "import socket; print(socket.getfqdn('$IP'))"`
    sudo gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/gateways create ip_addresses=$IP gateway_name=$HOST
  fi
  check=$(sudo gwcli ls iscsi-targets/ | grep 'o- gateways' | awk -F'[' '{print $2}')
  expect_val "Up: 2/2, Portals: 2]" "$check"

echo "Attach the disk"
    sudo gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/disks/ add disk=datapool/block0
    check=$(sudo gwcli ls iscsi-targets/ | grep 'o- disks' | awk -F'[' '{print $2}')
    expect_val "Disks: 1]" "$check"

echo "Create a host"
    sudo gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/hosts create client_iqn=iqn.1994-05.com.redhat:client
    check=$(sudo gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
    expect_val "Auth: ACL_ENABLED, Hosts: 1]" "$check"
    check=$(sudo gwcli ls iscsi-targets/ | grep 'o- iqn.1994-05.com.redhat:client' | awk -F'[' '{print $2}')
    expect_val "Auth: None, Disks: 0(0.00Y)]" "$check"

echo "Map the LUN"
    sudo gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/hosts/iqn.1994-05.com.redhat:client disk disk=datapool/block0
    check=$(sudo gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
    expect_val "Auth: ACL_ENABLED, Hosts: 1]" "$check"
    check=$(sudo gwcli ls iscsi-targets/ | grep 'o- iqn.1994-05.com.redhat:client' | awk -F'[' '{print $2}')
    expect_val "Auth: None, Disks: 1(300M)]" "$check"

echo "Success!"

