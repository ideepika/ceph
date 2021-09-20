#!/usr/bin/env python

import argparse
import os
import sys

from fedeproxy.architecture.forge import gitea


class Helper(object):
    releases = ("nautilus", "octopus", "pacific")

    def __init__(self):
        self.tmp = os.environ.get("TMP", "/tmp")

    def setup_repository(self, p):
        d = p.dvcs(f"{self.tmp}/repository")
        d.clone("master")
        for n in (1, 2):
            open(f"{d.directory}/file{n}", "w").write(str(n))
            d.commit(f"initial {n}", f"file{n}")
        d.push("master")
        for b in Helper.releases:
            d.g.checkout("-b", b)
            d.push(b)

    def setup(self):
        forge = gitea.Gitea(f"http://{os.environ.get('MY_IP', '0.0.0.0')}:8781")
        forge.authenticate(username="root", password="Wrobyak4")
        open(f"{self.tmp}/gitea-root-token", "w").write(forge.get_token())

        username_ceph = "ceph"
        email_ceph = "ceph@example.com"
        password = "Wrobyak4"
        forge.users.create(username_ceph, password, email_ceph)
        forge.authenticate(username=username_ceph, password=password)
        open(f"{self.tmp}/gitea-ceph-token", "w").write(forge.get_token())

        p_ceph = forge.projects.create(username_ceph, "ceph")
        assert p_ceph.project == "ceph"
        self.setup_repository(p_ceph)
        for release in Helper.releases:
            m = p_ceph.milestones.create(release)
            open(f"{self.tmp}/milestone-{release}", "w").write(str(m.id))

        forge.authenticate(username="root", password="Wrobyak4")
        username_contributor = "contributor"
        email_contributor = "contributor@examxple.com"
        forge.users.create(username_contributor, password, email_contributor)
        forge.authenticate(username=username_contributor, password=password)
        open(f"{self.tmp}/gitea-contributor-token", "w").write(forge.get_token())

        p = forge.project_fork(username_ceph, "ceph")
        assert p.project == "ceph"
        d = p.dvcs(f"{self.tmp}/fork")
        d.clone("master")
        open(f"{d.directory}/file1", "w").write("CHANGED")
        d.commit(f"change", "file1")
        d.push("master")
        r = forge.s.post(
            f"{forge.s.api}/repos/{p_ceph.namespace}/{p_ceph.project}/pulls",
            data={
                "title": "file1",
                "head": f"{username_contributor}:master",
                "base": f"master",
            },
        )
        print(r.text)
        r.raise_for_status()
        pr = r.json()

        forge.authenticate(username=username_ceph, password=password)
        r = forge.s.post(
            f"{forge.s.api}/repos/{p_ceph.namespace}/{p_ceph.project}/pulls/{pr['number']}/merge",
            data={
                "do": "merge",
                "commit_title": "merge",
            },
        )
        print(r.text)
        r.raise_for_status()
        open(f"{self.tmp}/merged-pr", "w").write(str(pr["number"]))

    def teardown(self):
        forge = gitea.Gitea(f"http://{os.environ.get('MY_IP', '0.0.0.0')}:8781")

        username_contributor = "contributor"
        forge.authenticate(username=username_contributor, password="Wrobyak4")
        forge.projects.delete(username_contributor, "ceph")

        username = "ceph"
        forge.authenticate(username=username, password="Wrobyak4")
        forge.projects.delete(username, "ceph")

        forge.authenticate(username="root", password="Wrobyak4")
        forge.users.delete(username)

    def main(self, argv):
        parser = argparse.ArgumentParser()
        parser.add_argument("fun")
        args = parser.parse_args(argv)
        getattr(self, args.fun)()


if __name__ == "__main__":
    Helper().main(sys.argv[1:])
