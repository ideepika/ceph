Directory tree organization
===========================

Introduction
------------

For a given SHA1 in the Ceph tree, packages for various distributions
are created. They are tested using a given SHA1 in the ceph-qa-suite
tree and their results are archived for analysis. Based on the test
results, a contribution will be merged or a release will be published.

Publishing a Ceph stable release is a chain of actions which can be
convenientely expressed in a Makefile: compile, run make check, create
packages, run all suites in ceph-qa-suite. Each step creates files
and directories that are organized as follows:

Definition
----------

Abstract description::

    - ceph-SHA1
      - ceph-qa-suite-SHA1
        - test description A
          - log archive directory 1
          - log archive directory 2
          - ...
          - (optional) OK symlink to successful run
        - test description B
          - log archive directory 3
          - log archive directory 4
        - ...
      - distribution-version A
        - package-repository
        - package-repository.log
        - make-check
        - make-check.log
      - distribution-version B
        - package-repository
        - package-repository.log
        - make-check
        - make-check.log
      ...
    
For instance::

    - ceph-3c41332a0d7f489bf35fadd8fee48fefa4d50b4c
      - ceph-qa-suite-686d6e9c0b53300d9deef26b284f6fc03e15579a
        - 75a7ec085eeb307ee0cbc304e8963f66
          - OK -> ubuntu-2015-12-19_11:19:15-dummy-master---basic-openstack/148
          - ubuntu-2015-12-19_11:19:15-dummy-master---basic-openstack/148
            - teuthology.log
      - centos-7.0
        - ceph-rpm-centos7-x86_64-basic
        - make-check.log

Interpretation
--------------

At the top level ``ceph-SHA1``, all packages and test results based on
the matching SHA1 are grouped together. The ``ceph-qa-suite-SHA1``
subdirectory group all ``ceph-qa-suite`` test results. If the
``ceph-qa-suite`` repository is modified (for instance because tests
are adapted), there can be more than one ``ceph-qa-suite-SHA1``.

The ``ceph-qa-suite-SHA1`` contains at most one directory for each
test description. The test description it self cannot be used because
it contains / and may exceed the maximum allows path length and the
corresponding SHA1 is used instead. Each test description directory
may contain multiple runs and a single symbolic link (OK) to the
directory of one successful run. If a test fails, the OK symlink is
not created but the logs are available for analysis.

In a directory named after the distribution (for instance centos-7.0,
ubuntu-14.04 etc.), the packages are stored in a directory using the
same name convention as the gitbuilders. The ``make-check`` is created
if it passed. For the package respository and make check, the log of
the build process is archived in the .log file, for forensic analysis.

Using make
----------

A makefile can be created to run each test for a given ``Ceph`` and
``ceph-qa-suite`` with rules such as::

   ceph-qa-suite-686d6e9c0b53579a/75a7ec085e/OK:
       teuthology-suite --description 'dummy/{all/nop.yaml}' --wait
       touch $@

so the ``OK`` file only exists if the test is successful. The packages
can be created only if make check has passed with a rule such as::


  ceph-9bf35fadd8f/centos-7.0/ceph-rpm-centos7-x86_64-basic: ceph-9bf35fadd8f/centos-7.0/make-check
       rpmbuild /tmp/package
       mv /tmp/package $@

  ceph-9bf35fadd8f/centos-7.0/make-check:
       make check > $@.log
       touch $@
