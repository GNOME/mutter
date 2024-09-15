#!/usr/bin/env python3

import dbus
import sys
import os
import fcntl
import subprocess
import getpass
import argparse
import tempfile
import select
import socket
import threading
import configparser
from collections import OrderedDict
from dbusmock import DBusTestCase
from dbus.mainloop.glib import DBusGMainLoop
from pathlib import Path
from gi.repository import Gio


class MultiOrderedDict(OrderedDict):
    def __setitem__(self, key, value):
        if isinstance(value, list) and key in self:
            self[key].extend(value)
        else:
            super(OrderedDict, self).__setitem__(key, value)

def get_subprocess_stdout():
    if os.getenv('META_DBUS_RUNNER_VERBOSE') == '1':
        return sys.stderr
    else:
        return subprocess.DEVNULL

def generate_file_name(args):
    args_string = '_'.join(args)
    args_string = args_string.replace('/', '_')
    return 'session-{}-service-{}.log'.format(os.getpid(), args_string)

class MutterDBusRunner(DBusTestCase):
    @classmethod
    def __get_templates_dir(klass):
            return os.path.join(os.path.dirname(__file__), 'dbusmock-templates')

    @classmethod
    def setUpClass(klass, launch=[], bind_sockets=False):
        klass.templates_dirs = [klass.__get_templates_dir()]

        klass.mocks = OrderedDict()

        host_system_bus_address = os.getenv('DBUS_SYSTEM_BUS_ADDRESS')
        if host_system_bus_address is None:
            host_system_bus_address = 'unix:path=/run/dbus/system_bus_socket'

        disable_passthrough = os.getenv('META_DBUS_RUNNER_DISABLE_LOGIND_PASSTHROUGH')
        if disable_passthrough:
            host_system_bus_address = ''

        print('Starting D-Bus daemons (session & system)...', file=sys.stderr)
        DBusTestCase.setUpClass()
        klass.start_session_bus()
        klass.start_system_bus()

        klass.sockets = []
        klass.poll_thread = None
        if bind_sockets:
            klass.enable_pipewire_sockets()

        print('Launching required services...', file=sys.stderr)
        klass.service_processes = []
        for service in launch:
            klass.launch_service([service])

        print('Starting mocked services...', file=sys.stderr)
        (klass.mocks_manager, klass.mock_obj) = klass.start_from_local_template(
            'meta-mocks-manager', {'templates-dir': klass.__get_templates_dir()})

        klass.start_from_local_template('localed')
        klass.start_from_local_template('colord')
        klass.start_from_local_template('gsd-color')
        klass.start_from_local_template('rtkit')
        klass.start_from_local_template('screensaver')
        klass.start_from_local_template('logind', {
            'host_system_bus_address': host_system_bus_address,
        })

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

        print('Closing PipeWire socket...', file=sys.stderr)
        klass.disable_pipewire_sockets()

        print('Terminating services...', file=sys.stderr)
        for process in klass.service_processes:
            print('  - Terminating {}'.format(' '.join(process.args)), file=sys.stderr)
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

    @classmethod
    def launch_service(klass, args, env=None, pass_fds=()):
        if 'MUTTER_TEST_LOG_DIR' in os.environ:
            service_log_dir = os.environ['MUTTER_TEST_LOG_DIR']
        else:
            service_log_dir = '/tmp'
        service_log_path = os.path.join(service_log_dir, generate_file_name(args))
        print('  - Launching {} (log file: {})'.format(' '.join(args),
                                                       service_log_path),
              file=sys.stderr)

        service_log = open(service_log_path, 'w')
        klass.service_processes += [subprocess.Popen(args,
                                                     env=env,
                                                     pass_fds=pass_fds,
                                                     stdout=service_log,
                                                     stderr=service_log)]
        service_log.close()

    @classmethod
    def poll_pipewire_sockets_in_thread(klass, sockets):
        poller = select.poll()
        for socket in sockets:
            poller.register(socket.fileno(), select.POLLIN | select.POLLHUP)

        should_spawn = False
        should_poll = True
        while should_poll:
            results = poller.poll()
            for result in results:
                if result[1] == select.POLLIN:
                    should_spawn = True
                    should_poll = False
                else:
                    should_poll = False

        if not should_spawn:
            return

        print("Noticed activity on a PipeWire socket, launching services...", file=sys.stderr)

        pipewire_env = os.environ
        pipewire_env['LISTEN_FDS'] = f'{len(sockets)}'
        pipewire_fds = {}
        subprocess_fd = 3
        for sock in sockets:
            pipewire_fds[subprocess_fd] = sock.fileno()
            subprocess_fd += 1

        socket_launch = os.path.join(os.path.dirname(__file__), 'socket-launch.sh')
        klass.launch_service([socket_launch, 'pipewire'],
                             env=pipewire_env,
                             pass_fds=pipewire_fds)
        klass.launch_service(['wireplumber'])

    @classmethod
    def get_pipewire_socket_names(klass):
        pipewire_socket_unit = '/usr/lib/systemd/user/pipewire.socket'
        if os.path.exists(pipewire_socket_unit):
            config = configparser.ConfigParser(strict=False,
                                               empty_lines_in_values=False,
                                               dict_type=MultiOrderedDict,
                                               interpolation=None)
            config.read([pipewire_socket_unit])
            socket_names = config.get('Socket', 'ListenStream')
        else:
            # The pipewire.socket file was not found, use the default
            # value used by pipewire as a fallback (c.f. 'man
            # pipewire.conf'):
            socket_names = ['%t/pipewire-0']
        runtime_dir = os.environ['XDG_RUNTIME_DIR']
        return [socket_name.replace('%t', runtime_dir)
                for socket_name in socket_names]

    @classmethod
    def enable_pipewire_sockets(klass):
        runtime_dir = os.environ['XDG_RUNTIME_DIR']

        sockets = []
        for socket_name in klass.get_pipewire_socket_names():
            sock = socket.socket(socket.AF_UNIX)
            print("Binding {} for socket activation".format(socket_name), file=sys.stderr)
            sock.bind(socket_name)
            sock.listen()
            sockets.append(sock)

        poll_closure = lambda: klass.poll_pipewire_sockets_in_thread(sockets)

        klass.poll_thread = threading.Thread(target=poll_closure)
        klass.poll_thread.start()

        klass.sockets = sockets

    @classmethod
    def disable_pipewire_sockets(klass):
        for sock in klass.sockets:
            sock.shutdown(socket.SHUT_RDWR)
            sock.close()

        if klass.poll_thread:
            klass.poll_thread.join()


