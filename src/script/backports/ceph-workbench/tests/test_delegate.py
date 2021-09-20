# -*- mode: python; coding: utf-8 -*-
#
# Copyright (C) 2016 <contact@redhat.com>
#
# Author: Loic Dachary <loic@dachary.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
import argparse
import json
import logging
import mock
import os
import pprint
import pytest  # noqa # it provides caplog
import shutil
import tempfile

from ceph_workbench import delegate
from ceph_workbench import util
from scripts.openstack import get_parser
from teuthology import openstack

logging.basicConfig(format='%(asctime)s %(levelname)s %(message)s',
                    level=logging.DEBUG)


class TestCephQaSuite(object):

    def setup(self):
        self.d = tempfile.mkdtemp()

    def teardown(self):
        shutil.rmtree(self.d)

    def test_get_trimmed_argv(self):
        d_parser = delegate.TeuthologyDelegate.get_parser()
        parser = argparse.ArgumentParser(
            parents=[
                d_parser,
            ],
            conflict_handler='resolve',
        )
        parser.add_argument('--something-with-arg')
        parser.add_argument('--no-arg', action='store_true')
        pprint.pprint(vars(parser))
        argv_discarded = [
            '--no-arg',
            '--something-with-arg', 'arg',
        ]
        argv_preserved = [
            '-s', 'dummy',
            'distros/all/jessie-8.0.yaml',
        ]
        argv = argv_discarded + argv_preserved
        args = parser.parse_args(argv)
        assert 'openrc.sh' == args.openrc
        trimmed_argv = delegate.TeuthologyDelegate.get_trimmed_argv(
            get_parser(), args)
        expected = ['--key-name',
                    'teuthology-myself',
                    '--simultaneous-jobs',
                    10,
                    '-s',
                    'dummy',
                    '--teuthology-git-url',
                    'http://github.com/SUSE/teuthology',
                    'distros/all/jessie-8.0.yaml']
        assert expected == trimmed_argv

    def test_verify_keys(self):
        if 'OS_AUTH_URL' not in os.environ:
            pytest.skip('no OS_AUTH_URL environment variable')
        config_dir = self.d + "/verify_keys"
        with mock.patch.multiple(
                delegate,
                get_config_dir=lambda: config_dir,
        ):
            key_name = 'teuthology-test'
            c = delegate.TeuthologyDelegate(argparse.Namespace(
                key_name=key_name,
            ))
            #
            # create the keypair and the public / private files
            #
            private_key = config_dir + "/" + key_name + ".pem"
            public_key = config_dir + "/" + key_name + ".pub"
            assert not os.path.exists(private_key)
            assert not os.path.exists(public_key)
            c.verify_keys()
            assert os.path.exists(private_key)
            assert os.path.exists(public_key)
            keypair = json.loads(util.sh(
                "openstack keypair show -f json " + key_name))
            fingerprint = util.sh("ssh-keygen -Emd5 -l -f " + public_key + " 2>/dev/null | sed -e 's/.*MD5:\([^ ]*\).*/\\1/'")
            if not fingerprint:
                fingerprint = util.sh("ssh-keygen -l -f " + public_key + " | cut -d' ' -f2")
            assert openstack.OpenStack.get_value(
                keypair, 'fingerprint') == fingerprint.strip()

            #
            # create the keypair from existing public / private files
            #
            util.sh("openstack keypair delete " + key_name)
            c.args.key_filename = None
            c.verify_keys()
            keypair = json.loads(util.sh(
                "openstack keypair show -f json " + key_name))
            assert openstack.OpenStack.get_value(
                keypair, 'fingerprint') == fingerprint.strip()

            #
            # re-create the keypair with the existing public key
            # if the fingerprints do not match
            #
            util.sh("""
            cd {config_dir}
            ssh-keygen -q -N '' -f fake
            openstack keypair delete {key_name}
            openstack keypair create --public-key fake.pub {key_name}
            """.format(config_dir=config_dir,
                       key_name=key_name))
            other_keypair = json.loads(util.sh(
                "openstack keypair show -f json " + key_name))
            assert openstack.OpenStack.get_value(
                other_keypair, 'fingerprint') != fingerprint.strip()
            c.args.key_filename = None
            c.verify_keys()
            keypair = json.loads(util.sh(
                "openstack keypair show -f json " + key_name))
            assert openstack.OpenStack.get_value(
                keypair, 'fingerprint') == fingerprint.strip()

# Local Variables:
# compile-command: "cd .. ; tox -e py27 tests/test_delegate.py"
# End:
