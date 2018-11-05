import multiprocessing
import os
import shutil
import subprocess

from component_configurator import Configurator
from openssl_settings import OpenSslSettings

class WebsocketsSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self._version = '3.0.1'
        self._package_name = 'libwebsockets'
        self._package_url = 'https://github.com/warmcat/libwebsockets/archive/v' + self._version + '.zip'

        self._ssl_settings = OpenSslSettings(settings)

    def get_package_name(self):
        return self._package_name + '-' + self._version

    def get_revision_string(self):
        return self._version

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'libwebsockets')

    def get_url(self):
        return self._package_url

    def is_archive(self):
        return True

    def config(self):
        command = ['cmake',
                   os.path.join(self._project_settings.get_sources_dir(), self._package_name + '-' + self._version),
                   '-DLWS_WITH_SHARED=OFF',
                   '-DLWS_WITHOUT_SERVER=ON',
                   '-DLWS_WITHOUT_TESTAPPS=ON',
                   '-DLWS_WITHOUT_TEST_SERVER=ON',
                   '-DLWS_WITHOUT_TEST_PING=ON',
                   '-DLWS_WITHOUT_TEST_CLIENT=ON',
                   '-G',
                   self._project_settings.get_cmake_generator()]

        env_vars = os.environ.copy()
        env_vars['OPENSSL_ROOT_DIR'] = self._ssl_settings.get_install_dir()
        env_vars['OPENSSL_INCLUDE_DIR'] = os.path.join(self._ssl_settings.get_install_dir(), 'include')

        result = subprocess.call(command, env=env_vars)
        return result == 0

    def make_windows(self):
        command = ['devenv',
                   self.get_solution_file(),
                   '/build',
                   self.get_win_build_configuration()]

        print('Start building libwebsockets')
        print(' '.join(command))

        result = subprocess.call(command)
        return result == 0

    def get_solution_file(self):
        return 'libwebsockets.sln'

    def get_win_build_configuration(self):
        if self._project_settings.get_build_mode() == 'release':
            return 'Release'
        else:
            return 'Debug'

    def install_win(self):
        lib_dir = os.path.join(self.get_build_dir(), 'lib', self.get_win_build_configuration())
        include_dir = os.path.join(self.get_build_dir(), 'include')

        install_lib_dir = os.path.join(self.get_install_dir(), 'lib')
        install_include_dir = os.path.join(self.get_install_dir(), 'include')

        self.filter_copy(lib_dir, install_lib_dir, '.lib')
        self.filter_copy(include_dir, install_include_dir)

        return True

    def make_x(self):
        command = ['make', '-j', str(multiprocessing.cpu_count())]

        result = subprocess.call(command)
        return result == 0

    def install_x(self):
        lib_dir = os.path.join(self.get_build_dir(), 'lib')
        include_dir = os.path.join(self.get_build_dir(), 'include')

        install_lib_dir = os.path.join(self.get_install_dir(), 'lib')
        install_include_dir = os.path.join(self.get_install_dir(), 'include')

        self.filter_copy(lib_dir, install_lib_dir, '.a')
        self.filter_copy(include_dir, install_include_dir)

        return True
