# -*- mode: python; coding: utf-8 -*-
#
# Copyright (C) 2016 <contact@redhat.com>
#
# Author: Loic Dachary <loic@dachary.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#
# From the ceph-workbench source directory
#
# apt-get install nginx + add autoindex on; to /etc/nginx/site-*/default
# rm -fr /usr/share/nginx/html/ceph-fixture*
# bash -x tests/setup-ceph-fixture.sh tests/ceph-fixture /usr/share/nginx/html/ceph-fixture # noqa
#
# git fetch ; git reset --hard origin/wip-release
# PYTHONPATH=teuthology ceph-workbench '--verbose'   release          '--version' '10.1.0'            '--key-name' 'teuthology-test' --key-filename ~/.ceph-workbench/teuthology-test.pem --ceph-workbench-branch wip-release --ceph-workbench-git-url http://ceph-workbench.dachary.org/dachary/ceph-workbench.git          '--suite' 'buildpackages/tests'            '--suite-repo'            'http://github.com/dachary/ceph-qa-suite'            '--suite-branch' 'wip-buildpackages'            '--ceph-repo' 'http://167.114.249.129/ceph-fixture'             '--filter' 'ubuntu_14.04,centos_7.0'  # noqa
#
# On the teuthology cluster, try the build phase with:
#
# cd ceph-workbench
# git fetch ; git reset --hard origin/wip-release
# ip=$(hostname -I | cut -f1 -d' ')
# bash -x tests/setup-ceph-fixture.sh tests/ceph-fixture /usr/share/nginx/html/ceph-fixture # noqa
# ceph-workbench --verbose release --tag-phase --build-phase --release-phase --wait --ceph-repo http://$ip/ceph-fixture --suite-repo http://github.com/dachary/ceph-qa-suite --key-filename /home/ubuntu/.ceph-workbench/teuthology.pem --key-name teuthology -s buildpackages/tests --suite-branch wip-buildpackages -v --version 10.1.0 --filter centos_7.2 # noqa
#
#
# Test builds etc. but do not cleanup afterwards for debug & analysis
#
# rm -fr /tmp/ceph-fixture* /tmp/gpg ; TEST_USE_TMP=yes TEST_NO_CLEANUP=true tox -e py27 -- -vv -k test_run -s tests/test_release.py # noqa
#
# Run only the merge phase
#
# TEST_USE_TMP=yes TEST_NO_BUILD_PACKAGE=yes TEST_NO_CLEANUP=true tox -e py27 -- -vv -k test_run -s tests/test_release.py # noqa
#
# Reset the ceph-workbench found in the teuthology cluster
#
# ip=IP-OF-TEUTHOLOGY-TEST
# ssh -i /tmp/teuthology-test.pem ubuntu@$ip bash -c "'cd ceph-workbench ; git fetch ; git reset --hard origin/wip-package'" # noqa
#
import logging
import mock
import os
import pytest  # noqa # it provides caplog
import shutil
import tempfile
import textwrap

