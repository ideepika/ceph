# -*- mode: python; coding: utf-8 -*-
#
# Copyright (C) 2015 <contact@redhat.com>
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
import logging
import mock
import os
import pytest  # noqa # it provides caplog
import shutil
import tempfile

from ceph_workbench import ceph_qa_suite
from ceph_workbench import util

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestCephQaSuite(object):

    def setup(self):
        self.d = tempfile.mkdtemp()

    def teardown(self):
        shutil.rmtree(self.d)

    @mock.patch('ceph_workbench.util.get_config_dir')
    def test_run(self, m_get_config_dir, caplog):
        if 'OS_AUTH_URL' not in os.environ:
            pytest.skip('no OS_AUTH_URL environment variable')
        m_get_config_dir.return_value = self.d + "/verify_keys"
        util.sh("openstack server delete teuthology-test || true")
        c = ceph_qa_suite.CephQaSuite.factory([
            '--verbose',
            '--key-name', 'teuthology-test',
            '--suite', 'dummy', '--dry-run',
            '--name', 'teuthology-test',
            '--wait', '--teardown',
        ])
        assert c.run()
        assert 'Scheduling dummy/{all/nop.yaml}' in caplog.text()
        util.sh("openstack server delete teuthology-test || true")


# Local Variables:
# compile-command: "cd .. ; tox -e py27 tests/test_ceph_qa_suite.py"
# End:
