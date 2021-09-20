#!/usr/bin/env python

import argparse
import os
import sys

import backport_create_issue
from ceph_workbench import wbredmine


class Helper(object):
    def __init__(self):
        self.tmp = os.environ.get("TMP", "/tmp")
        redmine = {
            "url": f"http://{os.environ.get('MY_IP', '0.0.0.0')}:8081",
            "username": "admin",
            "password": "admin123",
        }
        self.argv = [
            "--redmine-url",
            redmine["url"],
            "--redmine-user",
            redmine["username"],
            "--redmine-password",
            redmine["password"],
        ]
        self.wbredmine = wbredmine.WBRedmine.factory(self.argv).open()
        self.redmine = self.wbredmine.r

    def setup(self):
        # highjack the Feedback status prentending it is
        # the Pending Backport status because it is complicated
        # to create a new status and the associated workflows.
        wbredmine.WBRedmine.pending_backport = "Feedback"

        self.project = self.redmine.project.create(
            name="Ceph",
            identifier="ceph",
            issue_custom_field_ids=[self.wbredmine.backport_id, self.wbredmine.pr_id],
            tracker_ids=[1, 2, 3, 4],  # 4 is Backport
        )

        for version in (14, 15, 16, 17):
            self.redmine.version.create(
                project_id=self.project["id"], name=f"v{version}.0.0", sharing="descendants"
            )

        self.teuthology = self.redmine.project.create(
            name="Teuthology",
            identifier="teuthology",
            issue_custom_field_ids=[self.wbredmine.backport_id],
            parent_id=self.project["id"],
            tracker_ids=[1, 2, 3],  # not 4 which is Backport
        )

        self.wbredmine.index()

        issue = self.issue_create(
            "ISSUE A", "DESCRIPTION A", self.args.backports, "NOTE A1", self.args.merged_pr
        )
        open(f"{self.tmp}/original-issue", "w").write(str(issue["id"]))
        self.backport_create(self.wbredmine.load_issue(issue["id"]))
        open(f"{self.tmp}/redmine_pull_request_id_custom_field", "w").write(str(self.wbredmine.pr_id))
        return self

    def backport_create(self, issue):
        backport_create_issue.release_id = self.wbredmine.release_id
        backport_create_issue.populate_status_dict(self.wbredmine.r)
        backport_create_issue.populate_tracker_dict(self.wbredmine.r)

        backport_create_issue.update_relations(self.redmine, issue, False)

        relations = self.redmine.issue_relation.filter(issue_id=issue["id"])
        assert len(relations) >= 1
        for relation in relations:
            other = self.redmine.issue.get(relation["issue_to_id"])
            assert str(other["priority"]) == str(
                issue["priority"]["name"]
            ), f'{other["priority"]} == {issue["priority"]["name"]}'
            release = backport_create_issue.get_release(other)
            open(f"{self.tmp}/backport-{release}", "w").write(str(other["id"]))

        return self

    def teardown(self):
        for project in self.redmine.project.all():
            if project["name"] == "Ceph":
                self.redmine.project.delete(project["id"])

    def issue_create(self, subject, description, backport, notes, merged_pr):
        priority_id = self.wbredmine.priority2priority_id["High"]
        issue = self.redmine.issue.create(
            project_id=self.project["id"],
            priority_id=priority_id,
            subject=subject,
            description=description,
            custom_fields=[
                {
                    "id": self.wbredmine.backport_id,
                    "value": backport,
                },
                {
                    "id": self.wbredmine.pr_id,
                    "value": merged_pr,
                },
            ],
        )
        assert issue["priority"]["name"] == "High", f'{issue["priority"]["name"]} == High'
        self.redmine.issue.update(issue.id, notes=notes)
        return issue

    def main(self, argv):
        parser = argparse.ArgumentParser()
        parser.add_argument("--merged-pr")
        parser.add_argument("--backports")
        parser.add_argument("fun")
        self.args = parser.parse_args(argv)
        getattr(self, self.args.fun)()


if __name__ == "__main__":
    Helper().main(sys.argv[1:])
