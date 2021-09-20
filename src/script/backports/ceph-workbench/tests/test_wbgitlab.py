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
import requests
import testtools

from ceph_workbench import wbgitlab

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestWBGitLab(testtools.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.tearDownClass()
        r = requests.post('http://127.0.0.1:8181/api/v3/session',
                          data={'login': 'root', 'password': '5iveL!fe'})
        r.raise_for_status()

        token = r.json()['private_token']
        host = 'http://127.0.0.1:8181'
        cls.argv = [
            '--gitlab-url', host,
            '--gitlab-token', token,
            '--gitlab-repo', 'root/testrepo',
        ]
        cls.gitlab = wbgitlab.WBGitLab.factory(cls.argv)
        cls.add_project()

    @classmethod
    def tearDownClass(cls):
        cls.remove_project()

    @classmethod
    def add_project(cls):
        g = cls.gitlab.gitlab
        url = g['url'] + "/projects/" + g['repo']
        query = {'private_token': g['token']}
        logging.info("add project " + g['repo'])
        url = g['url'] + "/projects"
        query['public'] = 'true'
        query['namespace'] = g['namespace']
        query['name'] = g['name']
        result = requests.post(url, params=query)
        if result.status_code != requests.codes.created:
            raise ValueError(result.text)
            logging.debug("project " + g['repo'] + " added: " +
                          result.text)
        return result.json()

    @classmethod
    def remove_project(cls):
        if hasattr(cls, 'gitlab'):
            g = cls.gitlab.gitlab
            requests.delete(g['url'] + "/projects/" + g['repo'],
                            params={'private_token': g['token']})

    def test_index(self):
        g = wbgitlab.WBGitLab.factory(self.argv)
        g.index()
