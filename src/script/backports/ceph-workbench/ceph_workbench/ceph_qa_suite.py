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
import logging

from ceph_workbench.delegate import TeuthologyDelegate
from ceph_workbench.util import sh
from scripts import openstack

log = logging.getLogger(__name__)


class CephQaSuite(TeuthologyDelegate):
    @staticmethod
    def get_parser():
        return TeuthologyDelegate.get_parser()

    @staticmethod
    def factory(argv):
        return CephQaSuite(CephQaSuite.get_parser().parse_args(argv))

    def run(self):
        super(CephQaSuite, self).run()
        argv = self.get_trimmed_argv(openstack.get_parser(), self.args)
        command = "teuthology-openstack " + " ".join(map(lambda x: "'" + str(x) + "'", argv))
        return sh(command)
