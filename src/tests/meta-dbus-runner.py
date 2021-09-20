#!/usr/bin/env python3

import dbus
import sys
import os
import fcntl
import subprocess
from collections import OrderedDict
from dbusmock import DBusTestCase
from dbus.mainloop.glib import DBusGMainLoop

DBusGMainLoop(set_as_default=True)


def set_nonblock(fd):
    '''Set a file object to non-blocking'''

    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)


def get_templates_dir():
    return os.path.join(os.path.dirname(__file__), 'dbusmock-templates')

def get_template_path(template_name):
    return os.path.join(get_templates_dir(), template_name + '.py')


class MutterDBusTestCase(DBusTestCase):
    @classmethod
    def setUpClass(klass):
        klass.mocks = OrderedDict()

        DBusTestCase.setUpClass()
        klass.start_session_bus()
        klass.start_system_bus()

        (klass.mocks_manager, klass.mock_obj) = klass.start_from_local_template(
            'meta-mocks-manager', {'templates-dir': get_templates_dir()})

        klass.start_from_template('logind')
        klass.start_from_local_template('localed')

        klass.system_bus_con = klass.get_dbus(system_bus=True)
        klass.session_bus_con = klass.get_dbus(system_bus=False)

        if klass.session_bus_con.name_has_owner('org.gnome.Mutter.DisplayConfig'):
            raise Exception(
                'org.gnome.Mutter.DisplayConfig already has owner on the session bus, bailing')

    @classmethod
    def tearDownClass(klass):
        klass.mock_obj.Cleanup()

        for (mock_server, mock_obj) in reversed(klass.mocks.values()):
            mock_server.terminate()
            mock_server.wait()

        DBusTestCase.tearDownClass()

    @classmethod
    def start_from_template(klass, template, params={}):
        mock_server, mock_obj = \
            klass.spawn_server_template(template,
                                        params,
                                        stdout=subprocess.PIPE)
        set_nonblock(mock_server.stdout)

        mocks = (mock_server, mock_obj)
        assert klass.mocks.setdefault(template, mocks) == mocks
        return mocks

    @classmethod
    def start_from_local_template(klass, template_file_name, params={}):
        template = get_template_path(template_file_name)
        return klass.start_from_template(template, params)

    @classmethod
    def start_from_template_managed(klass, template):
        klass.mock_obj.StartFromTemplate(template)

    @classmethod
    def start_from_local_template_managed(klass, template_file_name):
        template = get_template_path(template_file_name)
        klass.mock_obj.StartFromLocalTemplate(template)

    @classmethod
    def start_from_class(klass, mock_class, params={}):
        mock_server = \
            klass.spawn_server(mock_class.BUS_NAME,
                               mock_class.MAIN_OBJ,
                               mock_class.MAIN_IFACE,
                               mock_class.SYSTEM_BUS,
                               stdout=subprocess.PIPE)
        set_nonblock(mock_server.stdout)

        bus = klass.get_dbus(system_bus=mock_class.SYSTEM_BUS)
        mock_obj = bus.get_object(mock_class.BUS_NAME, mock_class.MAIN_OBJ)
        mock_class.load(mock_obj, params)

        mocks = (mock_server, mock_obj)
        assert klass.mocks.setdefault(mock_class, mocks) == mocks
        return mocks

    def wrap_call(self, args):
        env = {}
        env.update(os.environ)
        env['NO_AT_BRIDGE'] = '1'
        env['GSETTINGS_BACKEND'] = 'memory'

        wrapper = env.get('META_DBUS_RUNNER_WRAPPER')
        if wrapper == 'gdb':
            args = ['gdb', '-ex', 'r', '-ex', 'bt full', '--args'] + args
        elif wrapper:
            args = wrapper.split(' ') + args

        p = subprocess.Popen(args, env=env)
        self.assertEqual(p.wait(), 0)


if __name__ == '__main__':
    MutterDBusTestCase.setUpClass()
    test_case = MutterDBusTestCase()
    test_case.assertGreater(len(sys.argv), 1)
    try:
        test_case.wrap_call(sys.argv[1:])
    finally:
        MutterDBusTestCase.tearDownClass()
