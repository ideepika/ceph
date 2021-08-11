#!/usr/bin/env bash
set -ex

mydir=`dirname $0`
test_script="$@"

export ISCSI_CONTAINER=$(sudo podman ps -a | grep -F 'iscsi' | grep -Fv 'tcmu' | awk '{print $1}')

sudo podman cp $mydir/$test_script $ISCSI_CONTAINER:bin/$test_script

sudo podman exec $ISCSI_CONTAINER $test_script