ceph-workbench
==============

ceph-workbench is a command-line toolbox for `Ceph <http://ceph.com>`_

Documentation : http://ceph-workbench.readthedocs.org/
Home page : https://pypi.python.org/pypi/ceph-workbench

Installation
============

* Install Docker http://docs.docker.com/engine/installation/

* Copy the following to ``~/.bashrc``::

    eval "$(docker run dachary/ceph-workbench install)"

* Verify that it works::

    ceph-workbench --help

* Optionally copy your OpenStack ``$PROJECT-openrc.sh`` file to
  ``~/.ceph-workbench/openrc.sh``: the ``ceph-qa-suite`` subcommand will
  use it.

Hacking
=======

* git clone --recursive https://lab.fedeproxy.eu/ceph/ceph-workbench
* cd ceph-workbench
* echo layout python3 > .envrc
* direnv allow .
* pip install poetry
* poetry install
* pre-commit install

* Get the code:: 


* Set up the development environment::

   deactivate || true ; source bootstrap

  This creates a virtualenv containing the :code:`ceph-workbench`
  executable and everything it needs to work.

* Activate the development environment and run :code:`ceph-workbench`::

   source virtualenv/bin/activate
   PYTHONPATH=teuthology ceph-workbench --help

* Run the tests (requires OpenStack credentials to get 100% coverage)::

   deactivate || true ; bash run-tests.sh

* Sync the teuthology submodule::

   git submodule update --remote teuthology

* Run a single test::

   tox -e py27 -- -s -k test_run tests/test_ceph_qa_suite.py

* Run ceph-workbench using the dev environment of the current working
  directory in the docker container instead of the installed version::

   eval "$(docker/entrypoint.sh install)"
   ceph-workbench --help # use what is installed in the container
   ceph-workbench-debug --help # use ceph-workbench from the working directory
   ceph-workbench-shell bash # login the container and debug

* Check the documentation : rst2html < README.rst > /tmp/a.html

Release management
==================

* Prepare a new version

 - version=1.3.0 ; perl -pi -e "s/^version.*/version = $version/" setup.cfg ; for i in 1 2 ; do python setup.py sdist ; amend=$(git log -1 --oneline | grep --quiet "version $version" && echo --amend) ; git commit $amend -m "version $version" ChangeLog setup.cfg ; git tag -a -f -m "version $version" $version ; done

* Publish a new version

 - python setup.py sdist upload --sign
 - git push ; git push --tags
 - docker rmi dachary/ceph-workbench
 - docker build --no-cache --tag dachary/ceph-workbench docker
 - docker build --tag dachary/ceph-workbench:1.5.9 docker
 - docker login
 - docker push dachary/ceph-workbench
 - docker push dachary/ceph-workbench:1.5.9

* pypi maintenance

 - python setup.py register # if the project does not yet exist
 - trim old versions at https://pypi.python.org/pypi/ceph-workbench
