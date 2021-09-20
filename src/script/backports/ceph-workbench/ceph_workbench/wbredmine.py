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

# https://python-redmine.com/
import redminelib
from ceph_workbench import util

log = logging.getLogger(__name__)


class ExceptionUserOrKey(Exception):
    pass


class WBRedmine(object):

    pending_backport = "Pending Backport"

    def __init__(self, args):
        self.args = args
        self.issues = {}
        self.status2status_id = {}
        self.version2version_id = {}
        self.priority2priority_id = {}
        self.tracker2tracker_id = {}
        self.project_id2project = {}

    def open(self):
        self.args_sanity_check()
        self.r = redminelib.Redmine(self.args.redmine_url, key=self.get_key())
        self.set_custom_fields()
        return self

    def index(self):
        self.map_names_to_ids()
        self.load_open_issues()
        return self

    def get_key(self):
        if self.args.redmine_user:
            r = redminelib.Redmine(
                self.args.redmine_url, username=self.args.redmine_user, password=self.args.redmine_password
            )
            self.key = r.user.get("current").api_key
        else:
            self.key = self.args.redmine_key
        return self.key

    def map_names_to_ids(self):
        self.status2status_id = {}
        for status in self.r.issue_status.all():
            self.status2status_id[status.name] = status.id

        versions = self.r.version.filter(project_id="ceph")
        self.version2version_id = {}
        for version in versions:
            self.version2version_id[version.name] = version.id

        priorities = self.r.enumeration.filter(project_id="ceph", resource="issue_priorities")
        self.priority2priority_id = {}
        for priority in priorities:
            self.priority2priority_id[priority.name] = priority.id

        self.tracker2tracker_id = {}
        for tracker in self.r.tracker.all():
            self.tracker2tracker_id[tracker.name] = tracker.id

    def set_custom_fields(self):
        #
        # Hack because
        # http://www.redmine.org/projects/redmine/wiki/Rest_CustomFields
        # requires administrative permissions. It can be removed when
        # https://www.redmine.org/issues/18875
        # is resolved.
        #
        if "tracker.ceph.com" in self.args.redmine_url:
            self.backport_id = 2
            self.release_id = 16
            return "hack"
        else:
            for field in self.r.custom_field.all():
                if field.name == "Backport":
                    self.backport_id = field.id
                elif field.name == "Release":
                    self.release_id = field.id
                elif field.name == "Pull request ID":
                    self.pr_id = field.id

    def has_tracker(self, project_id, tracker_name):
        for tracker in self.get_project(project_id).trackers:
            if tracker["name"] == tracker_name:
                return True
        return False

    def get_project(self, project_id):
        if project_id not in self.project_id2project:
            project = self.r.project.get(project_id, include="trackers")
            self.project_id2project[project_id] = project
        return self.project_id2project[project_id]

    @staticmethod
    def get_parser():
        parser = argparse.ArgumentParser(
            parents=[util.get_parser()],
            add_help=False,
        )
        parser.add_argument("--redmine-url", help="base URL for API calls", default="http://tracker.ceph.com")
        auth = parser.add_mutually_exclusive_group()
        auth.add_argument(
            "--redmine-user",
            help=("if set, use --redmine-password to " "authenticate (cannot be used with " "--redmine-key)"),
        )
        parser.add_argument("--redmine-password", help=("must be set if --redmine-user is set"))
        auth.add_argument(
            "--redmine-key",
            help=(
                "authenticate using the value found at "
                "http://tracker.ceph.com/my/api_key instead "
                "of --redmine-user and --redmine-password"
            ),
        )
        return parser

    def args_sanity_check(self):
        if not self.args.redmine_user and not self.args.redmine_key:
            raise ExceptionUserOrKey("did not find --redmine-user " " or --redmine-key")

    @staticmethod
    def factory(argv):
        return WBRedmine(WBRedmine.get_parser().parse_args(argv))

    def url(self, issue):
        return self.args.redmine_url + "/issues/" + str(issue["id"])

    def load_issue(self, issue_id):
        if issue_id not in self.issues or "id" not in self.issues[issue_id]:
            self.update_issues(issue_id)
        return self.issues[issue_id]

    def set_backport(self, issue):
        for field in issue["custom_fields"]:
            if field["name"] == "Backport" and field["value"] != 0:
                issue["backports"] = set(re.findall("\w+", field["value"]))
                log.debug(
                    "backports for "
                    + str(issue["id"])
                    + " is "
                    + str(field["value"])
                    + " "
                    + str(issue["backports"])
                )
                return True
        return False

    def get_release(self, issue):
        for field in issue.custom_fields:
            if field["name"] == "Release":
                return ",".join(field["value"])

    def update_issues(self, issue_id):
        self.issues.setdefault(issue_id, {}).update(self.r.issue.get(issue_id, include="journals"))
        issue = self.issues[issue_id]
        self.set_backport(issue)

    def load_open_issues(self):
        for release in self.args.releases:
            kwargs = {
                "project_id": "ceph",
                "status_id": "open",
                "limit": 100,
                "cf_" + str(self.backport_id): "" + release,
            }
            log.debug("load_open_issues " + str(kwargs))
            for issue in self.r.issue.filter(**kwargs):
                if issue["project"]["name"] in ("Ceph", "rbd", "rgw", "fs"):
                    self.update_issues(issue["id"])
