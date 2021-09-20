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

from ceph_workbench import wbbackportcreateissue
from ceph_workbench import wbredmine
from fixture_redmine import FixtureRedmine

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s')


class TestWBBackportCreateIssue(object):

    def setup(self):
        self.argv = []
        self.fixture_redmine = FixtureRedmine().setUp()
        self.argv += self.fixture_redmine.argv

    def teardown(self):
        self.fixture_redmine.tearDown()

    @mock.patch.object(wbbackportcreateissue.WBBackportCreateIssue,
                       'create_issue')
    @mock.patch.object(wbbackportcreateissue.WBBackportCreateIssue,
                       'open')
    def test_run(self, m_open, m_create_issue):
        i = wbbackportcreateissue.WBBackportCreateIssue.factory(
            self.argv)
        i.run()
        m_create_issue.assert_called_with()

    def test_create_issue(self, caplog):
        i = wbbackportcreateissue.WBBackportCreateIssue.factory(self.argv)
        i.open().index()
        tracker_id = i.tracker2tracker_id['Bug']
        backport_tracker = i.tracker2tracker_id['Backport']
        pending_backport = i.status2status_id[
            wbredmine.WBRedmine.pending_backport]
        #
        # Add a backport issue for firefly and hammer
        #
        issue1_releases = set(('firefly', 'hammer'))
        releases = ",".join(issue1_releases)
        issue1 = i.r.issue.create(project_id='ceph',
                                  tracker_id=tracker_id,
                                  status_id=pending_backport,
                                  custom_fields=[{
                                      'id': i.backport_id,
                                      'value': releases,
                                  }],
                                  subject='issue1')
        #
        # Unknown release
        #
        issue2 = i.r.issue.create(project_id='ceph',
                                  tracker_id=tracker_id,
                                  status_id=pending_backport,
                                  custom_fields=[{
                                      'id': i.backport_id,
                                      'value': 'unknown',
                                  }],
                                  subject='issue2')
        #
        # Relation to an issue that is not in the Backport
        # tracker is ignored.
        #
        issue3 = i.r.issue.create(project_id='ceph',
                                  tracker_id=tracker_id,
                                  status_id=pending_backport,
                                  custom_fields=[{
                                      'id': i.backport_id,
                                      'value': 'firefly',
                                  }],
                                  subject='issue3')
        issue4 = i.r.issue.create(project_id='ceph',
                                  tracker_id=tracker_id,
                                  subject='issue4')
        i.r.issue_relation.create(issue_id=issue3['id'],
                                  issue_to_id=issue4['id'],
                                  relation_type='relates')
        #
        # Relation to an issue that is in the Backport
        # tracker but not copied_to is an error
        #
        issue5 = i.r.issue.create(project_id='ceph',
                                  tracker_id=tracker_id,
                                  status_id=pending_backport,
                                  custom_fields=[{
                                      'id': i.backport_id,
                                      'value': 'firefly',
                                  }],
                                  subject='issue5')
        issue6 = i.r.issue.create(project_id='ceph',
                                  tracker_id=backport_tracker,
                                  subject='issue6')
        i.r.issue_relation.create(issue_id=issue5['id'],
                                  issue_to_id=issue6['id'],
                                  relation_type='relates')
        #
        # There are too many backport issues
        #
        issue7 = i.r.issue.create(project_id='ceph',
                                  tracker_id=tracker_id,
                                  status_id=pending_backport,
                                  custom_fields=[{
                                      'id': i.backport_id,
                                      'value': 'firefly',
                                  }],
                                  subject='issue7')
        issue8 = i.r.issue.create(project_id='ceph',
                                  tracker_id=backport_tracker,
                                  custom_fields=[{
                                      'id': i.release_id,
                                      'value': 'firefly',
                                  }],
                                  subject='issue8')
        i.r.issue_relation.create(issue_id=issue7['id'],
                                  issue_to_id=issue8['id'],
                                  relation_type='copied_to')
        issue9 = i.r.issue.create(project_id='ceph',
                                  tracker_id=backport_tracker,
                                  custom_fields=[{
                                      'id': i.release_id,
                                      'value': 'hammer',
                                  }],
                                  subject='issue9')
        i.r.issue_relation.create(issue_id=issue7['id'],
                                  issue_to_id=issue9['id'],
                                  relation_type='copied_to')
        #
        # Pending backport but the backport field is missing
        # or empty
        #
        issue10 = i.r.issue.create(project_id='ceph',
                                   tracker_id=tracker_id,
                                   status_id=pending_backport,
                                   custom_fields=[{
                                       'id': i.backport_id,
                                       'value': '',
                                   }],
                                   subject='issue10')
        issue11 = i.r.issue.create(project_id='ceph',
                                   tracker_id=tracker_id,
                                   status_id=pending_backport,
                                   subject='issue11')
        #
        # Pending backport but the teuthology project
        # does not have a Backport tracker
        #
        issue12 = i.r.issue.create(project_id='teuthology',
                                   tracker_id=tracker_id,
                                   status_id=pending_backport,
                                   custom_fields=[{
                                       'id': i.backport_id,
                                       'value': 'firefly',
                                   }],
                                   subject='issue12')
        i.args.dry_run = True
        i.create_issue()
        l = caplog.text()
        assert i.url(issue1) + ' add backport to hammer' in l
        assert i.url(issue1) + ' add backport to firefly' in l

        i.args.dry_run = False
        i.create_issue()
        relations = i.r.issue_relation.filter(issue_id=issue1['id'])
        assert len(relations) == 2
        for relation in relations:
            other = i.r.issue.get(relation['issue_to_id'])
            issue1_releases.remove(i.get_release(other))
        assert len(issue1_releases) == 0

        i.args.dry_run = True
        i.create_issue()
        l = caplog.text()
        assert i.url(issue2) + ' requires backport to unknown release' in l
        assert i.url(issue3) + ' ignore relation to ' + i.url(issue4) in l
        assert (i.url(issue5) + " unexpected relation 'relates' to " +
                i.url(issue6)) in l
        assert (i.url(issue7) + ' has more backport issues (firefly,hammer) ' +
                'than expected (firefly)') in l
        assert i.url(issue10) + ' the backport field is empty' in l
        assert i.url(issue11) + ' no backport field' in l
        assert (i.url(issue12) + ' skipped because the project Teuthology ' +
                'does not have a Backport tracker') in l

# Local Variables:
# compile-command: "cd .. ; tox -e py27 tests/test_wbsetbackportcreateissue.py" # noqa
# End:
