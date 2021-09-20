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

import requests
from six.moves.urllib import parse

log = logging.getLogger(__name__)


class WBGitLab(object):
    def __init__(self, args):
        self.args = args

        if not hasattr(self.args, "gitlab_repo"):
            log.error("WBGitLab without --gitlab-repo")
            return

        (self.args.gitlab_namespace, self.args.gitlab_name) = self.args.gitlab_repo.split("/")
        self.args.gitlab_repo = parse.quote_plus(self.args.gitlab_repo)
        if not self.args.gitlab_git:
            self.args.gitlab_git = self.args.gitlab_url.replace("http://", "ssh://git@")

        self.gitlab = {
            "git": self.args.gitlab_git,
            "host": self.args.gitlab_url,
            "name": self.args.gitlab_name,
            "namespace": self.args.gitlab_namespace,
            "url": self.args.gitlab_url + "/api/v3",
            "repo": self.args.gitlab_repo,
            "token": self.args.gitlab_token,
        }

    @staticmethod
    def get_parser():
        parser = argparse.ArgumentParser()
        parser.add_argument("--gitlab-url", help="Gitlab url", required=True)
        parser.add_argument("--gitlab-git", help="Gitlab git access URL")
        parser.add_argument("--gitlab-token", help="Gitlab authentication token", required=True)
        parser.add_argument("--gitlab-repo", help="Gitlab repo (for instance ceph/ceph)")
        return parser

    @staticmethod
    def factory(argv):
        return WBGitLab(WBGitLab.get_parser().parse_args(argv))

    def get(self):
        url = self.gitlab["url"] + "/projects/" + self.gitlab["repo"] + "/merge_requests"
        query = {
            "private_token": self.gitlab["token"],
            "state": "all",
        }
        payloads = []
        next_query = query
        while next_query:
            log.debug(str(next_query))
            result = requests.get(url, params=next_query)
            payloads += result.json()
            next_query = None
            for link in result.headers.get("Link", "").split(","):
                if 'rel="next"' in link:
                    m = re.search("<(.*)>", link)
                    if m:
                        parsed_url = parse(m.group(1))
                        # append query in case it was not preserved
                        # (gitlab has that problem)
                        next_query = query
                        next_query.update(dict(parse.parse_qsl(parsed_url.query)))
        return payloads

    def commit_url(self, sha):
        return (
            self.gitlab["url"]
            + "/projects/"
            + self.gitlab["namespace"]
            + "/"
            + self.gitlab["name"]
            + "/commit/"
            + sha
        )

    def index(self):
        self.pull2merge = {}
        self.merge2pull = {}
        self.release2pulls = {}
        self.pull2release = {}
        self.merge_id2merge_request = {}
        self.merge_requests = self.get()
        for merge_request in self.merge_requests:
            # ignore merge_requests that are closed but not merged
            if merge_request["state"] == "closed":
                if not merge_request["description"] or ":MERGED:" not in merge_request["description"]:
                    continue
                else:
                    merge_request["state"] = "merged"
            self.merge_id2merge_request[merge_request["id"]] = merge_request
            source = merge_request["source_branch"]
            (a, pull_request, b) = source.split("/")
            self.pull2merge[pull_request] = merge_request["id"]
            self.merge2pull[merge_request["id"]] = pull_request
            target = merge_request["target_branch"]
            self.release2pulls.setdefault(target, []).append(pull_request)
            self.pull2release[pull_request] = target
            urls = (
                self.gitlab["host"]
                + "/"
                + self.gitlab["namespace"]
                + "/"
                + self.gitlab["name"]
                + "/merge_requests/"
                + str(merge_request["iid"])
                + " "
                "https://github.com/ceph/ceph/pull/" + pull_request
            )
            merge_request["urls"] = urls
