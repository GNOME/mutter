'''colord proxy mock template
'''

# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.

__author__ = 'Jonas Ådahl'
__copyright__ = '(c) 2021 Red Hat Inc.'

import dbus
import os
import pwd
from dbusmock import MOCK_IFACE


BUS_PREFIX = 'org.freedesktop.ColorManager'
PATH_PREFIX = '/org/freedesktop/ColorManager'

BUS_NAME = BUS_PREFIX
MAIN_OBJ = PATH_PREFIX
MAIN_IFACE = BUS_NAME
DEVICE_IFACE = BUS_PREFIX + '.Device'
SYSTEM_BUS = True


def load(mock, parameters=None):
    mock.devices = {}

def escape_unit_name(name):
    for s in ['.', '-', '\'', ' ']:
        name = name.replace(s, '_')
    return name

def get_username(uid):
    return pwd.getpwuid(uid).pw_name

def device_id_from_path(mock, path):
    for device_id in mock.devices:
        device_path = mock.devices[device_id]
        if device_path == path:
            return device_id
    return None

@dbus.service.method(MAIN_IFACE, in_signature='ssa{sv}', out_signature='o')
def CreateDevice(self, device_id, scope, props):
    uid = os.getuid()
    username = get_username(uid)
    device_path = PATH_PREFIX + '/devices/' + \
        escape_unit_name(device_id) + \
        '_' + username + '_' + str(uid)
    self.devices[device_id] = device_path
    self.AddObject(device_path,
                   DEVICE_IFACE,
                   {
                     'DeviceId': device_id,
                   },
                   [])
    self.EmitSignal(MAIN_IFACE, 'DeviceAdded', 'o', [device_path])
    return device_path

@dbus.service.method(MAIN_IFACE, in_signature='o')
def DeleteDevice(self, device_path):
    self.RemoveObject(device_path)
    device_id = device_id_from_path(self, device_path)
    del self.devices[device_id]
    self.EmitSignal(MAIN_IFACE, 'DeviceRemoved', 'o', [device_path])


@dbus.service.method(MAIN_IFACE, in_signature='s', out_signature='o')
def FindDeviceById(self, device_id):
    return self.devices[device_id]


@dbus.service.method(MOCK_IFACE)
def ClearDevices(self):
    for device_path in self.devices.values():
        self.RemoveObject(device_path)
    self.devices = {}
