#!/bin/bash

set -ex
trap "bash tests/teardown.sh" EXIT
source bootstrap
bash tests/setup.sh
tox
