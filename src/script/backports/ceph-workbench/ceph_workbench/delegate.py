# -*- mode: python; coding: utf-8 -*-
#
# Copyright (C) 2016 <contact@redhat.com>
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
import collections
import logging
import os

from ceph_workbench.util import get_config_dir, sh
from scripts import openstack

log = logging.getLogger(__name__)


class TeuthologyDelegate(object):
    def __init__(self, args):
        self.args = args

    @staticmethod
    def get_parser():
        o_parser = openstack.get_openstack_parser()
        o_arg = o_parser._option_string_actions["--teuthology-branch"]
        o_arg.default = "master"
        o_arg = o_parser._option_string_actions["--simultaneous-jobs"]
        o_arg.default = 10
        o_arg = o_parser._option_string_actions["--teuthology-git-url"]
        o_arg.default = "http://github.com/SUSE/teuthology"
        k_parser = openstack.get_key_parser()
        k_arg = k_parser._option_string_actions["--key-name"]
        k_arg.default = "teuthology-myself"
        parser = argparse.ArgumentParser(
            parents=[
                o_parser,
                k_parser,
                openstack.get_suite_parser(),
            ],
            conflict_handler="resolve",
        )
        # implemented in docker/entrypoint.sh
        parser.add_argument(
            "--openrc",
            help="OpenStack credentials file, relative to ~/.ceph-workbench",
            default="openrc.sh",
        )
        return parser

    def verify_keys(self):
        key_dir = get_config_dir()
        if not os.path.exists(key_dir):
            os.mkdir(key_dir)
        sh(
            """
        cd {key_dir}
        set -x
        if ! test -f {key_name}.pem ; then
            openstack keypair delete {key_name} || true
            openstack keypair create {key_name} > {key_name}.pem || exit 1
            chmod 600 {key_name}.pem
        fi
        if ! test -f {key_name}.pub ; then
            if ! ssh-keygen -y -f {key_name}.pem > {key_name}.pub ; then
               cat {key_name}.pub
               exit 1
            fi
        fi
        if ! openstack keypair show --public-key {key_name} > {key_name}.keypair > {key_name}.check ; then # noqa
            openstack keypair create --public-key {key_name}.pub {key_name} || exit 1 # noqa
        else
            if ! diff -uB {key_name}.pub {key_name}.check ; then
                openstack keypair delete {key_name} || exit 1
                openstack keypair create --public-key {key_name}.pub {key_name} || exit 1 # noqa
            fi
        fi
        """.format(
                key_dir=key_dir, key_name=self.args.key_name
            )
        )
        self.args.key_filename = os.path.join(key_dir, self.args.key_name + ".pem")

    @staticmethod
    def get_trimmed_argv(to_parser, args):
        options = []
        positionals = []
        dest2option = {}
        known_dests = []
        for action in to_parser._actions:
            option = filter(lambda o: o in to_parser._option_string_actions, action.option_strings)
            if len(option) > 0:
                dest2option[action.dest] = option[0]
            known_dests.append(action.dest)
        v = collections.OrderedDict(sorted(vars(args).items(), key=lambda t: t[0]))
        for (key, value) in v.iteritems():
            logging.debug("get_trimmed_argv: checking " + str(key) + "=" + str(value))
            if key not in known_dests:
                logging.debug("get_trimmed_argv: skip unknown " + str(key))
                continue
            if key in dest2option:
                option = dest2option[key]
                action = to_parser._option_string_actions[option]
                if value != action.default:
                    if action.nargs is None or action.nargs == 1:
                        options.extend([option, value])
                    elif action.nargs == 0:
                        options.append(option)
            else:
                logging.debug("get_trimmed_argv: positional " + str(key) + "=" + str(value))
                positionals.extend(value)
        return options + positionals

    def run(self):
        self.verify_keys()
