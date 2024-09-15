'''logind proxy mock template
'''

# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.

__author__ = 'Sebastian Wick'
__copyright__ = '(c) 2024 Red Hat Inc.'

import os
import re
from pathlib import Path

import dbus
from gi.repository import GLib
from gi.repository import Gio

from dbusmock import MOCK_IFACE, mockobject

BUS_NAME = 'org.freedesktop.login1'
MAIN_OBJ = '/org/freedesktop/login1'
MAIN_IFACE = 'org.freedesktop.login1.Manager'
MANAGER_IFACE = MAIN_IFACE
SEAT_IFACE = 'org.freedesktop.login1.Seat'
SESSION_IFACE = 'org.freedesktop.login1.Session'
PROP_IFACE = 'org.freedesktop.DBus.Properties'
SYSTEM_BUS = True


def escape_object_path(path):
    b = bytearray()
    b.extend(path.encode())
    path = Gio.dbus_escape_object_path_bytestring(b)
    if path[0].isdigit():
        path = '_{0:02x}{1}'.format(ord(path[0]), path[1:])
    return os.path.basename(path)

def find_host_session_id(bus):
    if 'XDG_SESSION_ID' in os.environ:
        return escape_object_path(os.environ['XDG_SESSION_ID'])

    auto_session = bus.get_object(BUS_NAME, f'{MAIN_OBJ}/session/auto')
    session_id = auto_session.Get(SESSION_IFACE, 'Id', dbus_interface=PROP_IFACE)

    manager = bus.get_object(BUS_NAME, MAIN_OBJ)
    session_path = manager.GetSession(session_id, dbus_interface=MANAGER_IFACE)

    return os.path.basename(session_path)

def find_host_seat_id(bus, session_id):
    session = bus.get_object(BUS_NAME, f'{MAIN_OBJ}/session/{session_id}')
    (seat_id, _) = session.Get(SESSION_IFACE, 'Seat', dbus_interface=PROP_IFACE)
    return seat_id


class Login1Seat(mockobject.DBusMockObject):
    def __init__(self, *args, **kwargs):
        super(Login1Seat, self).__init__(*args, **kwargs)

        bus = kwargs.get('mock_data')
        self.host_seat = bus.get_object(self.bus_name.get_name(), self.path) if bus else None

    @staticmethod
    def add_new(manager, seat_id, host_bus):
        seat_path = f'{MAIN_OBJ}/seat/{seat_id}'
        if not seat_path in mockobject.objects:
            manager.AddObject(seat_path, SEAT_IFACE,
                              {
                                'Id': seat_id,
                              },
                              [],
                              mock_class=Login1Seat,
                              mock_data=host_bus)

    @dbus.service.method(SEAT_IFACE, in_signature='u', out_signature='')
    def SwitchTo(self, n):
        if self.host_seat:
            return self.host_seat.SwitchTo(n, dbus_interface=SEAT_IFACE)
        # noop


class Login1Session(mockobject.DBusMockObject):
    def __init__(self, *args, **kwargs):
        super(Login1Session, self).__init__(*args, **kwargs)

        bus = kwargs.get('mock_data')
        self.host_session = bus.get_object(self.bus_name.get_name(), self.path) if bus else None

        self.backlights = {}

    @staticmethod
    def add_new(manager, session_id, seat_id, host_bus):
        session_path = f'{MAIN_OBJ}/session/{session_id}'
        seat_path = f'{MAIN_OBJ}/seat/{seat_id}'
        if not session_path in mockobject.objects:
            manager.AddObject(session_path, SESSION_IFACE,
                              {
                                'Id': session_id,
                                'Active': True,
                                'Seat': (seat_id, dbus.ObjectPath(seat_path)),
                              },
                              [],
                              mock_class=Login1Session,
                              mock_data=host_bus)

    def open_file_direct(self, major, minor):
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
        fd = os.open('/dev/' + devname, os.O_RDWR | os.O_CLOEXEC | os.O_NONBLOCK)
        unix_fd = dbus.types.UnixFd(fd)
        os.close(fd)
        return (unix_fd, False)

    @dbus.service.method(SESSION_IFACE, in_signature='uu', out_signature='hb')
    def TakeDevice(self, major, minor):
        if self.host_session:
            return self.host_session.TakeDevice(major, minor,
                                                dbus_interface=SESSION_IFACE)
        return self.open_file_direct (major, minor)

    @dbus.service.method(SESSION_IFACE, in_signature='uu', out_signature='')
    def ReleaseDevice(self, major, minor):
        if self.host_session:
            return self.host_session.ReleaseDevice(major, minor,
                                                   dbus_interface=SESSION_IFACE)
        # noop

    @dbus.service.method(SESSION_IFACE, in_signature='b', out_signature='')
    def TakeControl(self, force):
        if self.host_session:
            return self.host_session.TakeControl(force, dbus_interface=SESSION_IFACE)

    @dbus.service.method(SESSION_IFACE, in_signature='', out_signature='')
    def ReleaseControl(self):
        if self.host_session:
            return self.host_session.ReleaseControl(dbus_interface=SESSION_IFACE)
        # noop

    @dbus.service.method(SESSION_IFACE, in_signature='ssu', out_signature='')
    def SetBrightness(self, subsystem, name, brightness):
        self.backlights[subsystem][name] = brightness
        pass

    @dbus.service.method(MOCK_IFACE, in_signature='ssu', out_signature='')
    def CreateBacklight(self, subsystem, name, brightness):
        if not subsystem in self.backlights:
            self.backlights[subsystem] = {}

        self.backlights[subsystem][name] = brightness

    @dbus.service.method(MOCK_IFACE, in_signature='ss', out_signature='')
    def DestroyBacklight(self, subsystem, name):
        if subsystem in self.backlights and name in self.backlights[subsystem]:
            del self.backlights[subsystem][name]

    @dbus.service.method(MOCK_IFACE, in_signature='ss', out_signature='u')
    def GetBacklight(self, subsystem, name):
        return self.backlights[subsystem][name]


@dbus.service.method(MANAGER_IFACE, in_signature='u', out_signature='o')
def GetUser(self, uid):
    user_path = f'{MAIN_OBJ}/user/_{uid}'
    return user_path

@dbus.service.method(MANAGER_IFACE, in_signature='u', out_signature='o')
def GetSessionByPID(self, pid):
    session_path = f'{MAIN_OBJ}/session/{self.preferred_session_id}'
    return session_path

@dbus.service.method(MANAGER_IFACE, in_signature='ssss', out_signature='h')
def Inhibit(self, what, who, why, mode):
    # Return an arbitrary FD
    return os.open('/dev/null', os.O_RDONLY)

def create_session(self, host_bus):
    session_id = None
    seat_id = None

    if host_bus:
        session_id = find_host_session_id(host_bus)
        seat_id = find_host_seat_id(host_bus, session_id)

    if not seat_id:
        session_id = 'dummy'
        seat_id = 'seat0'

    if not self.preferred_session_id or host_bus:
        self.preferred_session_id = session_id

    Login1Seat.add_new(self, seat_id, host_bus)
    Login1Session.add_new(self, session_id, seat_id, host_bus)

def load(manager, parameters):
    try:
        bus_address = parameters['host_system_bus_address']
        host_bus = dbus.bus.BusConnection(bus_address)
    except:
        host_bus = None

    manager.preferred_session_id = None

    # Try to create a passthrough session
    create_session(manager, host_bus)
