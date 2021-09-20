Development Philosophy
======================

``ceph-workbench`` is a command-line toolbox for `Ceph <http://ceph.com>`_.

All functionality is 100% covered by integration and unit tests that
can be run using ``tox``_.

.. _tox: http://codespeak.net/tox/

To this end, the test suite spawns containers running Redmine, GitLab,
etc. instances and shuts them down when the tests complete.

Each new feature is first developed as a set of manually run
snippets and tools, outside of ``ceph-workbench``, and only those
features that prove useful are implemented and tested as
``ceph-workbench`` subcommands. This is an effective way to make sure
a problem exists before the solution is implemented.

``ceph-workbench`` does not claim to be useful to all developers
working on Ceph, only to those who chose to participate in the
development of ``ceph-workbench``.

