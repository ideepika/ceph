#!/bin/bash

set -e

CEPH_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || true)

if test -z "$CEPH_ROOT"; then
    source $(dirname $0)/detect-build-env-vars.sh
fi

#
# Only run if ceph-backports.sh was modified because:
#
# - it is standalone
# - the tests require significant resources (two docker containers)
# - they take a few minutes to complete
#
SCRIPT=src/script/ceph-backport.sh

if ! git diff --stat ${GITHUB_BASE:=origin/master}..${GITHUB_SHA:=HEAD} | grep -q $SCRIPT ; then
    echo SKIP because $SCRIPT is not modified
    exit 0
fi

cd $CEPH_ROOT/src/script/backports

venv_path=$(mktemp -d)

trap "tests/setup.sh teardown; rm -fr $venv_path" EXIT

$CEPH_ROOT/src/tools/setup-virtualenv.sh ${venv_path}
source ${venv_path}/bin/activate
pip install poetry
poetry install
poetry run pre-commit run --config .pre-commit-config.yaml --files ceph-workbench/ceph_workbench/*.py tests/*.py
poetry run tests/setup.sh
poetry run tests/test-ceph-backport.sh
