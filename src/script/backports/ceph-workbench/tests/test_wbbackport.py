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
import io
import logging
import re
import shutil
import testtools

from ceph_workbench import util
from ceph_workbench import wbbackport
from fixture_xref import FixtureXref

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestWBBackport(testtools.TestCase):

    def setUp(self):
        super(TestWBBackport, self).setUp()
        self.fixture_xref = FixtureXref().setUp()

    def tearDown(self):
        super(TestWBBackport, self).tearDown()
        self.fixture_xref.tearDown()
        b = wbbackport.WBBackport.factory(self.fixture_xref.argv)
        shutil.rmtree(b.wiki_directory, True)

    def lookup_lines(self, lines, regex):
        for s in lines:
            if re.search(regex, s):
                return True
        self.addDetail('output', testtools.content.text_content(
            regex + ' does not match any line in \n' + "\n".join(lines)))
        return False

    def test_inventory_write(self):
        b = wbbackport.WBBackport.factory(
            self.fixture_xref.argv +
            ['--backport-range', 'master..firefly',
             '--releases', 'firefly'])
        util.sh("""
        cd {dir}
        echo 0 > README ; git add README ; git commit -m 'head' README
        git branch cuttlefish

        # branch firefly from master
        git branch firefly
        git tag --annotate -m tag v0.80

        git checkout master
        """.format(dir=b.xref.git.args.git_directory))
        b.open()
        b.inventory_write('master..firefly')

    def test_inventory_isolated_commit(self):
        b = wbbackport.WBBackport.factory(self.fixture_xref.argv +
                                          ['--releases', 'firefly'])
        util.sh("""
        cd {dir}
        echo 0 > README ; git add README ; git commit -m 'head' README
        git branch cuttlefish

        # branch firefly from master
        git branch firefly
        git tag --annotate -m tag v0.80

        # an isolated commit
        git checkout firefly
        echo 1 >> README ; git add README
           git commit -m 'ISOLATED COMMIT' README

        git checkout master
        """.format(dir=b.xref.git.args.git_directory))
        b.open()
        out = io.StringIO()
        b.inventory_release('tags/v0.80..firefly', '', out)

        lines = out.getvalue().split('\n')
        self.assertTrue(self.lookup_lines(
            lines, 'isolated commit.*ISOLATED COMMIT'))

    def test_inventory_issues_no_merge_request(self):
        b = wbbackport.WBBackport.factory(self.fixture_xref.argv +
                                          ['--releases', 'firefly'])
        issue = self.fixture_xref.redmine.issue_create(
            'SUBJECT',
            'DESCRIPTION',
            'BACKPORT',
            'NOTE')
        util.sh("""
        cd {dir}
        echo 0 > README ; git add README ; git commit -m 'head' README
        git branch cuttlefish

        # branch firefly from master
        git branch firefly
        git tag --annotate -m tag v0.80
        git branch firefly-backports

        # a commit not part of a merge request but linked to an issue
        git checkout firefly
        echo c >> README ; git commit -m 'Fixes: #{issue}' README

        git checkout master
        """.format(dir=b.xref.git.args.git_directory,
                   issue=issue['id']))
        b.open()
        out = io.StringIO()
        b.inventory_release('tags/v0.80..firefly', '', out)

        lines = out.getvalue().split('\n')
        self.assertTrue(self.lookup_lines(
            lines, 'firefly Fixes: #' + str(issue['id'])))

    def test_inventory_issues_via_cherry_picking(self):
        """Commit C1 merged in master has no issue associated with it.

        Commit C1 is cherry-picked as C2 in firefly and
            associated with issue I1.
        Commit C1 is cherry-picked as C3 in dumpling.

        When displaying dumpling, and only because C3 is not
        associated with an issue, it is associated with the same issue
        as C2. The general idea is that something is better than
        nothing in this specific case.
        """
        b = wbbackport.WBBackport.factory(self.fixture_xref.argv +
                                          ['--releases', 'dumpling,firefly'])
        util.sh("""
        cd {dir}
        echo 0 >> FILE ; git add FILE ; git commit -m 'head' FILE
        git branch cuttlefish

        # branch dumpling from master
        git branch dumpling
        git tag --annotate -m tag v0.67

        echo 0 >> FILE ; git add FILE ; git commit -m 'head' FILE

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

        # this one will be referenced by an issue
        git checkout firefly
        git cherry-pick -x $picked

        # this one has no link to an issue
        git checkout dumpling
        git cherry-pick -x $picked

        git checkout master
        """.format(dir=b.xref.git.args.git_directory))
        commit = b.xref.git.open().r.rev_parse('firefly')

        issue = self.fixture_xref.redmine.issue_create(
            'SUBJECT',
            'description is commit:' + commit.hexsha,
            'BACKPORT',
            'NOTE')

        b.open()
        out = io.StringIO()
        b.inventory_release('tags/v0.67..dumpling', '', out)

        lines = out.getvalue().split('\n')
        self.assertTrue(self.lookup_lines(
            lines, 'SUBJECT.*/' + str(issue['id'])))

# Local Variables:
# compile-command: "../.tox/py27/bin/py.test  -s -v test_wbbackport.py"
# End:
