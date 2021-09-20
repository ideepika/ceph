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
import argparse
import fixtures
import logging
import testtools

from ceph_workbench import util
from ceph_workbench import wbxref

REDMINE = {
    'url': 'http://localhost:8081',
    'username': 'admin',
    'password': 'admin',
}

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestWBXref(testtools.TestCase):

    def setUp(self):
        super(TestWBXref, self).setUp()
        fixture = fixtures.TempDir()
        self.useFixture(fixture)
        self.d = fixture.path
        util.sh("""
        cd {dir}
        git init
        """.format(dir=self.d))
        self.argv = [
            '--git-directory', self.d,
            '--redmine-url', REDMINE['url'],
            '--redmine-user', REDMINE['username'],
            '--redmine-password', REDMINE['password'],
            '--gitlab-url', 'http://127.0.0.1:8181',
            '--gitlab-token', 'TOKEN',
            '--gitlab-repo', 'root/testrepo',
        ]

    def test_init(self):
        wbxref.WBXref(argparse.Namespace())

    def test_merge_message2issue(self):
        expected = '1234'
        for message in ("Merge pull request from wip-" + expected,
                        "remote-tracking branch 'bac/wip-" + expected):
            self.assertEqual([expected],
                             wbxref.WBXref.merge_message2issue(message))

    def test_message2issue(self):
        expected = '1234'
        for message in ("http://tracker.ceph.com/issues/" + expected,
                        "fix " + expected,
                        "Fixes:#" + expected,
                        "Fixes: #" + expected,
                        "Fixes " + expected,
                        "Fixes #" + expected,
                        "ref " + expected,
                        "Refs:#" + expected,
                        "Refs: #" + expected,
                        "Refs " + expected,
                        "Refs #" + expected):
            self.assertEqual([expected],
                             wbxref.WBXref.message2issue(message))

    def test_update_issue(self):
        x = wbxref.WBXref(argparse.Namespace())
        merge_id = 3
        merge_request = {
            'id': merge_id,
        }
        issue_ids = set([30, 40])
        x.update_issue(merge_request, issue_ids)
        for issue_id in issue_ids:
            self.assertIn(issue_id, x.issue_id2merge_requests)
        self.assertEqual(issue_ids, merge_request['issues'])

    def test_update_commit2issues(self):
        issue0 = '500'
        issue1 = '600'
        util.sh("""
        cd {dir}
        set -x
        echo > README ; git add README ; git commit -m 'initial' README
        git branch cuttlefish
        git branch next
        git checkout next
        echo a >> README ; git commit -m 'head' README
        git checkout master
        git merge --no-ff -m 'merge' next

        git checkout next
        echo a >> README ; git commit -m 'head' README
        git checkout master
        git merge --no-ff -m 'Merge pull request from wip-{issue0}' next

        git checkout next
        echo a >> README ; git commit -m 'head' README
        git checkout master
        git merge --no-ff -m "remote-tracking branch 'abc/wip-{issue1}" next
        """.format(dir=self.d,
                   issue0=issue0,
                   issue1=issue1))
        x = wbxref.WBXref.factory(self.argv)
        x.open()
        x.update_commit2issues()
        actual = set()
        for issues in x.commit2issues.values():
            actual.update(issues)
        self.assertEqual(set([issue0, issue1]), actual)

    def test_update_issues_with_merge(self):
        util.sh("""
        cd {dir}
        set -x
        echo > README ; git add README ; git commit -m 'initial' README
        git branch cuttlefish

        # merge the next branch in master
        git branch next
        git checkout next
        echo a >> README ; git commit -m 'head' README
        git checkout master
        git merge --no-ff -m 'merge' next

        # branch firefly from master
        git branch firefly
        git tag --annotate -m tag v0.80
        git branch firefly-backports

        # a merge request merged into master
        git checkout master
        git checkout -b pull/1/head
        echo a >> README ; git commit -m 'head' README
        echo a >> README ; git commit -m 'head' README
        git checkout master
        git merge --no-ff -m 'merge' pull/1/head

        # a merge request already merged in firefly-backports
        git checkout firefly
        git checkout -b pull/2/head
        echo b >> README ; git commit -m 'head' README
        git checkout firefly-backports
        git merge --no-ff -m 'merge' pull/2/head

        # a merge request waiting to be merged in firefly
        git checkout firefly
        git checkout -b pull/3/head
        echo c >> README ; git commit -m 'head' README
        echo c >> README ; git commit -m 'head' README
        echo c >> README ; git commit -m 'head' README

        git checkout firefly
        git checkout -b pull/4/head

        git checkout master

        """.format(dir=self.d))
        x = wbxref.WBXref.factory(self.argv + ['--releases=firefly'])
        x.open()
        x.git.index()
        merge1 = 10
        merge2 = 20
        merge3 = 30
        merge4 = 40
        x.gitlab.merge_requests = [
            {  # already merged
                'id': merge1,
                'source_branch': 'pull/1/head',
                'target_branch': 'master',
                'urls': 'URLS',
            },
            {  # integrated in firefly-backports
                'id': merge2,
                'source_branch': 'pull/2/head',
                'target_branch': 'firefly',
                'urls': 'URLS',
            },
            {  # waiting to be integrated in firefly-backports
                'id': merge3,
                'source_branch': 'pull/3/head',
                'target_branch': 'firefly',
                'urls': 'URLS',
            },
            {  # no commit
                'id': merge4,
                'source_branch': 'pull/4/head',
                'target_branch': 'master',
                'urls': 'URLS',
            },
        ]
        x.update_issues_with_merge()
        self.assertEqual({'firefly': 'firefly-backports'},
                         x.git.release2backports)
        self.assertEqual({'firefly': set([merge2])}, x.release2integrated)
        self.assertEqual({'firefly': set([merge3])}, x.release2pending)
        total_commits = 0
        for merge_request in x.gitlab.merge_requests:
            total_commits += len(merge_request['commits'])
        self.assertEqual(total_commits, len(x.commit2merge_requests))

    def test_update_issues_with_comments(self):
        util.sh("""
        cd {dir}
        set -x
        echo > README ; git add README ; git commit -m 'initial' README
        git branch cuttlefish

        git branch next
        git checkout next
        echo a >> README ; git commit -m 'head' README
        git checkout master

        # branch firefly from master
        git branch firefly
        git tag --annotate -m tag v0.80
        git branch firefly-backports

        git checkout master
        git checkout -b pull/1/head
        echo a >> README ; git commit -m 'head' README
        echo a >> README ; git commit -m 'head' README
        git checkout master
        git merge --no-ff -m 'merge' pull/1/head

        """.format(dir=self.d))
        x = wbxref.WBXref.factory(self.argv + ['--releases=firefly'])
        x.open()
        x.gitlab.gitlab = {
            'url': 'GITLAB_URL',
            'namespace': 'NAMESPACE',
            'name': 'NAME',
        }
        x.git.index()
        commit = x.git.r.rev_parse('pull/1/head')
        merge_id = 10
        merge_request = {
            'id': merge_id,
            'urls': 'URLS',
            'target_branch': 'firefly',
        }
        x.gitlab.merge_id2merge_request = {
            10: merge_request,
        }
        x.commit2merge_requests = {
            commit.hexsha: set([merge_id]),
        }
        x.redmine.issues = {
            100: {
                'id': 100,
                'description': 'https://github.com/ceph/ceph/pull/1',
            },
            200: {
                'id': 200,
                'backports': set(['firefly']),
                'description': 'commit:' + commit.hexsha,
            },
            300: {
                'id': 300,
                'description': '',
                'journals': [{
                    'notes': 'https://github.com/ceph/ceph/pull/1',
                }],
            },
        }
        x.gitlab.pull2merge = {
            '1': 10,
        }
        x.update_issues_with_comments()
        #
        # issue 100 description links to
        #    pull request 1
        #    which is merge request 10
        #
        # issue 200 description links to
        #    commit which belongs to
        #    pull request 1
        #    which is merge request 10
        #
        self.assertEqual(set([100, 200]),
                         x.gitlab.merge_id2merge_request[10]['issues'])
        #
        # issue 300 notes links to
        #    pull request 1
        #    which is merge request 10
        #
        related_issues = x.gitlab.merge_id2merge_request[10]['related_issues']
        self.assertEqual(set([300]), related_issues)

    def test_update_cherry_pick_or_link(self):
        util.sh("""
        cd {dir}
        set -x
        echo > README ; git add README ; git commit -m 'initial' README
        git branch cuttlefish

        # branch firefly from master
        git branch firefly
        git tag --annotate -m tag v0.80

        # a merge request merged into master
        git checkout master
        git checkout -b pull/1/head
        echo a >> README ; git commit -m 'head' README
        picked=$(git rev-parse HEAD)
        git checkout master
        git merge --no-ff -m 'merge' pull/1/head

        # cherry-pick a commit from master to firefly
        git checkout firefly
        git cherry-pick -x $picked
        git branch mark1
        echo a >> README ; git commit -m 'Fixes: #1234' README
        git branch mark2

        git log tags/v0.80..heads/firefly
        """.format(dir=self.d))
        x = wbxref.WBXref.factory(self.argv + ['--releases=firefly'])
        x.open()
        x.git.index()
        pull1 = x.git.r.rev_parse('pull/1/head')
        merge_id = 10
        x.commit2merge_requests = {
            pull1.hexsha: set([merge_id]),
        }
        x.update_cherry_pick_or_link()
        #
        # pull request 2 in firefly is cherry picked
        # from pull request 1 that is in master
        #
        mark1 = x.git.r.rev_parse('mark1')
        self.assertEqual(set([10]), x.commit2merge_requests[mark1.hexsha])
        mark2 = x.git.r.rev_parse('mark2')
        self.assertEqual(set(['1234']), x.commit2issues[mark2.hexsha])

    def test_sanity_checks(self):
        x = wbxref.WBXref.factory(self.argv + ['--releases=firefly'])
        x.open()
        issue_id = 100
        x.redmine.issues = {
            issue_id: {
                'id': issue_id,
                'description': 'https://github.com/ceph/ceph/pull/1',
                'status': {
                    'name': 'Pending Backport',
                },
                'backports': set(),
            },
        }
        merge_id = 10
        x.merge_id2merge_request = {
            merge_id: {
                'target_branch': 'firefly',
                'urls': 'URLS',
            },
        }
        x.issue_id2merge_requests = {
            issue_id: set([merge_id])
        }
        self.assertFalse(x.sanity_checks())
        x.redmine.issues[issue_id]['backports'] = set(['firefly'])
        self.assertTrue(x.sanity_checks())
        x.issue_id2merge_requests = {}
        self.assertTrue(x.sanity_checks())
        x.redmine.issues[issue_id]['status']['name'] = 'opened'
        self.assertTrue(x.sanity_checks())
        x.redmine.issues[issue_id] = {}
        self.assertTrue(x.sanity_checks())
