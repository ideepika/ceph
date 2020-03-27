import logging
import rados

from typing import Dict, Optional

from ceph.deployment.service_spec import NFSServiceSpec

import cephadm
from orchestrator import OrchestratorError

from . import utils

logger = logging.getLogger(__name__)

class NFSGanesha(object):
    def __init__(self,
                 mgr,
                 daemon_id,
                 spec):
        # type: (cephadm.CephadmOrchestrator, str, NFSServiceSpec) -> None
        self.mgr = mgr
        self.daemon_id = daemon_id
        self.spec = spec

    def get_rados_user(self):
        # type: () -> str
        return '%s.%s' % (self.spec.service_type, self.daemon_id)

    def get_rados_config_name(self):
        # type: () -> str
        return 'conf-' + self.spec.service_name()

    def get_rados_config_url(self):
        # type: () -> str
        url = 'rados://' + self.spec.pool + '/'
        if self.spec.namespace:
            url += self.spec.namespace + '/'
        url += self.get_rados_config_name()
        return url

    def get_keyring_entity(self):
        # type: () -> str
        return utils.name_to_config_section(self.get_rados_user())

    def get_or_create_keyring(self, entity=None):
        # type: (Optional[str]) -> str
        if not entity:
            entity = self.get_keyring_entity()

        logger.info('Create keyring: %s' % entity)
        ret, keyring, err = self.mgr.mon_command({
            'prefix': 'auth get-or-create',
            'entity': entity,
        })

        if ret != 0:
            raise OrchestratorError(
                    'Unable to create keyring %s: %s %s' \
                            % (entity, ret, err))
        return keyring

    def update_keyring_caps(self, entity=None):
        # type: (Optional[str]) -> None
        if not entity:
            entity = self.get_keyring_entity()

        osd_caps='allow rw pool=%s' % (self.spec.pool)
        if self.spec.namespace:
            osd_caps='%s namespace=%s' % (osd_caps, self.spec.namespace)

        logger.info('Updating keyring caps: %s' % entity)
        ret, out, err = self.mgr.mon_command({
            'prefix': 'auth caps',
            'entity': entity,
            'caps': ['mon', 'allow r',
                     'osd', osd_caps,
                     'mds', 'allow rw'],
        })

        if ret != 0:
            raise OrchestratorError(
                    'Unable to update keyring caps %s: %s %s' \
                            % (entity, ret, err))

    def create_rados_config_obj(self, clobber=False):
        # type: (Optional[bool]) -> None
        obj = self.get_rados_config_name()

        with self.mgr.rados.open_ioctx(self.spec.pool) as ioctx:
            if self.spec.namespace:
                ioctx.set_namespace(self.spec.namespace)

            exists = True
            try:
                ioctx.stat(obj)
            except rados.ObjectNotFound as e:
                exists = False

            if exists and not clobber:
                # Assume an existing config
                logger.info('Rados config object exists: %s' % obj)
            else:
                # Create an empty config object
                logger.info('Creating rados config object: %s' % obj)
                ioctx.write_full(obj, ''.encode('utf-8'))

    def get_ganesha_conf(self):
        # type: () -> str
        return '''# generated by cephadm
RADOS_URLS {{
        UserId = "{user}";
        watch_url = "{url}";
}}

%url    {url}
'''.format(user=self.get_rados_user(),
           url=self.get_rados_config_url())

    def get_cephadm_config(self):
        # type: () -> Dict
        config = {'pool' : self.spec.pool} # type: Dict
        if self.spec.namespace:
            config['namespace'] = self.spec.namespace
        config['files'] = {
            'ganesha.conf' : self.get_ganesha_conf(),
        }
        logger.debug('Generated cephadm config-json: %s' % config)
        return config
