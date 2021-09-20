# -*- mode: python; coding: utf-8 -*-
#
# Copyright (C) 2015 <contact@redhat.com>
#
# Author: Loic Dachary <loic@dachary.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see `<http://www.gnu.org/licenses/>`.
#
import argparse

import github
from ceph_workbench import util


class WBGitHub(object):
    def __init__(self, args):
        self.args = args

    def open(self):
        if self.args.github_token:
            self.g = github.GitHub(access_token=self.args.github_token)
        return self

    def index(self):
        return self

    @staticmethod
    def get_parser():
        parser = argparse.ArgumentParser(
            parents=[util.get_parser()],
            add_help=False,
        )
        parser.add_argument(
            "--github-token", help=("GitHub API token obtained from " "https://github.com/settings/tokens")
        )
        parser.add_argument("--github-url", default="http://github.com", help="GitHub base URL")
        parser.add_argument(
            "--github-repo", default="ceph/ceph", help=("GitHub repository, relative to " "--github-url")
        )
        return parser

    @staticmethod
    def factory(argv):
        return WBGitHub(WBGitHub.get_parser().parse_args(argv))

    def get_clone_url(self):
        return self.args.github_url + "/" + self.args.github_repo

    def repos(self):
        (owner, repo) = self.args.github_repo.split("/")
        return self.g.repos(owner)(repo)