def run_test(args, extra_env):
    print('Running test case...', file=sys.stderr)

    env = {}
    env.update(os.environ)
    env['NO_AT_BRIDGE'] = '1'
    env['GTK_A11Y'] = 'none'
    env['GSETTINGS_BACKEND'] = 'memory'
    env['XDG_CURRENT_DESKTOP'] = ''
    env['META_DBUS_RUNNER_ACTIVE'] = '1'
    env.pop('XDG_SESSION_ID', None)

    if extra_env:
        env |= extra_env

    wrapper = os.getenv('META_DBUS_RUNNER_WRAPPER')

    if not os.getenv('META_DBUS_RUNNER_DISABLE_UMOCKDEV'):
        args = ['umockdev-wrapper'] + args

    if wrapper == 'gdb':
        args = ['gdb', '-ex', 'r', '-ex', 'bt full', '--args'] + args
    elif wrapper == 'rr':
        args = ['rr', 'record'] + args
    elif wrapper:
        args = wrapper.split(' ') + args

    p = subprocess.Popen(args, env=env)
    print('Process', args, 'started with pid', p.pid, file=sys.stderr)
    return p.wait()


def run_test_mocked(klass, rest, launch=[], bind_sockets=False, extra_env=None):
    result = 1

    klass.setUpClass(launch=launch,
                     bind_sockets=bind_sockets)
    runner = klass()
    runner.assertGreater(len(rest), 0)

    try:
        result = run_test(rest, extra_env)
    finally:
        MutterDBusRunner.tearDownClass()

    return result


def meta_run(klass, extra_env=None, setup_argparse=None, handle_argparse=None):
    DBusGMainLoop(set_as_default=True)

    parser = argparse.ArgumentParser()
    parser.add_argument('--launch', action='append', default=[])
    parser.add_argument('--no-isolate-dirs', action='store_true', default=False)
    if setup_argparse:
        setup_argparse(parser)
    (args, rest) = parser.parse_known_args(sys.argv)

    if handle_argparse:
        handle_argparse(args)

    rest.pop(0)
    if not rest:
        parser.error('Command or separator `--` not found')
    if rest[0] == '--':
        rest.pop(0)
    else:
        print('WARNING: Command or separator `--` not found', file=sys.stderr)

    if os.getenv('META_DBUS_RUNNER_ACTIVE') != None:
        print('Not re-creating mocked environment...', file=sys.stderr)
        return run_test(rest, extra_env)

    with tempfile.TemporaryDirectory(prefix='mutter-testroot-',
                                     ignore_cleanup_errors=True) as test_root:
        env_dirs = [] if args.no_isolate_dirs else [
            'HOME',
            'TMPDIR',
            'XDG_CACHE_HOME',
            'XDG_CONFIG_HOME',
            'XDG_DATA_HOME',
            'XDG_RUNTIME_DIR',
        ]
        for env_dir in env_dirs:
            directory = os.path.join(test_root, env_dir.lower())
            os.mkdir(directory, mode=0o700)
            os.environ[env_dir] = directory
            print('Setup', env_dir, 'as', directory, file=sys.stderr)

        return run_test_mocked(klass, rest,
                               launch=args.launch,
                               bind_sockets=True,
                               extra_env=extra_env)
