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
import logging
import re
import subprocess

from ceph_workbench import util, wbgit, wbgithub, wbredmine

log = logging.getLogger(__name__)


class WBBackportSetRelease(object):
    def __init__(self, args):
        self.args = args
        self.git = wbgit.WBRepo(args)
        self.github = wbgithub.WBGitHub(args)
        self.redmine = wbredmine.WBRedmine(args)

    def open(self):
        self.git.open().index()
        self.github.open().index()
        self.redmine.open().index()
        return self

    @staticmethod
    def get_parser():
        parser = argparse.ArgumentParser(
            parents=[
                wbgit.WBRepo.get_parser(),
                wbgithub.WBGitHub.get_parser(),
                wbredmine.WBRedmine.get_parser(),
            ],
            conflict_handler="resolve",
        )

        parser.add_argument(
            "--releases",
            action=util.CommaSplit,
            default=[
                "hammer",
                "infernalis",
                "jewel",
                "kraken",
                "luminous",
            ],
            help=("Comma separated list of releases"),
        )
        parser.add_argument(
            "--git-remote",
            default="origin",
            help=(
                "if set fetch the remote at initialization "
                "otherwise do nothing and assume "
                "--git-directory is up to date. The URL of "
                "--git-remote is verified to match "
                "the URL provided with --git-clone "
            ),
        )
        parser.add_argument(
            "--dry-run", action="store_const", const=True, help="display actions but do not carry them out"
        )
        return parser

    def set_defaults(self):
        if not self.git.args.git_clone:
            self.git.args.git_clone = self.github.get_clone_url()

    @staticmethod
    def factory(argv):
        return WBBackportSetRelease(WBBackportSetRelease.get_parser().parse_args(argv))

    def run(self):
        self.set_defaults()
        self.open()
        self.set_release()

    def set_release(self):
        fixes_re = re.compile(r"Fixes\:? #(\d+)")
        tracker_re = re.compile(self.redmine.args.redmine_url + "/issues/(\d+)")
        pr_re = re.compile("https?://github.com/" + self.github.args.github_repo + "/pull/(\d+)")

        def url_pr(pr):
            return self.github.get_clone_url() + "/pull/" + str(pr["number"])

        def url_r(issue):
            return self.redmine.url(issue)

        kwargs = {
            "limit": 500,
            "tracker_id": self.redmine.tracker2tracker_id["Backport"],
            "status_id": self.redmine.status2status_id["In Progress"],
        }
        r = self.redmine.r

        for issue in r.issue.filter(**kwargs):
            log.debug("checking http://tracker.ceph.com/issues/" + str(issue["id"]) + " " + issue["subject"])
            prs = pr_re.findall(issue["description"])
            if len(prs) != 1:
                log.error(url_r(issue) + " expected exactly one PR got " + str(prs))
                continue
            pr = self.github.repos().pulls(prs[0]).get()
            issues = fixes_re.findall(pr["body"])
            issues += tracker_re.findall(pr["body"])
            if not pr["merged"]:
                log.debug(url_r(issue) + " skipped because " + url_pr(pr) + " is not merged")
                continue
            if str(issue["id"]) not in issues:
                log.error(url_r(issue) + " expected the PR to reference the issue")
                continue
            if pr["base"]["ref"] not in util.releases():
                log.error(url_r(issue) + " " + pr["base"]["ref"] + " is not a known stable branch")
                continue
            merge_sha = self.git.head2merge[pr["head"]["sha"]]
            describe = subprocess.check_output(
                "cd " + self.git.args.git_directory + " ;" " git describe " + merge_sha, shell=True
            )
            version = re.findall("^(v\d+\.\d+\.\d+)", describe)
            if not version:
                log.error(url_r(issue) + " git describe is not parseable: " + describe.strip())
                continue
            ((major, minor),) = re.findall("(.*)\.(.*)", version[0])
            minor = "%d" % (int(minor) + 1)
            version = major + "." + minor
            if version not in self.redmine.version2version_id:
                log.error(
                    url_r(issue)
                    + " "
                    + str(version)
                    + " obtained from "
                    + describe.strip()
                    + " as returned by git describe "
                    + merge_sha
                    + " is not a known version"
                )
                continue
            log.info(
                url_r(issue)
                + " is set with version "
                + version
                + " "
                + str(issue["id"])
                + " because "
                + url_pr(pr)
                + " is merged"
            )
            if not self.args.dry_run:
                version = self.redmine.version2version_id[version]
                status_id = self.redmine.status2status_id["Resolved"]
                r.issue.update(issue["id"], version_id=version, status_id=status_id)
