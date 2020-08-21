#
#
# ***********************************************************************************
# * Copyright (C) 2018 - 2020, BlockSettle AB
# * Distributed under the GNU Affero General Public License (AGPL v3)
# * See LICENSE or http://www.gnu.org/licenses/agpl.html
# *
# **********************************************************************************
#
#
import multiprocessing
import os
import shutil
import subprocess

from component_configurator import Configurator


class TrezorCommonSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._script_revision = '4'
        self._git_revision = '74560655e3ef2f2c29901c18ccc1e82cd1424d5b'
        self._package_name = 'trezor-common'
        self._package_url = 'https://github.com/trezor/trezor-common/archive/' + self._git_revision + '.zip'

    def get_package_name(self):
        return self._package_name + '-' + self._git_revision

    def get_revision_string(self):
        return self._git_revision + '_' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'trezorCommon')

    def is_archive(self):
        return True

    def config(self):
        return True

    def make(self):
        return True

    def install(self):
        self.filter_copy(os.path.join(self.get_unpacked_sources_dir(), 'protob'), self.get_install_dir(), '.proto')

        return True


