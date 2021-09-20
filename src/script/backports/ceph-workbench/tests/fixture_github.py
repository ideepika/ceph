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
import binascii
from ceph_workbench import util
from ceph_workbench import wbgithub
import github
import logging
import os
import shutil
import tempfile
import time

GITHUB = {
    'token': '2f9990ef48be2255267f6a9a394389e24cc10e31',
    'username': 'ceph-workbench',
    'repo': 'testrepo-' + binascii.hexlify(os.urandom(10)),
    'password': 'Oktowyic8',
}


class FixtureGitHub(object):

    def setUp(self):
        github_url = 'http://{username}:{password}@github.com'.format(
            username=GITHUB['username'],
            password=GITHUB['password'])
        self.argv = [
            '--github-token', GITHUB['token'],
            '--github-repo', GITHUB['username'] + '/' + GITHUB['repo'],
            '--github-url', github_url,
        ]
        self.github = wbgithub.WBGitHub.factory(self.argv).open()
        self.remove_project()
        self.add_project()
        return self

    def tearDown(self):
        self.remove_project()

    def project_exists(self, name):
        retry = 10
        while retry > 0:
            try:
                for repo in self.github.g.user('repos').get():
                    if repo['name'] == name:
                        return True
                return False
            except github.ApiError:
                time.sleep(5)
            retry -= 1
        raise Exception('error getting the list of repos')

    def add_project(self):
        r = self.github.g.user('repos').post(
            name=GITHUB['repo'],
            auto_init=True)
        assert r['full_name'] == GITHUB['username'] + '/' + GITHUB['repo']
        while not self.project_exists(GITHUB['repo']):
            pass

    def remove_project(self):
        if self.project_exists(GITHUB['repo']):
            try:
                self.github.g.repos(GITHUB['username'] + '/' +
                                    GITHUB['repo']).delete()
            except github.ApiNotFoundError:
                pass  # this may happen because removal is asynchronous
        while self.project_exists(GITHUB['repo']):
            pass

    def pull_request(self, name, body, base):
        d = tempfile.mkdtemp()
        util.sh("""
        git clone {url} {d}
        cd {d}
        git branch wip-{name} origin/{base}
        git checkout wip-{name}
        echo a > file-{name}.txt ; git add file-{name}.txt ; git commit -m 'm' file-{name}.txt # noqa
        git push origin wip-{name}
        """.format(url=self.github.get_clone_url(),
                   d=d,
                   name=name,
                   base=base))
        shutil.rmtree(d)
        repos = self.github.g.repos(GITHUB['username'])(GITHUB['repo'])
        retry = 15
        while retry > 0:
            try:
                pr = repos.pulls().post(
                    title=name,
                    base=base,
                    body=body,
                    head='wip-' + name)
                break
            except github.ApiError as e:
                logging.exception("could not create pr " + str(e.response))
                time.sleep(60)
            retry -= 1
        assert retry > 0
        # Wait until the pull request exists
        retry = 10
        while retry > 0:
            try:
                repos.pulls(pr['number']).get()
                break
            except github.ApiError:
                logging.exception("could not get pr " + str(pr['number']))
                time.sleep(5)
            retry -= 1
        assert retry > 0
        return pr['number']

    def merge(self, pr, message):
        retry = 10
        while retry > 0:
            try:
                current = self.github.repos().pulls(pr).get()
                if current['state'] in ('merged', 'closed'):
                    return
                logging.info('state = ' + current['state'])
                self.github.repos().pulls(pr).merge().put(
                    commit_message=message)
            except github.ApiError as e:
                logging.error(str(e.response))
                logging.exception('merging ' + str(pr) + ' ' + message)
                time.sleep(5)
            retry -= 1
        assert retry > 0
