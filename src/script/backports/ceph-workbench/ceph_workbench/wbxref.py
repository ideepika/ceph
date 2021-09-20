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

import six
from ceph_workbench import wbgit, wbgithub, wbgitlab, wbredmine

log = logging.getLogger(__name__)


class WBXref(object):
    def __init__(self, args):
        self.args = args
        self.git = wbgit.WBRepo(args)
        self.github = wbgithub.WBGitHub(args)
        self.redmine = wbredmine.WBRedmine(args)
        self.gitlab = wbgitlab.WBGitLab(args)
        self.issue_id2merge_requests = {}
        self.commit2issues = {}

    def open(self):
        self.git.open()
        self.github.open()
        self.redmine.open()
        return self

    def index(self):
        self.git.index()
        self.redmine.index()
        self.github.index()
        self.gitlab.index()
        self.update_commit2issues()
        self.update_issues_with_merge()
        self.update_issues_with_comments()
        self.update_cherry_pick_or_link()
        return self

    @staticmethod
    def get_parser():
        return argparse.ArgumentParser(
            description="git, redmine and GitLab cross references",
            parents=[
                wbgit.WBRepo.get_parser(),
                wbgithub.WBGitHub.get_parser(),
                wbredmine.WBRedmine.get_parser(),
                wbgitlab.WBGitLab.get_parser(),
            ],
            add_help=False,
            conflict_handler="resolve",
        )

    @staticmethod
    def factory(argv):
        return WBXref(WBXref.get_parser().parse_args(argv))

    @staticmethod
    def merge_message2issue(message):
        return re.findall(r"Merge pull request.*from.*wip-(\d+)", message, re.IGNORECASE) or re.findall(
            r"remote-tracking branch '\w+/wip-(\d+)", message, re.IGNORECASE
        )

    @staticmethod
    def message2issue(message):
        return re.findall(r"(?:fix|ref)\w*[\s#:]*(\d+)", message, re.IGNORECASE) + re.findall(
            r"http://tracker.ceph.com/issues/(\d+)", message, re.IGNORECASE
        )

    def update_commit2issues(self):
        r = self.git.r
        issues = self.redmine.issues

        def fun(merge):
            log.debug("update_commit2issues " + merge.message)
            issue_ids = self.merge_message2issue(merge.message)
            if issue_ids:
                hexshas = [ca.hexsha for ca in r.iter_commits(merge.hexsha + "^1.." + merge.hexsha + "^2")]
                for issue in [int(i) for i in issue_ids]:
                    issues.setdefault(issue, {})["commits"] = hexshas
                    for hexsha in hexshas:
                        self.commit2issues.setdefault(hexsha, set()).update(issue_ids)

        self.git.for_each_merge(fun)

    def update_issue(self, merge_request, issues):
        for issue in issues:
            merge_request.setdefault("issues", set()).add(int(issue))
            s = self.issue_id2merge_requests.setdefault(int(issue), set())
            s.add(merge_request["id"])

    def update_issues_with_merge(self):
        self.release2pending = {}
        self.release2integrated = {}
        self.commit2merge_requests = {}
        repo = self.git.r
        for merge_request in self.gitlab.merge_requests:
            head = repo.rev_parse(merge_request["source_branch"])
            if head.hexsha in self.git.head2merge:
                log.debug(
                    "update_issues_with_merge: "
                    + str(merge_request["id"])
                    + " "
                    + merge_request["source_branch"]
                    + " is merged into "
                    + merge_request["target_branch"]
                )
                merge = self.git.head2merge[head.hexsha]
                self.update_issue(merge_request, self.merge_message2issue(head.message))
                commits = list(repo.iter_commits(merge + "^1.." + merge + "^2"))
            else:
                target = merge_request["target_branch"]
                commits = list(repo.iter_commits(target + ".." + head.hexsha))
                if commits and target in self.git.release2backports:
                    backports = self.git.release2backports[target]
                    if list(repo.iter_commits(backports + ".." + head.hexsha)):
                        self.release2pending.setdefault(target, set()).add(merge_request["id"])
                    else:
                        self.release2integrated.setdefault(target, set()).add(merge_request["id"])
            for commit in commits:
                self.update_issue(merge_request, self.message2issue(commit.message))
                self.commit2merge_requests.setdefault(commit.hexsha, set()).add(merge_request["id"])
            if len(commits):
                merge_request["commits"] = commits
                log.debug(
                    merge_request["urls"]
                    + " has "
                    + str(len(commits))
                    + " commits (first is "
                    + commits[0].hexsha
                    + ")"
                )
            else:
                merge_request["commits"] = []
                log.debug(merge_request["urls"] + " has no commit")

    def update_issues_with_comments(self):
        """For issues that have no merge requests associated with them already
        (i.e. the commits do not reference any issue with something
        like Fixes: #2049) search the issue description and notes
        for references to a merge request.
        """
        pull_re = r"https://github.com/ceph/ceph/pull/(\d+)"
        redmine = self.args.redmine_url
        merges = self.gitlab.merge_id2merge_request
        for (issue_id, issue) in six.iteritems(self.redmine.issues):
            if "id" in issue and issue["id"] not in self.issue_id2merge_requests:
                matches = re.findall(pull_re, issue["description"])
                if matches:
                    found = False
                    for pull in matches:
                        if pull in self.gitlab.pull2merge:
                            merge_request = merges[self.gitlab.pull2merge[pull]]
                            merge_request.setdefault("issues", set()).add(issue["id"])
                            found = True
                            log.info(
                                redmine + "/issues/" + str(issue["id"]) + " description links to "
                                "https://github.com/ceph/ceph/pull/" + pull
                            )
                    if found:
                        continue
                match = re.search(r"commit:(\w+)", issue["description"])
                if match:
                    commit = self.git.r.rev_parse(match.group(1))
                    merge_ids = self.commit2merge_requests.get(commit.hexsha, [])
                    if merge_ids:
                        for id in merge_ids:
                            merge_request = self.gitlab.merge_id2merge_request[id]
                            target = merge_request["target_branch"]
                            if target in issue.get("backports", set()):
                                (merge_request.setdefault("issues", set()).add(issue["id"]))
                                log.debug(
                                    redmine
                                    + "/issues/"
                                    + str(issue["id"])
                                    + " description has "
                                    + (self.gitlab.commit_url(commit.hexsha))
                                    + " which belongs to "
                                    + "merge request "
                                    + merge_request["urls"]
                                )
                        continue
                    else:
                        self.commit2issues.setdefault(commit.hexsha, set()).add(issue["id"])
                for journal in issue["journals"]:
                    #                    if 'notes' not in journal:
                    #                        continue
                    match = re.search(r"https://github.com/ceph/ceph/pull/(\d+)", journal["notes"])
                    if match:
                        pull = match.group(1)
                        if pull in self.gitlab.pull2merge:
                            merge_request = merges[self.gitlab.pull2merge[pull]]
                            merge_request.setdefault("related_issues", set()).add(issue["id"])
                            log.info(
                                redmine
                                + "/issues/"
                                + str(issue["id"])
                                + " comments link to merge request "
                                + merge_request["urls"]
                            )

    def update_cherry_pick_or_link(self):
        """If a commit is not associated with a merge request, maybe it was
        * cherry picked from a commit that is associated with a merge request
        * linked to an issue but never to a merge request
        """
        try_again = True
        while try_again:
            try_again = False
            for release in self.args.releases:
                tag = self.git.release2last_tag.get(release)
                already = "tags/" + tag + "..heads/" + release
                log.debug("cherry_pick_or_link: listing " + already)
                for c in self.git.r.iter_commits(already, no_merges=True):
                    log.debug("cherry_pick_or_link: commit " + c.hexsha)
                    if c.hexsha not in self.commit2merge_requests:
                        found = False
                        match = re.search(r"cherry picked from commit (\w+)", c.message, re.IGNORECASE)
                        if match:
                            picked = match.group(1)
                            if picked in self.commit2merge_requests:
                                log.debug("commit " + c.hexsha + " cherry picked ")
                                v = self.commit2merge_requests[picked]
                                self.commit2merge_requests[c.hexsha] = v
                                try_again = True
                                found = True
                        if not found:
                            issue_ids = self.message2issue(c.message)
                            for issue_id in issue_ids:
                                log.debug(
                                    "commit "
                                    + c.hexsha
                                    + " links to "
                                    + self.args.redmine_url
                                    + "/issues/"
                                    + str(issue_id)
                                )
                                self.commit2issues.setdefault(c.hexsha, set()).add(issue_id)
                                found = True

    def sanity_checks(self):
        result = True
        for (issue_id, issue) in six.iteritems(self.redmine.issues):
            if "id" not in issue:
                continue
            if issue["status"]["name"] != "Pending Backport":
                continue
            if issue_id not in self.issue_id2merge_requests:
                continue
            branches = [
                self.merge_id2merge_request[m]["target_branch"]
                for m in self.issue_id2merge_requests[issue_id]
            ]
            pending_backports = set(branches).intersection(set(self.args.releases))
            desired_backports = issue.get("backports", set())
            missing = pending_backports.difference(desired_backports)
            if missing:
                result = False
                is_missing = ",".join(missing)
                should_be = ",".join(desired_backports.union(pending_backports))
                log.error(
                    self.args.redmine_url
                    + "/issues/"
                    + str(issue_id)
                    + " backport is "
                    + str(desired_backports)
                    + " but should be "
                    + should_be
                    + " ("
                    + is_missing
                    + " is missing)"
                )
                for merge in self.issue_id2merge_requests[issue_id]:
                    log.error("\t" + self.merge_id2merge_request[merge]["urls"])
        return result
