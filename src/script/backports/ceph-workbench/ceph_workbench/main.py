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
import textwrap

from ceph_workbench import ceph_qa_suite, release, wbbackport, wbbackportcreateissue, wbbackportsetrelease
from github2gitlab import main

logging.basicConfig(format="%(asctime)s %(levelname)s %(message)s")

DEFAULT_LIBDIR = "/usr/local/lib/ceph-workbench"
DEFAULT_DATADIR = "/usr/local/share/ceph-workbench"


class CustomFormatter(argparse.ArgumentDefaultsHelpFormatter, argparse.RawDescriptionHelpFormatter):
    pass


class CephWorkbench(object):
    def __init__(self):
        self.parser = argparse.ArgumentParser(
            formatter_class=CustomFormatter,
            description=textwrap.dedent(
                """\
            A toolbox for the Ceph developer.

            The documentation for each subcommand can be displayed with

               ceph-workbench subcommand --help

            For instance:

               ceph-workbench backport-create-issue --help
               usage: ceph-workbench backport-create-issue [-h]
               ...

            For more information, refer to the Ceph developer guide at
            http://docs.ceph.com/docs/master/dev/
            """
            ),
        )

        self.parser.add_argument(
            "-v",
            "--verbose",
            action="store_true",
            default=None,
            help="be more verbose",
        )

        self.parser.add_argument(
            "--libdir",
            help="directory containing helpers programs",
        )
        self.parser.add_argument(
            "--datadir",
            help="directory for persistent data",
        )

        subparsers = self.parser.add_subparsers(
            title="subcommands",
            description="valid subcommands",
            help="sub-command -h",
        )

        subparsers.add_parser(
            "github2gitlab",
            help="Mirror a GitHub project to GitLab",
            parents=[main.GitHub2GitLab.get_parser()],
            add_help=False,
        ).set_defaults(
            func=main.GitHub2GitLab,
        )

        subparsers.add_parser(
            "backport",
            help="Backport reports",
            parents=[wbbackport.WBBackport.get_parser()],
            add_help=False,
        ).set_defaults(
            func=wbbackport.WBBackport,
        )

        subparsers.add_parser(
            "backport-set-release",
            formatter_class=CustomFormatter,
            description=textwrap.dedent(
                """\
            Set the release field of backport issues.

            For each open issue in the Backport tracker, get the
            URL to pull request from the description. If the pull request
            is merged, get the version with git describe. The issue is
            then resolved and the version is set to the next minor version.

            Let say http://tracker.ceph.com/issues/12484 has the URL
            https://github.com/ceph/ceph/pull/5360 in its description
            and it was merged as commit
            4a1e54fc88e43885c57049d1ad4c5641621b6c29. git describe
            returns v0.80.10-165-g4a1e54f and the version field of
            issue 12484 is set to v0.80.11 which is the Ceph version
            in which the commit will be published.
            """
            ),
            epilog=textwrap.dedent(
                """
            Examples:

            ceph-workbench backport-set-release \\
                           --git-directory /tmp/ceph \\
                           --github-token $github_token \\
                           --redmine-key $redmine_key \\
                           --dry-run

            Clone http://github.com/ceph/ceph into /tmp/ceph (assuming
            it does not already exist). All open Backport issues are
            looked up in http://tracker.ceph.com using the REST API
            with the $redmine_key credentials. The status of the
            corresponding GitHub pull request is verified using
            $github_token to increase the API rate limit. The redmine
            issues that should be modified (i.e. their release field
            set and the status changed to Resolved) are displayed but
            nothing is done because of --dry-run.

            ceph-workbench backport-set-release \\
                           --git-remote ceph \\
                           --git-directory $HOME/ceph \\
                           --github-token $github_token \\
                           --redmine-key $redmine_key \\
                           --dry-run

            Instead of cloning the Ceph repository every time, an existing
            one can be used at $HOME/ceph. More often than not, the local
            branches of a local clone of the Ceph repository do not reflect
            the latest state of the main repository. With --git-remote ceph,
            a remote is used instead of the local branches:

            $ git remote -v | grep '^ceph'
            ceph	http://github.com/ceph/ceph (fetch)
            ceph	http://github.com/ceph/ceph (push)

            To ensure the remote is up to date, a git fetch ceph is done
            during initialization. Working with a local repository is
            necessary because GitHub does not provide a reliable way to
            get the SHA of a merged pull request (see
            f2aa9e5cc8138a7607a460dbea271e7d907a228a for more information).

            """
            ),
            help="Set the release field of the backport issues",
            parents=[wbbackportsetrelease.WBBackportSetRelease.get_parser()],
            add_help=False,
        ).set_defaults(
            func=wbbackportsetrelease.WBBackportSetRelease,
        )

        subparsers.add_parser(
            "backport-create-issue",
            formatter_class=CustomFormatter,
            description=textwrap.dedent(
                """\
            Create Backport issues required by Pending Backports.

            For each issue with status Pending Backports, create
            an issue in the Backport tracker of the same project
            for each release listed in the Backport field.

            Let say http://tracker.ceph.com/issues/11786 has the
            string firefly and hammer in the Backport field. The
            Related issues are listed and one Backport issue is found
            with the Release field set to hammer. But no issue is
            found for the firefly release and a new one is created,
            with the same title. A Copied to relation is added to make
            it easier to find the Backport issues from the original
            issue.
            """
            ),
            epilog=textwrap.dedent(
                """
            Examples:

            ceph-workbench backport-create-issue \\
                           --redmine-key $redmine_key \\
                           --dry-run

            """
            ),
            help="Create Backport issues required by Pending Backports",
            parents=[wbbackportcreateissue.WBBackportCreateIssue.get_parser()],
            add_help=False,
        ).set_defaults(
            func=wbbackportcreateissue.WBBackportCreateIssue,
        )

        subparsers.add_parser(
            "ceph-qa-suite",
            formatter_class=CustomFormatter,
            description=textwrap.dedent(
                """\
            Run integration tests from https://github.com/ceph/ceph/tree/master/qa

            A wrapper around teuthology-openstack.

            The main technical difference is that it does not require
            --key-name and --key-filename. Instead, it creates a
            passwordless teuthology-test keypair in OpenStack and
            stores it in ~/.ceph-workbench. If such a key already
            exists, it is re-used.
            """
            ),
            epilog=textwrap.dedent(
                """
            Example:

            ceph-workbench ceph-qa-suite --suite dummy

            """
            ),
            help="ceph-qa-suite",
            parents=[ceph_qa_suite.CephQaSuite.get_parser()],
            add_help=False,
        ).set_defaults(
            func=ceph_qa_suite.CephQaSuite,
        )

        subparsers.add_parser(
            "release",
            formatter_class=CustomFormatter,
            description=textwrap.dedent(
                """\
            Create packages and repositories for a Ceph version

            . Clone the --ceph-repo repository
            . Tag the tip of the --ceph branch with --version if
              the tag does not exist yet
            . Build the packages for the tag with:
                 teuthology-suite --suite buildpackages
            . Sign the packages with the PGP key in ~/.ceph-workbench
            . Create repositories with the signed packages
            . Upload the repositories and the PGP public key to
              the packages-repository instance
            . Verify the packages can be installed

            Before running this command, the ceph branch must be
            manually edited so the version found in debian/changelog
            and configure.ac match the version given with the
            --version option.

            When the command completes, the URL of the web server
            containing the packages is displayed. From the IP address
            in this URL the following can be deduced:

            http://IP/ceph/              Ceph clone with the tag
            http://IP/debian-testing/    All-in-one deb repository
            http://IP/rpm-testing/       Per-OS rpm repositories
            http://IP/release-key.asc    PGP key used to sign the packages

            The {debian,rpm}-* repositories are compatible with
            the Ceph documentation:

               http://docs.ceph.com/docs/master/install/#install-software



            The first time ceph-workbench release is run, it creates a
            GPG keypair in ~/.ceph-workbench/gpg/ and uses the private
            key to sign the packages. The corresponding public key is
            made available as release-key.asc in this directory.

            If a tag vX.Y.Z corresponding to the --version X.Y.Z
            option exists, that tag will be used to build the
            packages. Otherwise a new tag vX.Y.Z will be created,
            pointing to the tip of the branch. The read-only Ceph
            repository at http://IP/ceph/ is made available
            to enable retrieval of the tag
            (git ls-remote http://IP/ceph/v$version).
            """
            ),
            epilog=textwrap.dedent(
                """
            Example:

            ceph-workbench release --suite buildpackages/any \\
                         --ceph jewel \\
                         --version 10.0.4 \\
                         --filter ubuntu_14.04,centos_7.2

            Builds CentOS 7 and Ubuntu 14.04 packages for the version
            10.0.4 from the jewel branch from http://github.com/ceph/ceph.
            The packages are built by the buildpackages/any suite which
            also installs them once as a sanity check.

            ceph-workbench release --suite buildpackages/any \\
                         --ceph-repo http://github.com/dachary/ceph \\
                         --ceph jewel \\
                         --version 10.0.4 \\
                         --filter ubuntu_14.04,centos_7.2

            Build the same release as above but use the jewel branch from
            the http://github.com/dachary/ceph repository instead of the
            default http://github.com/ceph/ceph.
            """
            ),
            help="release",
            parents=[release.CephRelease.get_parser()],
            add_help=False,
        ).set_defaults(
            func=release.CephRelease,
        )

    def run(self, argv):
        self.args = self.parser.parse_args(argv)

        if self.args.verbose:
            level = logging.DEBUG
        else:
            level = logging.INFO
        logging.getLogger("ceph_workbench").setLevel(level)

        return self.args.func(self.args).run()
