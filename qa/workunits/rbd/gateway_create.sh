#!/usr/bin/env bash
set -ex

function expect_true()
{
	set -x
	if ! "$@"; then return 1; else return 0; fi
}

iscsi_container=$(sudo podman ps -a | grep -F 'iscsi' | grep -Fv 'tcmu' | awk '{print $1}')

gateway_cmd=$(find . -name gateway_create.sh)
podman $gateway_cmd/gateway_create.sh $iscsi_container:bin/gateway_create.sh

if [ -z "$iscsi_container" ]; then
    podman exec $iscsi_container gateway_create.sh
fi

echo "Create a datapool/block0 disk..."
    expect_true gwcli disks/ create pool=datapool image=block0 size=300M wwn=36001405da17b74481464e9fa968746d3

    check=$(gwcli ls disks/ | grep 'o- disks' | awk -F'[' '{print $2}')
    test "$check" == "300M, Disks: 1])" || return 1

    check=$(gwcli ls disks/ | grep 'o- datapool' | awk -F'[' '{print $2}')
    test "$check" == "datapool (300M)]" || return 1

    check=$(gwcli ls disks/ | grep 'o- block0' | awk -F'[' '{print $2}')
    test "$check" == "datapool/block0 (Unknown, 300M)]" || return 1

echo "Create the target IQN..."
    expect_true gwcli iscsi-targets/ create target_iqn=iqn.2003-01.com.redhat.iscsi-gw:ceph-gw

    check=$(gwcli ls iscsi-targets/ | grep 'o- iscsi-targets' | awk -F'[' '{print $2}')
    test "$check" == "DiscoveryAuth: None, Targets: 1]" || return 1

    check=$(gwcli ls iscsi-targets/ | grep 'o- iqn.2003-01.com.redhat.iscsi-gw:ceph-gw' | awk -F'[' '{print $2}')
    test "$check" == "Auth: None, Gateways: 0])" || return 1

    check=$(gwcli ls iscsi-targets/ | grep 'o- disks' | awk -F'[' '{print $2}')
    test "$check" == "Disks: 0]" || return 1

    check=$(gwcli ls iscsi-targets/ | grep 'o- gateways' | awk -F'[' '{print $2}')
    test "$check" == "Up: 0/0, Portals: 0]" || return 1

    check=$(gwcli ls iscsi-targets/ | grep 'o- host-groups' | awk -F'[' '{print $2}')
    test "$check" == "Groups : 0]" || return 1

    check=$(gwcli ls iscsi-targets/ | grep 'o- hosts' | awk -F'[' '{print $2}')
    test "$check" == "Auth: ACL_ENABLED, Hosts: 0]" || return 1

echo "Create the first gateway"
    export HOST=`python3 -c "import socket; print(socket.getfqdn())"`
    export IP=`hostname -i | awk '{print $NF}'`

    expect_true gwcli iscsi-targets/iqn.2003-01.com.redhat.iscsi-gw:ceph-gw/gateways create ip_addresses=$IP gateway_name=$HOST
    check=$(gwcli ls iscsi-targets/ | grep 'o- gateways' | awk -F'[' '{print $2}')
    test "$check" == "Up: 1/1, Portals: 1]" || return 1
