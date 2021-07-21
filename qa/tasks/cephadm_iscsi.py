"""
Run ceph-iscsi cluster using cephadm setup
"""
import logging
import contextlib
from io import StringIO
from textwrap import dedent
from teuthology import misc as teuthology
from teuthology.exceptions import CommandFailedError, ConnectionLostError
from teuthology.orchestra import run
from tasks import cephadm

log = logging.getLogger(__name__)


class TestCephadmIscsi():

    def __init__(self, ctx, config):

        self.ctx = ctx
        self.config = config
        nodes = []
        daemons = {}
        poolname = 'iscsi'
        if 'cluster' not in config:
            config['cluster'] = 'ceph'
        cluster_name = config['cluster']

    def _test_gateway(remote, self):
        # go to deployed iscsi daemon have to manually use podman, avoid using tcmu container
        out = remote.sh(args=['sudo',
                              'podman', 'ps', '-a',
                              run.Raw('|'), 'grep', '-F', "'iscsi'",
                              run.Raw('|'), 'grep', '-Fv', "'tcmu'",
                              run.Raw('|'), 'awk', "'{print", "$1}'"])
        log.info('podman container iscsi : {}'.format(out))

        remote.sh(['podman',
                   'exec',
                   '-it',
                   '{id}'.format(id=out),
                   '/bin/bash'])

    def test_gateway(self, ):
        for remote, roles in self.ctx.cluster.remotes.items():
            for role in [r for r in roles
                         if teuthology.is_type('iscsi', self.cluster_name)(r)]:
                # manually login to iscsi container
                _test_gateway(remote)


@contextlib.contextmanager
def task(ctx, config):
    """
    Run ceph iscsi setup.

    Specify the list of gateways to run ::

      tasks:
        ceph_iscsi:
          gateways: [a_gateway.0, c_gateway.1]
          clients: [b_client.0]

    """
    log.info('Setting ceph iscsi cluster using cephadm...')
    test_iscsi = TestCephadmIscsi(ctx, config)
    # deploy iscsi cephadm daemons
    ceph_iscsi()
    test_iscsi.test_gateway()

    try:
        yield
    finally:
        log.info('Ending ceph iscsi cephadm tests')
