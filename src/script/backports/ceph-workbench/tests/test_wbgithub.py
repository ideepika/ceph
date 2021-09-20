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
import testtools

from ceph_workbench import wbgithub
from fixture_github import FixtureGitHub

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestWBGitHub(testtools.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.f = FixtureGitHub()
        cls.f.setUp()
        cls.argv = cls.f.argv

    @classmethod
    def tearDownClass(cls):
        cls.f.tearDown()

    def test_index(self):
        g = wbgithub.WBGitHub.factory(self.argv)
        g.index()

    def test_pull_request(self):
        name = 'name'
        number = self.f.pull_request(name, 'body', 'master')
        pr = self.f.github.repos().pulls(number).get()
        assert name == pr['title']
