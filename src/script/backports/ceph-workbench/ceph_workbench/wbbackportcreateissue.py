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

from ceph_workbench import util, wbredmine

log = logging.getLogger(__name__)


class WBBackportCreateIssue(wbredmine.WBRedmine):
    @staticmethod
    def get_parser():
        parser = argparse.ArgumentParser(
            parents=[
                wbredmine.WBRedmine.get_parser(),
            ],
            conflict_handler="resolve",
        )

        parser.add_argument(
            "--dry-run", action="store_const", const=True, help="display actions but do not carry them out"
        )
        return parser

    @staticmethod
    def factory(argv):
        return WBBackportCreateIssue(WBBackportCreateIssue.get_parser().parse_args(argv))

    def run(self):
        self.open().index()
        self.create_issue()

    def update_relations(self, issue):
        success = True
        relations = self.r.issue_relation.filter(issue_id=issue["id"])
        existing_backports = set()
        for relation in relations:
            other = self.r.issue.get(relation["issue_to_id"])
            if other["tracker"]["name"] != "Backport":
                log.debug(
                    self.url(issue)
                    + " ignore relation to "
                    + self.url(other)
                    + " because it is not in the Backport "
                    + "tracker"
                )
                continue
            if relation["relation_type"] != "copied_to":
                log.error(
                    self.url(issue)
                    + " unexpected relation '"
                    + relation["relation_type"]
                    + "' to "
                    + self.url(other)
                )
                success = False
                continue
            release = self.get_release(other)
            existing_backports.add(release)
            log.debug(
                self.url(issue)
                + " backport to "
                + release
                + " is "
                + self.args.redmine_url
                + "/issues/"
                + str(relation["issue_to_id"])
            )
        if existing_backports == issue["backports"]:
            log.debug(self.url(issue) + " has all the required backport issues")
            return success
        if existing_backports.issuperset(issue["backports"]):
            log.error(
                self.url(issue)
                + " has more backport"
                + " issues ("
                + ",".join(sorted(existing_backports))
                + ")"
                " than expected (" + ",".join(sorted(issue["backports"])) + ")"
            )
            return False
        release_id = self.release_id
        backport_tracker = self.tracker2tracker_id["Backport"]
        for release in issue["backports"] - existing_backports:
            if release not in util.releases():
                log.error(self.url(issue) + " requires backport to " + "unknown release " + release)
                success = False
                break
            subject = release + ": " + issue["subject"]
            if self.args.dry_run:
                log.info(self.url(issue) + " add backport to " + release)
                continue
            other = self.r.issue.create(
                project_id=issue["project"]["id"],
                tracker_id=backport_tracker,
                subject=subject,
                priority="Normal",
                custom_fields=[
                    {
                        "id": release_id,
                        "value": release,
                    }
                ],
            )
            self.r.issue_relation.create(
                issue_id=issue["id"], issue_to_id=other["id"], relation_type="copied_to"
            )
            log.info(self.url(issue) + " added backport to " + release + " " + self.url(other))
        return success

    def create_issue(self):
        kwargs = {
            "limit": 500,
            "project_id": "ceph",
            "status_id": self.status2status_id[wbredmine.WBRedmine.pending_backport],
        }

        for issue in self.r.issue.filter(**kwargs):
            if not self.has_tracker(issue["project"]["id"], "Backport"):
                log.debug(
                    self.url(issue)
                    + " skipped because the project "
                    + issue["project"]["name"]
                    + " does not have a "
                    + "Backport tracker"
                )
                continue
            if not self.set_backport(issue):
                log.error(self.url(issue) + " no backport field")
                continue
            if len(issue["backports"]) == 0:
                log.error(self.url(issue) + " the backport field is empty")
            self.update_relations(issue)
