Install
=======

git clone --recursive https://lab.fedeproxy.eu/ceph/ceph-backport/

Running the tests
=================

* tests/setup.sh
* tests/test-ceph-backport.sh |& tee /tmp/out

Hacking
=======

* direnv allow .
* pip install poetry
* poetry install --dev
* pre-commit install
