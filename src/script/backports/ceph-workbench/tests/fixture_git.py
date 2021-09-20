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
from ceph_workbench import util
import shutil
import tempfile


class FixtureGit(object):

    def setUp(self):
        self.d = tempfile.mkdtemp()
        util.sh("""
        cd {dir}
        git init
        echo a >> master.txt ; git add master.txt ; git commit -m 'master' master.txt # noqa
        git branch cuttlefish
        echo a >> cuttlefish.txt ; git add cuttlefish.txt ; git commit -m 'cuttlefish' cuttlefish.txt # noqa
        git checkout master
        """.format(dir=self.d))
        self.argv = [
            '--git-directory', self.d,
        ]
        return self

    def tearDown(self):
        shutil.rmtree(self.d)
