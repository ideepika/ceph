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
import pytest  # noqa # it provides caplog

from ceph_workbench import util
from ceph_workbench import wbbackportsetrelease
from fixture_git import FixtureGit
from fixture_github import FixtureGitHub
from fixture_redmine import FixtureRedmine

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestWBBackportSetRelease(object):

    def setup(self):
        self.argv = []
        self.fixture_redmine = FixtureRedmine().setUp()
        self.argv += self.fixture_redmine.argv
        self.fixture_github = FixtureGitHub().setUp()
        self.argv += self.fixture_github.argv
        self.fixture_git = FixtureGit().setUp()
        self.argv += self.fixture_git.argv

    def teardown(self):
        self.fixture_redmine.tearDown()
        self.fixture_github.tearDown()
        self.fixture_git.tearDown()

    @mock.patch.object(wbbackportsetrelease.WBBackportSetRelease,
                       'set_release')
    @mock.patch.object(wbbackportsetrelease.WBBackportSetRelease,
                       'open')
    def test_run(self, m_open, m_set_release):
        i = wbbackportsetrelease.WBBackportSetRelease.factory(
            self.argv)
        i.run()
        m_set_release.assert_called_with()

    def test_set_release(self, caplog):
        i = wbbackportsetrelease.WBBackportSetRelease.factory(
            self.argv + [
                '--dry-run',
                '--git-remote=origin',
                '--releases=cuttlefish,dumpling,firefly',
            ])
        i.set_defaults()
        util.sh("""
        cd {dir}
        git remote add origin {url}
        git fetch origin
        git rebase origin/master

        touch README ; git add README
        echo a >> README ; git commit -m 'head' README

        # branch cuttlefish from master
        git checkout cuttlefish
        git tag --annotate -m tag v0.61
        echo a > file-cuttlefish.txt ; git add file-cuttlefish.txt ; git commit -m 'm' file-cuttlefish.txt # noqa
        git tag --annotate -m tag v0.61.1

        # branch dumpling from master
        git branch dumpling master
        git checkout dumpling
        echo a > file-dumpling.txt ; git add file-dumpling.txt ; git commit -m 'm' file-dumpling.txt # noqa
        git tag --annotate -m tag v0.67

        git checkout master
        echo a >> README ; git commit -m 'head' README

        # branch firefly from master
        git branch firefly master
        git checkout firefly
        git tag --annotate -m tag v0.80
        echo a > file-firefly.txt ; git add file-firefly.txt ; git commit -m 'm' file-firefly.txt # noqa
        git tag --annotate -m tag v0.80.1

        # branch unknownbranch from master
        git branch unknownbranch master
        git checkout unknownbranch
        echo a > file-unknownbranch.txt ; git add file-unknownbranch.txt ; git commit -m 'm' file-unknownbranch.txt # noqa

        git checkout master
        echo a >> README ; git commit -m 'head' README

        git push --tags origin master cuttlefish dumpling firefly unknownbranch
        """.format(url=i.github.get_clone_url(),
                   dir=i.git.args.git_directory))

        i.redmine.open().index()
        i.redmine.r.version.create(project_id='ceph',
                                   name='v0.80.1',
                                   sharing='descendants')
        i.redmine.r.version.create(project_id='ceph',
                                   name='v0.80.2',
                                   sharing='descendants')
        i.github.open()
        tracker_id = i.redmine.tracker2tracker_id['Backport']
        in_progress = i.redmine.status2status_id['In Progress']
        i_url = i.redmine.args.redmine_url + '/issues/'
        g_url = ("http://github.com/" + i.github.args.github_repo +
                 "/pull/")
        #
        # Skip if the pull request is not merged yet.
        #
        issue1 = i.redmine.r.issue.create(project_id='ceph',
                                          tracker_id=tracker_id,
                                          status_id=in_progress,
                                          subject='issue1')
        pr = self.fixture_github.pull_request('issue1',
                                              i_url + str(issue1['id']),
                                              'firefly')
        i.redmine.r.issue.update(issue1['id'],
                                 description=g_url + str(pr))
        #
        # Error: the pull request does not reference the issue
        #
        issue2 = i.redmine.r.issue.create(project_id='ceph',
                                          tracker_id=tracker_id,
                                          status_id=in_progress,
                                          subject='issue2')
        pr = self.fixture_github.pull_request(
            'issue2',
            'missing url to the tracker',
            'firefly')
        self.fixture_github.merge(pr, 'merge comment')
        i.redmine.r.issue.update(issue2['id'],
                                 description=g_url + str(pr))
        #
        # Error: the base branch of the pull request is not a known release
        #
        issue3 = i.redmine.r.issue.create(project_id='ceph',
                                          tracker_id=tracker_id,
                                          status_id=in_progress,
                                          subject='issue3')
        pr = self.fixture_github.pull_request('issue3',
                                              i_url + str(issue3['id']),
                                              'unknownbranch')
        self.fixture_github.merge(pr, 'merge comment')
        i.redmine.r.issue.update(issue3['id'],
                                 description=g_url + str(pr))
        #
        # Error: there are multiple pull requests in the issue description
        #
        description = (
            "http://github.com/" +
            i.github.args.github_repo + "/pull/123 "
            "http://github.com/" +
            i.github.args.github_repo + "/pull/789 "
        )
        issue4 = i.redmine.r.issue.create(project_id='ceph',
                                          tracker_id=tracker_id,
                                          status_id=in_progress,
                                          subject='issue4',
                                          description=description)
        #
        # Error: git describe for the merged pr does not
        # provide a parseable version
        #
        issue5 = i.redmine.r.issue.create(project_id='ceph',
                                          tracker_id=tracker_id,
                                          status_id=in_progress,
                                          subject='issue5')
        pr = self.fixture_github.pull_request('issue5',
                                              i_url + str(issue5['id']),
                                              'dumpling')
        self.fixture_github.merge(pr, 'merge comment')
        i.redmine.r.issue.update(issue5['id'],
                                 description=g_url + str(pr))
        #
        # Error: git describe returns a version that is not known to redmine
        #
        issue6 = i.redmine.r.issue.create(project_id='ceph',
                                          tracker_id=tracker_id,
                                          status_id=in_progress,
                                          subject='issue6')
        pr = self.fixture_github.pull_request('issue6',
                                              i_url + str(issue6['id']),
                                              'cuttlefish')
        self.fixture_github.merge(pr, 'merge comment')
        i.redmine.r.issue.update(issue6['id'],
                                 description=g_url + str(pr))
        #
        # Success: set the release and resolve the issue
        #
        issue7 = i.redmine.r.issue.create(project_id='ceph',
                                          tracker_id=tracker_id,
                                          status_id=in_progress,
                                          subject='issue7')
        pr = self.fixture_github.pull_request('issue7',
                                              i_url + str(issue7['id']),
                                              'firefly')
        self.fixture_github.merge(pr, 'merge comment')
        i.redmine.r.issue.update(issue7['id'],
                                 description=g_url + str(pr))
        r = i.redmine
        i.open()
        logging.debug('FIRST RUN')
        i.set_release()
        l = caplog.text()
        assert r.url(issue1) + ' skipped because' in l
        assert r.url(issue2) + ' expected the PR to reference the issue' in l
        assert (r.url(issue3) + ' unknownbranch is not a known stable branch'
                in l)
        assert r.url(issue4) + ' expected exactly one PR' in l
        assert r.url(issue5) + ' git describe is not parseable' in l
        assert r.url(issue6) + ' v0.61.2 obtained from' in l
        assert r.url(issue7) + ' is set with version' in l
        i.args.dry_run = False
        logging.debug('SECOND RUN')
        i.set_release()
        l = caplog.text()
        assert r.url(issue7) + ' is set with version' in l
        resolved = i.redmine.r.issue.get(issue7['id'])
        assert 'Resolved' == resolved['status'].name

# Local Variables:
# compile-command: "cd .. ; tox -e py27 -- -s tests/test_wbbackportsetrelease.py" # noqa
# End:
