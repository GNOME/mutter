#!/usr/bin/env python3

import dbus
import sys
import os
import fcntl
import subprocess
import getpass
import argparse
import logind_helpers
from collections import OrderedDict
from dbusmock import DBusTestCase
from dbus.mainloop.glib import DBusGMainLoop
from pathlib import Path
from gi.repository import Gio


def escape_object_path(path):
    b = bytearray()
    b.extend(path.encode())
    path = Gio.dbus_escape_object_path_bytestring(b)
    if path[0].isdigit():
        path = "_{0:02x}{1}".format(ord(path[0]), path[1:])
    return os.path.basename(path)

def get_subprocess_stdout():
    if os.getenv('META_DBUS_RUNNER_VERBOSE') == '1':
        return sys.stderr
    else:
        return subprocess.DEVNULL;


class MutterDBusRunner(DBusTestCase):
    @classmethod
    def __get_templates_dir(klass):
            return os.path.join(os.path.dirname(__file__), 'dbusmock-templates')

    @classmethod
    def setUpClass(klass, enable_kvm, launch):
        klass.templates_dirs = [klass.__get_templates_dir()]

        klass.mocks = OrderedDict()

        klass.host_system_bus_address = os.getenv('DBUS_SYSTEM_BUS_ADDRESS')
        if klass.host_system_bus_address is None:
            klass.host_system_bus_address = 'unix:path=/run/dbus/system_bus_socket'

        try:
            dbus.bus.BusConnection(klass.host_system_bus_address)
            klass.has_host_system_bus = True
        except:
            klass.has_host_system_bus = False

        print('Starting D-Bus daemons (session & system)...', file=sys.stderr)
        DBusTestCase.setUpClass()
        klass.start_session_bus()
        klass.start_system_bus()

        print('Launching required services...', file=sys.stderr)
        klass.service_processes = []
        for service in launch:
            klass.service_processes += [subprocess.Popen(service)]

        print('Starting mocked services...', file=sys.stderr)
        (klass.mocks_manager, klass.mock_obj) = klass.start_from_local_template(
            'meta-mocks-manager', {'templates-dir': klass.__get_templates_dir()})

        klass.start_from_local_template('localed')
        klass.start_from_local_template('colord')
        klass.start_from_local_template('gsd-color')
        klass.start_from_local_template('rtkit')
        klass.start_from_local_template('screensaver')

        klass.system_bus_con = klass.get_dbus(system_bus=True)
        klass.session_bus_con = klass.get_dbus(system_bus=False)

        klass.init_logind(enable_kvm)

        if klass.session_bus_con.name_has_owner('org.gnome.Mutter.DisplayConfig'):
            raise Exception(
                'org.gnome.Mutter.DisplayConfig already has owner on the session bus, bailing')

    @classmethod
    def tearDownClass(klass):
        klass.mock_obj.Cleanup()

        for (mock_server, mock_obj) in reversed(klass.mocks.values()):
            mock_server.terminate()
            mock_server.wait()

        for process in klass.service_processes:
            process.terminate()
            process.wait()

        DBusTestCase.tearDownClass()

    @classmethod
    def start_from_template(klass, template, params={}, system_bus=None):
        mock_server, mock_obj = \
            klass.spawn_server_template(template,
                                        params,
                                        get_subprocess_stdout(),
                                        system_bus=system_bus)

        mocks = (mock_server, mock_obj)
        return mocks

    @classmethod
    def start_from_local_template(klass, template_file_name, params={}, system_bus=None):
        template = klass.find_template(template_file_name)
        return klass.start_from_template(template, params, system_bus=system_bus)

    @classmethod
    def start_from_template_managed(klass, template):
        klass.mock_obj.StartFromTemplate(template)

    @classmethod
    def start_from_local_template_managed(klass, template_file_name):
        template = klass.find_template(template_file_name)
        klass.mock_obj.StartFromLocalTemplate(template)

    @classmethod
    def start_from_class(klass, mock_class, params={}):
        mock_server = \
            klass.spawn_server(mock_class.BUS_NAME,
                               mock_class.MAIN_OBJ,
                               mock_class.MAIN_IFACE,
                               mock_class.SYSTEM_BUS,
                               stdout=get_subprocess_stdout())

        bus = klass.get_dbus(system_bus=mock_class.SYSTEM_BUS)
        mock_obj = bus.get_object(mock_class.BUS_NAME, mock_class.MAIN_OBJ)
        mock_class.load(mock_obj, params)

        mocks = (mock_server, mock_obj)
        return mocks

    def wrap_logind_call(call):
        code = \
f'''
import os
import sys

sys.path.insert(0, '{os.path.dirname(__file__)}')
import logind_helpers

{call}
'''
        return code

    @classmethod
    def forward_to_host(klass, object_path, interface, method, in_type, out_type):
        proxy = klass.system_bus_con.get_object('org.freedesktop.login1',
                                                object_path)
        proxy.AddMethod(interface, method, in_type, out_type,
f'''
import os
import sys

sys.path.insert(0, '{os.path.dirname(__file__)}')
import logind_helpers

ret = logind_helpers.call_host('{klass.host_system_bus_address}',
                               '{object_path}',
                               '{interface}',
                               '{method}',
                               '{in_type}',
                               args)
''')

    @classmethod
    def init_logind_forward(klass, session_path, seat_path):
        klass.forward_to_host(session_path, 'org.freedesktop.login1.Session',
                              'TakeDevice',
                              'uu', 'hb')
        klass.forward_to_host(session_path, 'org.freedesktop.login1.Session',
                              'ReleaseDevice',
                              'uu', '')
        klass.forward_to_host(session_path, 'org.freedesktop.login1.Session',
                              'TakeDevice',
                              'uu', 'hb')
        klass.forward_to_host(session_path, 'org.freedesktop.login1.Session',
                              'TakeControl',
                              'b', '')
        klass.forward_to_host(seat_path, 'org.freedesktop.login1.Seat',
                              'SwitchTo',
                              'u', '')

    @classmethod
    def init_logind_kvm(klass, session_path):
        session_obj = klass.system_bus_con.get_object('org.freedesktop.login1', session_path)
        session_obj.AddMethod('org.freedesktop.login1.Session',
                              'TakeDevice',
                              'uu', 'hb',
                              klass.wrap_logind_call(
f'''
major = args[0]
minor = args[1]
ret = logind_helpers.open_file_direct(major, minor)
'''))
        session_obj.AddMethods('org.freedesktop.login1.Session', [
            ('ReleaseDevice', 'uu', '', ''),
            ('TakeControl', 'b', '', ''),
        ])


    @classmethod
    def find_host_session_name(klass):
        if 'XDG_SESSION_ID' in os.environ:
            return escape_object_path(os.environ['XDG_SESSION_ID'])

        bus = dbus.bus.BusConnection(klass.host_system_bus_address)
        session_auto_proxy = bus.get_object('org.freedesktop.login1',
                                            '/org/freedesktop/login1/session/auto')
        props = dbus.Interface(session_auto_proxy,
                               dbus_interface='org.freedesktop.DBus.Properties')
        session_id = props.Get('org.freedesktop.login1.Session', 'Id')
        manager_proxy = bus.get_object('org.freedesktop.login1',
                                       '/org/freedesktop/login1')
        manager = dbus.Interface(manager_proxy,
                                 dbus_interface='org.freedesktop.login1.Manager')
        session_path = manager.GetSession(session_id)
        return os.path.basename(session_path)

    @classmethod
    def init_logind(klass, enable_kvm):
        logind = klass.start_from_template('logind')

        [p_mock, obj] = logind

        mock_iface = 'org.freedesktop.DBus.Mock'
        seat_path = obj.AddSeat('seat0', dbus_interface=mock_iface)
        session_name = 'dummy'
        if klass.has_host_system_bus:
            session_name = klass.find_host_session_name()

        session_path = obj.AddSession(session_name, 'seat0',
                                      dbus.types.UInt32(os.getuid()),
                                      getpass.getuser(),
                                      True,
                                      dbus_interface=mock_iface)

        if enable_kvm:
            klass.init_logind_kvm(session_path)
        elif klass.has_host_system_bus:
            klass.init_logind_forward(session_path, seat_path)

    @classmethod
    def add_template_dir(klass, templates_dir):
        klass.templates_dirs += [templates_dir]

    @classmethod
    def find_template(klass, template_name):
        for templates_dir in klass.templates_dirs:
            template_path = os.path.join(templates_dir, template_name + '.py')
            template_file = Path(template_path)
            if template_file.is_file():
                return template_path
        raise FileNotFoundError(f'Couldnt find a {template_name} template')


def wrap_call(args, wrapper):
    env = {}
    env.update(os.environ)
    env['NO_AT_BRIDGE'] = '1'
    env['GTK_A11Y'] = 'none'
    env['GSETTINGS_BACKEND'] = 'memory'
    env['META_DBUS_RUNNER_ACTIVE'] = '1'

    if wrapper == 'gdb':
        args = ['gdb', '-ex', 'r', '-ex', 'bt full', '--args'] + args
    elif wrapper:
        args = wrapper.split(' ') + args

    p = subprocess.Popen(args, env=env)
    return p.wait()


def meta_run(klass):
    DBusGMainLoop(set_as_default=True)

    parser = argparse.ArgumentParser()
    parser.add_argument('--kvm', action='store_true', default=False)
    parser.add_argument('--launch', action='append', default=[])
    (args, rest) = parser.parse_known_args(sys.argv)

    rest.pop(0)
    if rest[0] == '--':
      rest.pop(0)

    result = 1

    if os.getenv('META_DBUS_RUNNER_ACTIVE') == None:
        klass.setUpClass(args.kvm, args.launch)
        runner = klass()
        runner.assertGreater(len(rest), 0)
        wrapper = os.getenv('META_DBUS_RUNNER_WRAPPER')

        try:
            print('Running test case...', file=sys.stderr)
            result = wrap_call(rest, wrapper)
        finally:
            MutterDBusRunner.tearDownClass()
    else:
        try:
            print(('Inside a nested meta-dbus-runner: '
                   'Not re-creating mocked environment.'),
                  file=sys.stderr)
            print('Running test case...', file=sys.stderr)
            result = wrap_call(rest, None)
        finally:
            pass

    return result
