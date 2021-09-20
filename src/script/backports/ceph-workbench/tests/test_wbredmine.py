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
import fixture_redmine
import logging
import pytest

from ceph_workbench import wbredmine

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestWBRedmine(object):

    @classmethod
    def setup_class(cls):
        cls.f = fixture_redmine.FixtureRedmine()
        cls.f.setUp()
        cls.argv = cls.f.argv

    @classmethod
    def teardown_class(cls):
        cls.f.tearDown()

    def test_load_open_issues(self):
        argv = self.argv + ['--releases=pacific']
        r = wbredmine.WBRedmine.factory(argv).open()
        issue = r.r.issue.create(project_id=self.f.project['id'],
                                 subject='SUBJECT',
                                 description='DESCRIPTION',
                                 custom_fields=[{
                                     "id": r.backport_id,
                                     "value": 'pacific'
                                 }])
        r.r.issue.update(issue.id, notes='NOTE')
        r.load_open_issues()
        assert issue['id'] in r.issues
        issue = r.issues[issue['id']]
        assert set(['pacific']) == issue['backports']
        notes = list(map(lambda j: j['notes'], issue['journals']))
        assert ['NOTE'] == notes

    def test_set_custom_fields(self):
        r = wbredmine.WBRedmine.factory(self.argv).open()
        assert r.backport_id is not None
        assert r.release_id is not None
        r.args.redmine_url = 'http://tracker.ceph.com'
        assert r.set_custom_fields() == 'hack'

    def test_index(self):
        r = wbredmine.WBRedmine.factory(self.argv).open()
        r.index()
        assert 'New' in r.status2status_id
        assert 'Backport' in r.tracker2tracker_id
        assert len(r.version2version_id) > 0

    def test_key(self):
        r1 = wbredmine.WBRedmine.factory(self.argv).open()
        assert 'admin' == r1.r.user.get('current').login
        r2 = wbredmine.WBRedmine.factory([
            '--redmine-url', fixture_redmine.REDMINE['url'],
            '--redmine-key', r1.key,
        ]).open()
        assert 'admin' == r2.r.user.get('current').login

    def test_factory(self, capsys):
        with pytest.raises(wbredmine.ExceptionUserOrKey):
            wbredmine.WBRedmine.factory([]).args_sanity_check()
        with pytest.raises(SystemExit):
            wbredmine.WBRedmine.factory([
                '--redmine-user', 'user',
                '--redmine-key', 'key',
            ])
        out, err = capsys.readouterr()
        assert ('error: argument --redmine-key: '
                'not allowed with argument --redmine-user') in err
        r = wbredmine.WBRedmine.factory(self.argv).open()
        assert 'admin' == r.r.user.get('current').login
