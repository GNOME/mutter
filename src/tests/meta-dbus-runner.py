#!/usr/bin/env python3

import dbus
import sys
import os
import fcntl
import subprocess
import getpass
import argparse
from collections import OrderedDict
from dbusmock import DBusTestCase
from dbus.mainloop.glib import DBusGMainLoop

DBusGMainLoop(set_as_default=True)


def get_templates_dir():
    return os.path.join(os.path.dirname(__file__), 'dbusmock-templates')

def get_template_path(template_name):
    return os.path.join(get_templates_dir(), template_name + '.py')

def get_subprocess_stdout():
    if os.getenv('META_DBUS_RUNNER_VERBOSE') == '1':
        return sys.stderr
    else:
        return subprocess.DEVNULL;


class MutterDBusTestCase(DBusTestCase):
    @classmethod
    def setUpClass(klass, enable_kvm):
        klass.mocks = OrderedDict()

        print('Starting D-Bus daemons (session & system)...', file=sys.stderr)
        DBusTestCase.setUpClass()
        klass.start_session_bus()
        klass.start_system_bus()

        print('Starting mocked services...', file=sys.stderr)
        (klass.mocks_manager, klass.mock_obj) = klass.start_from_local_template(
            'meta-mocks-manager', {'templates-dir': get_templates_dir()})

        klass.start_from_local_template('localed')

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

        DBusTestCase.tearDownClass()

    @classmethod
    def start_from_template(klass, template, params={}):
        mock_server, mock_obj = \
            klass.spawn_server_template(template,
                                        params,
                                        get_subprocess_stdout())

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
                               stdout=get_subprocess_stdout())

        bus = klass.get_dbus(system_bus=mock_class.SYSTEM_BUS)
        mock_obj = bus.get_object(mock_class.BUS_NAME, mock_class.MAIN_OBJ)
        mock_class.load(mock_obj, params)

        mocks = (mock_server, mock_obj)
        assert klass.mocks.setdefault(mock_class, mocks) == mocks
        return mocks

    @classmethod
    def init_logind_kvm(klass, session_path):
        session_obj = klass.system_bus_con.get_object('org.freedesktop.login1', session_path)
        session_obj.AddMethod('org.freedesktop.login1.Session',
                              'TakeDevice',
                              'uu', 'hb',
'''
import re

major = args[0]
minor = args[1]

sysfs_uevent_path = '/sys/dev/char/{}:{}/uevent'.format(major, minor)
sysfs_uevent = open(sysfs_uevent_path, 'r')
devname = None
for line in sysfs_uevent.readlines():
    match = re.match('DEVNAME=(.*)', line)
    if match:
        devname = match[1]
        break
sysfs_uevent.close()
if not devname:
    raise dbus.exceptions.DBusException(f'Device file {major}:{minor} doesn\\\'t exist',
                                        major=major, minor=minor)
fd = os.open('/dev/' + devname, os.O_RDWR | os.O_CLOEXEC)
unix_fd = dbus.types.UnixFd(fd)
os.close(fd)
ret = (unix_fd, False)
''')
        session_obj.AddMethods('org.freedesktop.login1.Session', [
            ('ReleaseDevice', 'uu', '', ''),
            ('TakeControl', 'b', '', ''),
        ])

    @classmethod
    def init_logind(klass, enable_kvm):
        logind = klass.start_from_template('logind')

        [p_mock, obj] = logind

        mock_iface = 'org.freedesktop.DBus.Mock'
        obj.AddSeat('seat0', dbus_interface=mock_iface)
        session_path = obj.AddSession('dummy', 'seat0',
                                      dbus.types.UInt32(os.getuid()),
                                      getpass.getuser(),
                                      True,
                                      dbus_interface=mock_iface)

        if enable_kvm:
            klass.init_logind_kvm(session_path)

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
    parser = argparse.ArgumentParser()
    parser.add_argument('--kvm', action='store_true', default=False)
    (args, rest) = parser.parse_known_args(sys.argv)

    MutterDBusTestCase.setUpClass(args.kvm)
    test_case = MutterDBusTestCase()
    test_case.assertGreater(len(rest), 1)
    try:
        print('Running test case...', file=sys.stderr)
        test_case.wrap_call(rest[1:])
    finally:
        MutterDBusTestCase.tearDownClass()
