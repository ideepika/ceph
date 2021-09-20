ceph-workbench
==============

`ceph-workbench
<http://ceph-workbench.dachary.org/root/ceph-workbench>`_ is a
:ref:`GPLv3+ Licensed <gplv3>` command-line toolbox for `Ceph <http://ceph.com>`_.

Installation
------------

* Install Docker http://docs.docker.com/engine/installation/

* Copy the following shell function to ``~/.bashrc``::

    eval "$(docker run dachary/ceph-workbench install)"

* Verify that it works::

    ceph-workbench --help

* Optionally link ceph-workbench with your OpenStack tenant (for use with
  the ``ceph-qa-suite`` subcommand::

  1. Download your ``openrc.sh`` file by clicking on the "Download
     OpenStack RC File" button, which can be found in the "API Access" tab
     of the "Access & Security" dialog of the OpenStack Horizon dashboard.

  2. Create a ``~/.ceph-workbench`` directory, set its permissions to
     700, and move the ``openrc.sh`` file into it. Make sure that the
     filename is exactly `~/.ceph-workbench/openrc.sh`.

  3. Edit the file so it does not ask for your OpenStack password
     interactively. Comment out the relevant lines and replace them with
     something like::
  
         export OS_PASSWORD="aiVeth0aejee3eep8rogho3eep7Pha6ek"
  
    
User Guide
----------

The document `Contributing to Ceph: A Guide for Developers
<http://docs.ceph.com/docs/master/dev/>`_ explains the context in
which ``ceph-workbench`` can be used.

Contributor Guide
-----------------

If you want to contribute to ``ceph-workbench``, this part of the documentation is for
you.

.. toctree::
   :maxdepth: 1

   dev/philosophy
   dev/authors
   dev/namespace
