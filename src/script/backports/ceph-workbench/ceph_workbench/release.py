# -*- mode: python; coding: utf-8 -*-
#
# Copyright (C) 2016 Red Hat <contact@redhat.com>
# Copyright (C) 2016 Martin Palma <martin@palma.bz>
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
import os
import re
import textwrap

import urllib2
from ceph_workbench.delegate import TeuthologyDelegate
from ceph_workbench.util import get_config_dir, sh
from scripts import openstack
from teuthology.openstack import OpenStackInstance

log = logging.getLogger(__name__)


class CephRelease(TeuthologyDelegate):
    @staticmethod
    def get_parser():
        parser = argparse.ArgumentParser(
            parents=[
                TeuthologyDelegate.get_parser(),
            ],
            conflict_handler="resolve",
        )
        parser.add_argument(
            "--version",
            help="version to publish 10.0.4, 11.1.3 etc.",
            required=True,
        )
        parser.add_argument(
            "--tag-phase",
            action="store_true",
            default=None,
            help="tag the branch",
        )
        parser.add_argument(
            "--build-phase",
            action="store_true",
            default=None,
            help="build the packages",
        )
        parser.add_argument(
            "--release-phase",
            action="store_true",
            default=None,
            help=(
                "collect and sign the packages into a repository "
                "with a layout compatible with downloads.ceph.com"
            ),
        )
        parser.add_argument(
            "--merge-from",
            help=("URL from which downloads.ceph.com compatible " "repositories can be downloaded"),
        )
        parser.add_argument(
            "--merge-phase",
            action="store_true",
            default=None,
            help=("merge a remote downloads.ceph.com compatible " "repository with a local repository"),
        )
        parser.add_argument(
            "--verify-phase",
            action="store_true",
            default=None,
            help=("verify the packages at the --verify " "URL are usable"),
        )
        parser.add_argument(
            "--verify",
            help=("URL of packages created by the release phase " " (default http://packages-repository)"),
        )
        parser.add_argument(
            "--coverage",
            action="store_true",
            default=None,
            help="collect coverage for the remote ceph-workbench",
        )
        return parser

    @staticmethod
    def factory(argv):
        return CephRelease(CephRelease.get_parser().parse_args(argv))

    def run(self):
        if self.args.tag_phase:
            self.tag_phase()
        if self.args.build_phase:
            self.build_phase()
        if self.args.release_phase:
            self.release_phase()
        if self.args.merge_phase:
            self.merge_phase()
        if self.args.verify_phase:
            self.verify_phase()
        if (
            not self.args.tag_phase
            and not self.args.build_phase
            and not self.args.release_phase
            and not self.args.verify_phase
            and not self.args.merge_phase
        ):
            super(CephRelease, self).run()
            self.delegate_phase()
        return 0

    def delegate_phase(self):
        #
        # Create or re-use the teuthology cluster
        #
        argv = self.get_trimmed_argv(TeuthologyDelegate.get_parser(), self.args)
        command = (
            "teuthology-openstack --dry-run "
            + "--teuthology-branch "
            + self.args.teuthology_branch
            + " "
            + "--teuthology-git-url "
            + self.args.teuthology_git_url
            + " "
            + "--simultaneous-jobs "
            + str(self.args.simultaneous_jobs)
            + " "
            + " ".join(map(lambda x: "'" + str(x) + "'", argv))
        )
        sh(command)
        ip = OpenStackInstance(self.args.name).get_floating_ip_or_ip()
        #
        # Create the GPG key, if it does not already exists. Make sure
        # it does *not* have subkeys otherwise rpm -K will fail.
        #
        keydir = get_config_dir() + "/gpg"
        sh(
            """
        set -ex
        export GNUPGHOME={keydir}
        mkdir -p $GNUPGHOME
        chmod 700 $GNUPGHOME
        key={keydir}/release-key.asc
        if ! test -f $key ; then
            (
              echo "Key-Type: 1"
              echo "Key-Length: 2048"
              echo "Name-Real: A Contributor"
              echo "Name-Email: generous@ceph.com"
              echo "Expire-Date: 0"
            ) | gpg --batch --gen-key
            gpg --export --armor > $key
        fi
        gpg --fingerprint # for debug purposes
        """.format(
                keydir=keydir
            )
        )
        sh(
            "scp "
            + " -o StrictHostKeyChecking=false"
            + " -i "
            + self.args.key_filename
            + " -r "
            + keydir
            + " ubuntu@"
            + ip
            + ":.gnupg"
        )
        #
        # run the command asynchronously, in the cluster
        #
        if self.args.coverage:
            coverage = textwrap.dedent(
                """
            cat > /tmp/.coveragerc <<EOF
            [run]
            data_file=/tmp/.coverage
            EOF
            coverage run \
              --rcfile=/tmp/.coveragerc \
              --source=ceph-workbench/ceph_workbench \
              teuthology/virtualenv/bin/"""
            )
        else:
            coverage = ""  # pragma: no cover
        argv = self.get_trimmed_argv(self.get_parser(), self.args)
        command = """
        set -ex
        source .bashrc_teuthology
        sudo mkdir -p /var/log/ceph-workbench
        sudo chown -R $(id -u) /var/log/ceph-workbench
        type ceph-workbench
        touch /var/log/ceph-workbench/$$.log
        {coverage}ceph-workbench release \
              --tag-phase --build-phase --release-phase --verify-phase \
              --wait {args} \
              --key-name teuthology \
              --key-filename ~/.ceph-workbench/teuthology.pem \
              > /var/log/ceph-workbench/$$.log 2>&1 &
        tail -f --pid $! /var/log/ceph-workbench/$$.log
        ! tail -1 /var/log/ceph-workbench/$$.log | \
           grep --quiet 'returned non-zero exit status'
        """.format(
            args=" ".join(map(lambda x: "'" + str(x) + "'", argv)), coverage=coverage
        )
        sh("ssh -i " + self.args.key_filename + " ubuntu@" + ip + " bash", command)

    def tag_phase(self):
        tag = "v" + self.args.version
        sh(
            """
        set -ex
        cd /usr/share/nginx/html
        rm -fr ceph
        git clone --bare -b {branch} {git_url} ceph
        cd ceph
        if git rev-parse --quiet --verify tags/{tag} ; then
            git update-ref {branch} $(git rev-parse tags/{tag})
        else
            git -c user.name="Contributor" -c user.email=generous@ceph.com \
                tag \
                --annotate --message {tag} {tag}
        fi
        git update-server-info
        """.format(
                branch=self.args.ceph, git_url=self.args.ceph_repo, tag=tag
            )
        )

    def build_phase(self):
        tag = "v" + self.args.version
        ip = OpenStackInstance(self.args.name).get_floating_ip_or_ip()
        self.args.ceph_repo = "http://" + ip + "/ceph"
        self.args.ceph = tag
        argv = self.get_trimmed_argv(openstack.get_parser(), self.args)
        command = "teuthology-openstack " + " ".join(map(lambda x: "'" + str(x) + "'", argv))
        sh(command)

    @staticmethod
    def discover_builds(url, version, sha1):
        root = urllib2.urlopen(url).read()
        url_base = os.path.dirname(url)
        builds = []
        for build in re.finditer('href="(\w+)-(\w+)-(\w+)-(\w+)-(\w+)', root):
            found = "-".join(build.groups())
            log.debug("found " + found)
            repo_url = os.path.join(url_base, found, "ref", version)
            sha1_url = os.path.join(repo_url, "sha1")
            try:
                log.debug("verify " + sha1_url)
                actual_sha1 = urllib2.urlopen(sha1_url).read().strip()
            except urllib2.URLError as e:
                log.debug("skip because " + str(e))
                continue
            if sha1 != actual_sha1:
                log.debug("skip because the sha1 " + sha1 + " != " + actual_sha1)
                continue
            builds.append(
                {
                    "url": repo_url,
                    "version": version,
                    "sha1": sha1,
                    "project": build.group(1),
                    "pkg_type": build.group(2),
                    "dist": build.group(3),
                    "arch": build.group(4),
                    "flavor": build.group(5),
                }
            )
        return builds

    def get_sha1(self, version):
        return sh(
            """
        cd /usr/share/nginx/html/ceph
        git rev-parse %version% # ^{commit}
        """.replace(
                "%version%", version
            )
        ).strip()

    @staticmethod
    def get_codenames(distributions):
        if not os.path.exists(distributions):
            return set()
        content = open(distributions, "r").read()
        codenames = set(re.findall("Codename: (\w+)", content))
        log.debug(distributions + " codenames " + str(codenames))
        return codenames

    def release_deb(self, build):
        path = "debian-testing"
        sh("sudo apt-get install -y reprepro")
        sh("mkdir -p " + path + "/conf")
        distributions = path + "/conf/distributions"
        if not build["dist"] in self.get_codenames(distributions):
            open(distributions, "a").write(
                textwrap.dedent(
                    """
            Codename: {dist}
            Architectures: i386 amd64 arm64 source
            Components: main
            SignWith: default
            """.format(
                        **build
                    )
                )
            )
        sh(
            """
        reprepro --basedir {path} includedsc {dist} \
              $(find deb/{version} -name '*.dsc')
        reprepro --basedir {path} includedeb {dist} \
              $(find deb/{version} -name '*.deb')
        """.format(
                path=path, **build
            )
        )
        return path

    @staticmethod
    def rpm_createrepo(path):
        sh(
            """
        set -ex
        gpg_name=$(gpg --list-secret-keys --with-colons | \
                   grep '^sec' | cut -d: -f10)
        for dir in {path}/* ; do
           (
              cd $dir
              sudo docker run -v $HOME/.gnupg:/root/.gnupg \
                              -v $(pwd):$(pwd) -w $(pwd) \
                              --rm \
                              centos:7 \
                              bash -c "echo '%_gpg_name $gpg_name' > /root/.rpmmacros ; yum install -y --quiet rpm-sign createrepo ; rpm --addsign *.rpm ; createrepo ." # noqa
              sudo chown -R $(id -u) . ~/.gnupg # in case docker running as root did things # noqa
              gpg --detach-sign --armor --yes repodata/repomd.xml
           )
        done
        """.format(
                path=path
            )
        )

    def release_rpm(self, build):
        # use the function from teuthology instead
        dist2distro = {
            "centos7": "el7",
        }
        path = "rpm-testing"
        sh(
            """
        set -ex
        mkdir -p {path}
        (
          cd rpm/{version}
          for dir in * ; do
             if test -d $dir ; then
                mkdir -p ../../{path}/{distro}/$dir
                ln -f $dir/* ../../{path}/{distro}/$dir || true
             fi
          done
        )
        """.format(
                distro=dist2distro[build["dist"]], path=path, **build
            )
        )
        self.rpm_createrepo(os.path.join(path, dist2distro[build["dist"]]))
        return path

    def release_phase(self):
        ip = OpenStackInstance("packages-repository").get_floating_ip_or_ip()
        version = "v" + self.args.version
        sha1 = self.get_sha1(version)
        builds = self.discover_builds("http://" + ip + "/", version, sha1)
        sh("sudo apt-get install -y lftp")
        os.chdir("/usr/share/nginx/html")
        for build in builds:
            sh(
                """
            mkdir -p {pkg_type}
            cd {pkg_type}
            lftp -c 'mirror {url}'
            """.format(
                    **build
                )
            )
        publish = set()
        for build in builds:
            if build["pkg_type"] == "deb":
                publish.add(self.release_deb(build))
            elif build["pkg_type"] == "rpm":
                publish.add(self.release_rpm(build))
            else:
                log.info(build["url"] + " ignored because of unknown pkg_type = " + build["pkg_type"])
        for path in publish:
            sh("rsync --delete -av " + path + "/ " + ip + ":/usr/share/nginx/html/" + path + "/")
        sh("rsync -v ~/.gnupg/release-key.asc " + ip + ":/usr/share/nginx/html")

    def verify_repositories(self, url, version):
        key = url + "/release-key.asc"
        repositories = self.discover_repositories(url)
        for repository in repositories:
            if "/debian-" in repository:
                self.verify_deb(repository, key, version)
            elif "/rpm-" in repository:
                for distro in ("el7",):
                    self.verify_rpm(repository + "/" + distro, key, version)

    def verify_phase(self):
        if self.args.verify:
            url = self.args.verify
        else:
            ip = OpenStackInstance("packages-repository").get_floating_ip_or_ip()
            url = "http://" + ip + "/"
        self.verify_repositories(url, self.args.version)

    @staticmethod
    def verify_deb(repository, key, version):
        sh(
            textwrap.dedent(
                """
        sudo docker run --rm -i ubuntu:14.04 bash -ex <<'DOCKER' # noqa
        apt-get install -y wget
        wget -q -O- '{key}' | sudo apt-key add -
        (
          echo deb {repository} $(lsb_release -sc) main
          echo deb-src {repository} $(lsb_release -sc) main
        ) | sudo tee /etc/apt/sources.list.d/ceph.list
        apt-get update

        apt-get install -y dpkg-dev
        apt-get source ceph
        apt-get install -y ceph
        ceph --version | grep {version}
        DOCKER
        """.format(
                    repository=repository, key=key, version=version
                )
            )
        )

    @staticmethod
    def verify_rpm(repository, key, version):
        sh(
            textwrap.dedent(
                """
        sudo docker run --rm -i centos:7 bash -ex <<'DOCKER' # noqa
        yum install -y redhat-lsb-core
        MAJOR_VERSION=$(lsb_release -rs | cut -f1 -d.)
        yum-config-manager --add-repo https://dl.fedoraproject.org/pub/epel/$MAJOR_VERSION/x86_64/
        yum install --nogpgcheck -y epel-release
        rpm --import /etc/pki/rpm-gpg/RPM-GPG-KEY-EPEL-$MAJOR_VERSION
        if test $(lsb_release -si) = CentOS -a $MAJOR_VERSION = 7 ; then
          yum-config-manager --enable cr
        fi
        yum install -y yum-plugin-priorities
        yum install -y snappy leveldb gdisk python-argparse gperftools-libs

        cat > /etc/yum.repos.d/ceph.repo <<'EOF'
        [ceph]
        name=Ceph packages for $basearch
        baseurl={repository}/$basearch
        enabled=1
        priority=2
        gpgcheck=1
        type=rpm-md
        
        [ceph-noarch]
        name=Ceph packages for noarch
        baseurl={repository}/noarch
        enabled=1
        priority=2
        gpgcheck=1
        type=rpm-md
        
        [ceph-source]
        name=Ceph packages for source
        baseurl={repository}/SRPMS
        enabled=1
        priority=2
        gpgcheck=1
        type=rpm-md
        EOF
        
        rpm --import {key}
        yumdownloader --source ceph
        yum install -y ceph
        ceph --version | grep {version}
        DOCKER
        """.format(
                    repository=repository, key=key, version=version
                )
            )
        )

    @staticmethod
    def discover_repositories(url):
        root = urllib2.urlopen(url).read()
        url_base = os.path.dirname(url)
        repositories = []
        for repository in re.finditer('href="(rpm|debian)-(\w+)', root):
            found = "-".join(repository.groups())
            log.debug("found " + found)
            repo_url = os.path.join(url_base, found)
            repositories.append(repo_url)
        return repositories

    @staticmethod
    def merge_deb_fix_conf(incoming, outgoing):
        updates = []
        incoming = os.path.realpath(incoming)
        codenames = CephRelease.get_codenames(incoming + "/conf/distributions")
        for codename in codenames:
            updates.append(
                textwrap.dedent(
                    """
            Name: {suite}
            Method: file://{incoming}
            Suite: {suite}
            VerifyRelease: blindtrust
            Architectures: i386 amd64 arm64 source
            Components: main
            """.format(
                        suite=codename, incoming=incoming
                    )
                )
            )
        open(outgoing + "/conf/updates", "w").write("".join(updates))
        input = open(outgoing + "/conf/distributions", "r").read().split("\n")
        output = []
        for line in input:
            if ":" not in line:
                output.append(line)
                continue
            (key, value) = line.split(":", 1)
            if key == "Codename":
                output.append(line)
                if value.strip() in codenames:
                    output.append("Update: " + codename)
                    output.append("SignWith: default")
            elif key in ("SignWith", "Update"):
                pass
            else:
                output.append(line)
        open(outgoing + "/conf/distributions", "w").write("\n".join(output))

    def merge_deb(self, incoming, outgoing):
        sh("sudo apt-get install -y reprepro")
        self.merge_deb_fix_conf(incoming, outgoing)
        sh("cd " + outgoing + " ; reprepro --ignore=undefinedtarget update")

    def merge_rpm(self, incoming, outgoing):
        sh("rsync -av --exclude repodata " + incoming + "/ " + outgoing + "/")
        for dirpath, dirnames, filenames in os.walk(outgoing):
            if "SRPMS" in dirnames:
                self.rpm_createrepo(dirpath)

    def merge_phase(self):
        os.chdir("/usr/share/nginx/html")
        if self.args.merge_from:
            repositories = self.discover_repositories(self.args.merge_from)
            sh("sudo apt-get install -y lftp")
            sh("mkdir -p tmp")
            for repository in repositories:
                sh(
                    """
                cd tmp
                lftp -c 'mirror {url}'
                """.format(
                        url=repository
                    )
                )
        for repository in os.listdir("tmp"):
            incoming = "tmp/" + repository
            outgoing = repository
            if not os.path.exists(outgoing):
                sh("mv " + incoming + " " + outgoing)
                continue
            if repository.startswith("debian"):
                self.merge_deb(incoming, outgoing)
            elif repository.startswith("rpm"):
                self.merge_rpm(incoming, outgoing)
            else:
                log.info(repository + " ignored, it does not start with {rpm,debian}")
        if self.args.merge_from:
            sh("rm -fr tmp")
