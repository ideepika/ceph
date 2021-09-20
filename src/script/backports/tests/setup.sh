#!/bin/bash

set -e

if test "$(id -u)" != 0 ; then
    SUDO=sudo
fi

if ! type jq >&/dev/null ; then
    $SUDO apt-get update
    $SUDO apt-get install -y jq
fi

tests/setup-gitea.sh "$@"
( cd ceph-workbench ; tests/setup-redmine.sh "$@" )