from ceph_workbench import ceph_qa_suite
from ceph_workbench import release
from ceph_workbench import util
from teuthology.openstack import OpenStackInstance

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestCephRelease(object):

    def setup(self):
        self.d = tempfile.mkdtemp()

    def teardown(self):
        shutil.rmtree(self.d)

    def test_merge_deb_fix_conf(self):
        shutil.copytree('tests/debian-repository',
                        self.d + '/debian-repository')
        incoming = self.d + '/debian-repository/tmp/debian-testing'
        outgoing = self.d + '/debian-repository/debian-testing'
        assert not os.path.exists(outgoing + '/conf/updates')
        distributions = open(outgoing + '/conf/distributions').read()
        assert 'Update: trusty' not in distributions
        assert 'SignWith: default' not in distributions
        assert 'jessie' in distributions
        release.CephRelease.merge_deb_fix_conf(incoming, outgoing)
        assert incoming in open(outgoing + '/conf/updates').read()
        distributions = open(outgoing + '/conf/distributions').read()
        assert 'Update: trusty' in distributions
        assert 'SignWith: default' in distributions
        assert 'jessie' in distributions

    def test_discover_repositories(self):
        url = 'file:tests/downloads-fixture/index.html'
        actual = release.CephRelease.discover_repositories(url)
        assert ['file:tests/downloads-fixture/debian-testing',
                'file:tests/downloads-fixture/debian-hammer',
                'file:tests/downloads-fixture/rpm-testing'] == actual

    def test_discover_builds(self, caplog):
        url = 'file:tests/packages-repository-fixture/index.html'
        sha1 = 'efc8134f669743f4946297eac89aa0fd46a19dae'
        version = 'v10.0.3'
        actual = release.CephRelease.discover_builds(url, version, sha1)
        assert 'skip because the sha1' in caplog.text()
        assert [{'arch': 'x86_64',
                 'dist': 'centos6',
                 'flavor': 'basic',
                 'pkg_type': 'rpm',
                 'project': 'ceph',
                 'sha1': 'efc8134f669743f4946297eac89aa0fd46a19dae',
                 'url': 'file:tests/packages-repository-fixture/ceph-rpm-centos6-x86_64-basic/ref/v10.0.3',  # noqa
                 'version': 'v10.0.3'},
                {'arch': 'x86_64',
                 'dist': 'trusty',
                 'flavor': 'blkin',
                 'pkg_type': 'deb',
                 'project': 'ceph',
                 'sha1': 'efc8134f669743f4946297eac89aa0fd46a19dae',
                 'url': 'file:tests/packages-repository-fixture/ceph-deb-trusty-x86_64-blkin/ref/v10.0.3',  # noqa
                 'version': 'v10.0.3'}] == actual

    @mock.patch('ceph_workbench.delegate.get_config_dir')
    def test_run(self, m_get_config_dir, caplog):
        if 'OS_AUTH_URL' not in os.environ:
            pytest.skip('no OS_AUTH_URL environment variable')
        #
        # something, somewhere updates ChangeLog and there is a beer
        # bounty for whoever discover what is responsible for that
        #
        util.sh("git checkout ChangeLog")
        #
        # ensure the cluster exists and runs this version of
        # ceph-workbench
        #
        if util.sh("git status --short"):
            util.sh("git --no-pager diff")
            pytest.fail('git status claims there are uncommited changes')
        public = util.sh("git branch -r --points-at "
                         "$(git rev-parse HEAD) | "
                         "grep -v ' -> ' | head -1").strip()
        if not public:
            pytest.fail('git push to a publicly available repository')
        (remote, branch) = public.split('/')
        remote_url = util.sh("git remote get-url " + remote).strip()
        if 'git@' in remote_url:  # no ssh access, turn to http
            remote_url = (remote_url.
                          replace(':', '/').
                          replace('git@', 'http://'))
        if 'TEST_USE_TMP' in os.environ:
            config_dir = '/tmp'
        else:
            config_dir = self.d
        m_get_config_dir.return_value = config_dir
        if 'TEST_NO_BUILD_PACKAGE' not in os.environ:
            self.run_build(config_dir, remote_url, branch, caplog)
        if 'TEST_NO_MERGE' not in os.environ:
            self.run_merge(config_dir)
        if 'TEST_NO_CLEANUP' not in os.environ:
            self.run_cleanup(config_dir)

    def run_build(self, config_dir, remote_url, branch, caplog):
        util.sh("""
        set -x
        for server in teuthology-test packages-repository ; do
            openstack server set --name REMOVE-ME-$server $server
            openstack server delete REMOVE-ME-$server
        done
        true # it's ok if we fail to remove non existent instances
        """)
        c = ceph_qa_suite.CephQaSuite.factory([
            '--verbose',
            '--ceph-workbench-branch', branch,
            '--ceph-workbench-git-url', remote_url,
            '--key-name', 'teuthology-test',
            '--suite', 'dummy', '--dry-run',
            '--name', 'teuthology-test',
            '--wait',
        ])
        assert c.run()
        assert 'Scheduling dummy/{all/nop.yaml}' in caplog.text()
        #
        # create a fake ceph repository that packages quickly
        #
        ip = OpenStackInstance('teuthology-test').get_floating_ip_or_ip()
        util.sh("tests/setup-ceph-fixture.sh tests/ceph-fixture " +
                config_dir + "/ceph-fixture")
        util.sh("scp " +
                " -o StrictHostKeyChecking=false" +
                " -i " + config_dir + "/teuthology-test.pem" +
                " -r " + config_dir + "/ceph-fixture ubuntu@" + ip +
                ":/usr/share/nginx/html/ceph-fixture")
        #
        # build and release the fake ceph package
        #
        c = release.CephRelease.factory([
            '--verbose',
            '--coverage',
            '--version', '10.0.4',
            '--key-name', 'teuthology-test',
            '--suite', 'buildpackages/tests',
            #
            # Keep these around for when a ceph-qa-suite branch is
            # needed for tests.
            #
            #  '--suite-repo',
            #  'http://github.com/dachary/ceph-qa-suite',
            #  '--suite-branch', 'wip-buildpackages',
            '--ceph-repo', 'http://' + ip + '/ceph-fixture',
            '--name', 'teuthology-test',
            '--filter', 'ubuntu_14.04.yaml,centos_7.2.yaml',
        ])
        assert 0 == c.run()

    def run_merge(self, config_dir):
        teuthology = OpenStackInstance(
            'teuthology-test').get_floating_ip_or_ip()
        packages = OpenStackInstance(
            'packages-repository').get_floating_ip_or_ip()
        command = textwrap.dedent("""
        set -ex
        cat > /tmp/.coveragerc <<EOF
        [run]
        data_file=/tmp/.coverage
        EOF
        source .bashrc_teuthology
        cd /usr/share/nginx/html
        rm -fr debian-testing rpm-testing
        #
        # repositories do not exist, do not
        # merge, just copy
        #
        coverage run --append \
            --source=$HOME/ceph-workbench/ceph_workbench \
            $HOME/teuthology/virtualenv/bin/ceph-workbench --verbose release \
            --version 10.0.4 \
            --merge-phase \
            --merge-from http://{packages}/
        test -d debian-testing
        test -d rpm-testing
        #
        # empty the repositories
        #
        cd debian-testing
        mv conf/distributions .distributions
        rm -r *
        mkdir conf
        mv .distributions conf/distributions
        cd ..
        rm -r rpm-testing/*
        #
        # merge with the empty repositories
        #
        coverage run --append \
            --source=$HOME/ceph-workbench/ceph_workbench \
            $HOME/teuthology/virtualenv/bin/ceph-workbench --verbose release \
            --version 10.0.4 \
            --merge-phase \
            --merge-from http://{packages}/
        test -d debian-testing/dists
        test -d rpm-testing/el7/SRPMS
        #
        # verify the result
        #
        wget http://{packages}/release-key.asc
        coverage run --append \
            --source=$HOME/ceph-workbench/ceph_workbench \
            $HOME/teuthology/virtualenv/bin/ceph-workbench --verbose release \
            --version 10.0.4 \
            --verify-phase \
            --verify http://{teuthology}/
        """.format(packages=packages,
                   teuthology=teuthology))
        util.sh("ssh " +
                " -o StrictHostKeyChecking=false" +
                " -i " + config_dir + "/teuthology-test.pem" +
                " ubuntu@" + teuthology + " bash", command)

    def run_cleanup(self, config_dir):
        ip = OpenStackInstance('teuthology-test').get_floating_ip_or_ip()
        util.sh("scp " +
                " -o StrictHostKeyChecking=false" +
                " -i " + config_dir + "/teuthology-test.pem" +
                " ubuntu@" + ip + ":/tmp/.coverage" +
                " .tox/.coverage.remote")
        util.sh("openstack server delete "
                "teuthology-test packages-repository || true")

# Local Variables:
# compile-command: "cd .. ; tox -e py27 tests/test_release.py"
# End:
