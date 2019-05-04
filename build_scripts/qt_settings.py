import multiprocessing
import os
import shutil
import subprocess

from build_scripts.component_configurator import Configurator
from build_scripts.jom_settings import JomSettings


class QtSettings(Configurator):
    def __init__(self, settings):
        Configurator.__init__(self, settings)
        self.jom = JomSettings(settings)
        self._release = '5.12'
        self._version = self._release + '.2'
        self._package_name = 'qt-everywhere-src-' + self._version
        self._script_revision = '7'

        if self._project_settings.on_windows():
            self._package_url = 'https://download.qt.io/official_releases/qt/' + self._release + '/' + self._version + '/single/' + self._package_name + '.zip'
        else:
            self._package_url = 'https://download.qt.io/official_releases/qt/' + self._release + '/' + self._version + '/single/' + self._package_name + '.tar.xz'

    def get_package_name(self):
        return self._package_name

    def get_revision_string(self):
        return self._version + '_' + self._script_revision

    def get_url(self):
        return self._package_url

    def get_install_dir(self):
        return os.path.join(self._project_settings.get_common_build_dir(), 'Qt5')

    def is_archive(self):
        return True

    def config(self):
        command = []

        modules_to_skip = ['doc', 'webchannel', 'webview', 'sensors', 'serialport',
                           'script', 'multimedia', 'wayland', 'location', 'webglplugin', 'gamepad',
                           'purchasing', 'canvas3d', 'speech', '3d', 'androidextras', 'canvas3d',
                           'connectivity', 'virtualkeyboard']
        if self._project_settings.get_link_mode() == 'static':
            modules_to_skip.append('imageformats')

        sql_drivers_to_skip = ['db2', 'oci', 'tds', 'sqlite2', 'odbc', 'ibase', 'psql']

        if self._project_settings.on_windows():
            command.append(os.path.join(self.get_unpacked_sources_dir(), 'configure.bat'))
            command.append('-platform')
            command.append('win32-msvc' + self._project_settings.get_vs_year())
        else:
            command.append(os.path.join(self.get_unpacked_sources_dir(), 'configure'))

        if self._project_settings.get_build_mode() == 'release':
            command.append('-release')
            command.append('-no-qml-debug')
        else:
            command.append('-debug')

        if self._project_settings.on_linux():
            command.append('-dbus')
        else:
            command.append('-no-dbus')

        if self._project_settings.get_link_mode() == 'static':
            command.append('-static')
            if self._project_settings.on_windows():
                command.append('-static-runtime')

        command.append('-confirm-license')
        command.append('-opensource')
        command.append('-no-opengl')
        command.append('-qt-pcre')
        command.append('-qt-harfbuzz')
        command.append('-sql-sqlite')
        command.append('-sql-mysql')
        command.append('-no-feature-vulkan')
        command.append('-no-openssl')

        if self._project_settings.on_osx():
            command.append('-L/usr/local/opt/mysql@5.7/lib')
            command.append('-I/usr/local/opt/mysql@5.7/include')
            command.append('-I/usr/local/opt/mysql@5.7/include/mysql')

        if self._project_settings.on_linux():
            command.append('-system-freetype')
            command.append('-fontconfig')

            command.append('-no-glib')
            command.append('-cups')
            command.append('-no-icu')
            command.append('-nomake')
            command.append('tools')
        else:
            command.append('-qt-libpng')
            command.append('-no-freetype')

        if self._project_settings.on_windows():
            command.append('-IC:\Program Files\MySQL\MySQL Connector C 6.1\include')
            command.append('-LC:\Program Files\MySQL\MySQL Connector C 6.1\lib')

        command.append('-nomake')
        command.append('tests')
        command.append('-nomake')
        command.append('examples')

        for driver in sql_drivers_to_skip:
            command.append('-no-sql-' + driver)

        for module in modules_to_skip:
            command.append('-skip')
            command.append(module)

        command.append('-prefix')
        command.append(self.get_install_dir())

        result = subprocess.call(command, env=compile_variables)
        if result != 0:
            print('Configure of Qt failed')
            return False

        return True

    def make(self):
        command = []

        if self._project_settings.on_windows():
            command.append(self.jom.get_executable_path())
            if self._project_settings.get_link_mode() == 'static':
                command.append('mode=static')
        else:
            command.append('make')
            command.append('-j')
            command.append(str(max(1, multiprocessing.cpu_count() - 1)))

        result = subprocess.call(command)
        if result != 0:
            print('Qt make failed')
            return False

        return True

    def install(self):
        command = []
        if self._project_settings.on_windows():
            command.append('nmake')
        else:
            command.append('make')

        command.append('install')
        result = subprocess.call(command)
        if result != 0:
            print('Qt install failed')
            return False

        return True
