import multiprocessing
import os
import shutil
import subprocess

from component_configurator import Configurator
from build_scripts.mpir_settings import MPIRSettings


class LibBTC(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self.mpir = MPIRSettings(settings)
        self._version = '43ed4c7d95bfb666ee009311e8f48b6a30bda464'
        self._package_name = 'libbtc'

        self._package_url = 'https://github.com/sergey-chernikov/' + self._package_name + '/archive/%s.zip' % self._version

    def get_package_name(self):
        return self._package_name + '-' + self._version

    def get_revision_string(self):
        return self._version

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'libbtc')

    def get_url(self):
        return self._package_url

    def is_archive(self):
        return True

    def config(self):
        command = ['cmake',
                   self.get_unpacked_sources_dir(),
                   '-DGMP_INSTALL_DIR=' + self.mpir.get_install_dir(),
                   '-G',
                   self._project_settings.get_cmake_generator()]

        if self._project_settings.on_windows():
            command.append('-DCMAKE_CXX_FLAGS_DEBUG=/MTd')
            command.append('-DCMAKE_CXX_FLAGS_RELEASE=/MT')

        result = subprocess.call(command)

        return result == 0

    def make_windows(self):
        command = ['devenv',
                   self.get_solution_file(),
                   '/build',
                   self.get_win_build_configuration(),
                   '/project',
                   self._package_name]

        print(' '.join(command))

        result = subprocess.call(command)
        return result == 0

    def get_solution_file(self):
        return 'libbtc.sln'

    def get_win_build_configuration(self):
        if self._project_settings.get_build_mode() == 'release':
            return 'Release'
        else:
            return 'Debug'

    def install_headers(self):
        include_dir = os.path.join(self.get_unpacked_sources_dir(), 'include')
        secp256k1_include_dir = os.path.join(self.get_unpacked_sources_dir(), 'src', 'secp256k1', 'include')
        install_include_dir = os.path.join(self.get_install_dir(), 'include')

        self.filter_copy(include_dir, install_include_dir, '.h')

        for name in os.listdir(secp256k1_include_dir):
            src_name = os.path.join(secp256k1_include_dir, name)
            dst_name = os.path.join(install_include_dir, name)

            if os.path.isfile(src_name):
                 shutil.copy(src_name, dst_name)

        return True

    def install_win(self):
        lib_dir = os.path.join(self.get_build_dir(), self.get_win_build_configuration())

        install_lib_dir = os.path.join(self.get_install_dir(), 'lib')

        self.filter_copy(lib_dir, install_lib_dir, '.lib')

        return self.install_headers()

    def make_x(self):
        command = ['make', '-j', str(multiprocessing.cpu_count())]

        result = subprocess.call(command)
        return result == 0

    def install_x(self):
        lib_dir = self.get_build_dir()

        install_lib_dir = os.path.join(self.get_install_dir(), 'lib')

        self.filter_copy(lib_dir, install_lib_dir, '.a')

        return self.install_headers()
