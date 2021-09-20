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
import os
import subprocess

log = logging.getLogger(__name__)


def get_config_dir():
    return os.path.expanduser("~/.ceph-workbench")


def sh(command, input=None):
    log.debug(":sh: " + command)
    if input is None:
        stdin = None
    else:
        stdin = subprocess.PIPE
    proc = subprocess.Popen(
        args=command, stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=True, bufsize=1
    )
    if stdin is not None:
        proc.stdin.write(input)
        proc.stdin.close()
    lines = []
    with proc.stdout:
        for line in iter(proc.stdout.readline, b""):
            line = line.decode("utf-8")
            lines.append(line)
            log.debug(line.strip().encode("ascii", "ignore"))
    if proc.wait() != 0:
        raise subprocess.CalledProcessError(returncode=proc.returncode, cmd=command)
    return "".join(lines)


class CommaSplit(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, values.split(","))


def get_parser():
    parser = argparse.ArgumentParser(
        description="Ceph",
        add_help=False,
    )

    parser.add_argument(
        "--releases",
        action=CommaSplit,
        default=[],
        help=("Comma separated list of releases " "(e.g. dumpling,firefly)"),
    )
    return parser


def releases():
    return (
        "argonaut",
        "bobtail",
        "cuttlefish",
        "dumpling",
        "emperor",
        "firefly",
        "giant",
        "hammer",
        "infernalis",
        "jewel",
        "kraken",
        "luminous",
    )
