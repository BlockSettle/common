#
#
# ***********************************************************************************
# * Copyright (C) 2021, BlockSettle AB
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

class TOML_CPP(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '2.3.0'
        self._package_name = 'TOML' + self._version
        self._package_url = "https://github.com/marzer/tomlplusplus/archive/v" + self._version + ".zip"

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
        return os.path.join(self._project_settings.get_common_build_dir(), 'TOML')

    def get_unpacked_toml_sources_dir(self):
        return os.path.join(self._project_settings.get_sources_dir(), 'tomlplusplus-' + self._version)

    def install(self):
        self.filter_copy(self.get_unpacked_toml_sources_dir(), self.get_install_dir())
        return True
