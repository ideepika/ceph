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
import os
import tempfile

import six
from ceph_workbench import util, wbxref


class WBBackport(object):
    def __init__(self, args):
        self.args = args
        self.xref = wbxref.WBXref(args)
        if self.args.backport_wiki:
            self.wiki_directory = self.args.backport_wiki
        else:
            self.wiki_directory = self.xref.git.args.git_directory + ".wiki"
        self.wiki_git = (
            self.xref.gitlab.gitlab["git"]
            + "/"
            + self.xref.gitlab.gitlab["namespace"]
            + "/"
            + self.xref.gitlab.gitlab["name"]
            + ".wiki"
        )

    def open(self):
        self.xref.open().index()
        return self

    @staticmethod
    def get_parser():
        parser = argparse.ArgumentParser(
            description="Backports",
            parents=[
                wbxref.WBXref.get_parser(),
            ],
            add_help=False,
            conflict_handler="resolve",
        )
        parser.add_argument("--backport-wiki", help="GitLab wiki git directory")
        parser.add_argument(
            "--backport-range", help=("git range (for instance " "tags/v0.80.8..origin/firefly)")
        )
        return parser

    @staticmethod
    def factory(argv):
        return WBBackport(WBBackport.get_parser().parse_args(argv))

    def display_merge_ids(self, merge_ids, prefix, f):
        for merge_id in merge_ids:
            self.display_merge_request(self.xref.gitlab.merge_id2merge_request[merge_id], prefix, f)

    @staticmethod
    def message2short(message):
        message = message.split("\n")[0]
        if len(message) > 40:
            message = message[:40] + "..."
        return message

    def display_cherry(self, commits, direction, prefix, f):
        for picked in commits:
            if picked in self.xref.git.hexsha2commit:
                p = self.xref.git.hexsha2commit[picked]
                m = self.message2short(p.message)
            else:
                m = ""
            tag = self.xref.git.commit2tag.get(picked, "")
            f.write(
                prefix * 2
                + "* picked "
                + direction
                + " "
                + self.xref.gitlab.commit_url(picked)
                + " "
                + tag
                + " "
                + m
                + "\n"
            )

    def display_commit(self, commit, prefix, f):
        tag = self.xref.git.commit2tag.get(commit.hexsha, "")
        f.write(
            prefix * 2
            + "* "
            + self.xref.gitlab.commit_url(commit.hexsha)
            + " "
            + tag
            + " "
            + self.message2short(commit.message)
            + "\n"
        )
        self.display_cherry(self.xref.git.picked_by2commits.get(commit.hexsha, []), "from", prefix + " ", f)
        self.display_cherry(self.xref.git.commit2picked_by.get(commit.hexsha, []), "by", prefix + " ", f)

    def display_commits(self, commits, prefix, f):
        for commit in commits:
            if commit in self.xref.git.hexsha2commit:
                commit = self.xref.git.hexsha2commit[commit]
            else:
                f.write(prefix * 2 + " " + self.xref.gitlab.commit_url(commit) + " is unknown" + "\n")
                continue
            self.display_commit(commit, prefix, f)

    def display_merge_request(self, merge_request, prefix, f):
        f.write(prefix * 2 + "* " + merge_request["urls"] + " " + merge_request["target_branch"] + "\n")
        for issue_id in merge_request.get("issues", []):
            self.display_issue(issue_id, prefix + " ", f)
        self.display_commits(merge_request["commits"], prefix + " ", f)

    def display_issue(self, issue_id, prefix, f):
        url = "http://tracker.ceph.com/issues/{issue_id}".format(issue_id=issue_id)
        issue = self.xref.redmine.load_issue(issue_id)
        if "assigned_to" in issue:
            contact = "assigned to {assignee}".format(assignee=issue["assigned_to"]["name"])
        else:
            contact = " author {author}".format(author=issue["author"]["name"])
        f.write(
            prefix * 2
            + "* *"
            + issue["project"]["name"]
            + "* "
            + issue["subject"].replace('"', "")
            + " "
            + url
            + " "
            + contact
            + "\n"
        )

    def inventory_release(self, ref, prefix, f):
        merge_ids = set()
        issue2commits = {}
        issue_via_picked2commit = set()
        for c in self.xref.git.r.iter_commits(ref, no_merges=True):
            if c.hexsha in self.xref.commit2merge_requests:
                merge_ids.update(self.xref.commit2merge_requests[c.hexsha])
            elif c.hexsha in self.xref.commit2issues:
                for issue_id in self.xref.commit2issues[c.hexsha]:
                    issue2commits.setdefault(issue_id, []).append(c.hexsha)
            elif c.hexsha in self.xref.git.picked_by2commits:
                picked = self.xref.git.picked_by2commits.get(c.hexsha, [])
                # if picked:
                #     s = " ".join([self.xref.gitlab.commit_url(p)
                #                   for p in picked])
                # else:
                #     s = ''
                # i = ''
                # for picked in self.xref.git.picked_by2commits[c.hexsha]:
                #     for k in self.xref.commit2issues.get(picked, []):
                #         i += "," + str(k)
                # print("* commit:"+ c.hexsha +
                #       " cherry picked from " +
                #       s + " " + i + " " +
                #       c.message.split("\n")[0][:40]
                for picked in self.xref.git.picked_by2commits[c.hexsha]:
                    issue_via_picked2commit.update(self.xref.commit2issues.get(picked, set()))
            else:
                f.write(
                    prefix * 2
                    + "* isolated commit "
                    + self.xref.gitlab.commit_url(c.hexsha)
                    + " "
                    + c.message.split("\n")[0][:40]
                    + "\n"
                )
        f.write(prefix * 2 + "* issues (no merge request)\n")
        for (issue_id, commits) in six.iteritems(issue2commits):
            self.display_issue(issue_id, prefix + " ", f)
            self.display_commits(commits, prefix + "  ", f)
        f.write(prefix * 2 + "* issues (via cherry-picking)\n")
        for issue_id in issue_via_picked2commit:
            self.display_issue(issue_id, prefix + " ", f)
            issue = self.xref.redmine.load_issue(int(issue_id))
            self.display_commits(issue.get("commits", []), prefix + "  ", f)
        f.write(prefix * 2 + "* merge requests\n")
        self.display_merge_ids(merge_ids, prefix + " ", f)

    def wiki_clone(self):
        if not os.path.exists(self.wiki_directory):
            if "GIT_SSH" in os.environ:
                name = None
            else:
                (fd, name) = tempfile.mkstemp()
                script = "#!/bin/bash\n" + 'ssh -o StrictHostKeyChecking=false "$@"\n'
                os.write(fd, script.encode("utf-8"))
                os.close(fd)
                os.chmod(name, 0o755)
                os.environ["GIT_SSH"] = self.ssh
            util.sh(" git clone " + self.wiki_git + " " + self.wiki_directory)
            if name:
                os.unlink(name)
                del os.environ["GIT_SSH"]

    def inventory_write(self, range):
        self.wiki_clone()
        (start, end) = range.split("..")
        with open(self.wiki_directory + "/" + start.replace(".", "-") + ".rdoc", "w") as f:
            self.inventory_release(range, "", f)
