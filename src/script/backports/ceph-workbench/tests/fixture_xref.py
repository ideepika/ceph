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
from fixture_git import FixtureGit
from fixture_github import FixtureGitHub
from fixture_gitlab import FixtureGitLab
from fixture_redmine import FixtureRedmine


class FixtureXref(object):

    def __init__(self):
        self.redmine = FixtureRedmine()
        self.github = FixtureGitHub()
        self.gitlab = FixtureGitLab()
        self.git = FixtureGit()

    def setUp(self):
        self.argv = []
        self.redmine.setUp()
        self.argv += self.redmine.argv
        self.github.setUp()
        self.argv += self.github.argv
        self.gitlab.setUp()
        self.argv += self.gitlab.argv
        self.git.setUp()
        self.argv += self.git.argv
        return self

    def tearDown(self):
        self.redmine.tearDown()
        self.github.tearDown()
        self.gitlab.tearDown()
        self.git.tearDown()
