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
from ceph_workbench import wbgitlab
import logging
import os
import requests
import tempfile
import time


class FixtureGitLab(object):

    def setUp(self):
        r = requests.post('http://127.0.0.1:8181/api/v3/session',
                          data={'login': 'root', 'password': '5iveL!fe'})
        r.raise_for_status()

        git = 'ssh://git@127.0.0.1:8022'
        host = 'http://127.0.0.1:8181'
        token = r.json()['private_token']
        self.argv = [
            '--gitlab-git', git,
            '--gitlab-url', host,
            '--gitlab-token', token,
            '--gitlab-repo', 'root/testrepo',
        ]
        self.gitlab = wbgitlab.WBGitLab.factory(self.argv)
        self.add_ssh_key()
        self.remove_project()
        self.add_project()

    def tearDown(self):
        self.remove_project()

    def add_ssh_key(self):
        (fd, self.key_file) = tempfile.mkstemp()
        util.sh("yes | ssh-keygen -q -N '' -f " + self.key_file)
        g = self.gitlab.gitlab
        url = g['url'] + "/projects/" + g['repo']
        query = {'private_token': g['token']}
        logging.info("add key " + g['repo'])
        url = g['url'] + "/user/keys"
        query['title'] = 'key'
        query['key'] = open(self.key_file + '.pub').read()
        result = requests.post(url, params=query)
        if result.status_code != requests.codes.created:
            raise ValueError(result.text)

        (fd, self.ssh) = tempfile.mkstemp()
        script = ('#!/bin/bash\n' +
                  'ssh -i ' + self.key_file +
                  ' -o StrictHostKeyChecking=false ' +
                  ' "$@"\n')
        os.write(fd, script.encode('utf-8'))
        os.close(fd)
        os.chmod(self.ssh, 0o755)
        os.environ['GIT_SSH'] = self.ssh

    def remove_ssh_key(self):
        os.unlink(self.key_file)
        os.unlink(self.key_file + '.pub')
        os.unlink(self.ssh)

    def add_project(self):
        g = self.gitlab.gitlab
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
        return result.json()

    def remove_project(self):
        g = self.gitlab.gitlab
        requests.delete(g['url'] + "/projects/" + g['repo'],
                        params={'private_token': g['token']})
        deleted = False
        for delay in (1, 2, 4, 8, 16, 32, 64, 128):
            result = requests.get(g['url'] + "/projects/" + g['repo'],
                                  params={'private_token': g['token']})
            if result.status_code == 404:
                deleted = True
                break
            time.sleep(delay)
        if not deleted:
            raise ValueError('failed to delete ' +
                             g['url'] + "/projects/" + g['repo'])
