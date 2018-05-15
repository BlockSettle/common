import os
import subprocess
import shutil

from component_configurator import Configurator


class SpdlogSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._package_name = "SPDLog"
        self._version = '0.16.3'
        self._package_url = "https://github.com/gabime/spdlog/archive/v" + self._version + ".zip"

    def get_package_name(self):
        return self._package_name

    def get_url(self):
        return self._package_url

    def is_archive(self):
        return True

    def config(self):
        return True

    def make(self):
        return True

    def get_unpacked_spdlog_sources_dir(self):
        return os.path.join(self._project_settings.get_sources_dir(), 'spdlog-' + self._version)

    def install(self):
        self.filter_copy(self.get_unpacked_spdlog_sources_dir(), self.get_install_dir())
        return True
