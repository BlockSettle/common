#
#
# ***********************************************************************************
# * Copyright (C) 2020, BlockSettle AB
# * Distributed under the GNU Affero General Public License (AGPL v3)
# * See LICENSE or http://www.gnu.org/licenses/agpl.html
# *
# **********************************************************************************
#
#
import os
import subprocess
import shutil

from component_configurator import Configurator

class ArgparseSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = 'de4db870058c37b6094bc5ccb03c9ea45708c855'
        self._package_name = 'args' + self._version
        # self._package_url = 'https://github.com/p-ranav/argparse/archive/v' + self._version + '.zip'
        self._package_url = 'https://github.com/Taywee/args/archive/' + self._version + '.zip'

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version

    def get_url(self):
        return self._package_url

    def is_archive(self):
        return True

    def config(self):
        return True

    def make(self):
        return True

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'Argparse')

    def get_unpacked_argparse_sources_dir(self):
        return os.path.join(self._project_settings.get_sources_dir(), 'args-' + self._version)

    def install(self):
        self.filter_copy(self.get_unpacked_argparse_sources_dir(), self.get_install_dir())
        return True
