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
from dbusmock import MOCK_IFACE, mockobject


BUS_PREFIX = 'org.freedesktop.ColorManager'
PATH_PREFIX = '/org/freedesktop/ColorManager'

BUS_NAME = BUS_PREFIX
MAIN_OBJ = PATH_PREFIX
MAIN_IFACE = BUS_NAME
DEVICE_IFACE = BUS_PREFIX + '.Device'
PROFILE_IFACE = BUS_PREFIX + '.Profile'
SYSTEM_BUS = True


def load(mock, parameters=None):
    mock.devices = {}
    mock.profiles = {}

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

def profile_id_from_path(mock, path):
  for profile_id in mock.profiles:
     profile_path = mock.profiles[profile_id]
     if profile_path == path:
         return profile_id
  return None


class ColordAlreadyExistsException(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.ColorManager.AlreadyExists'

class ColordNotFoundException(dbus.DBusException):
    _dbus_error_name = 'org.freedesktop.ColorManager.NotFound'


@dbus.service.method(MAIN_IFACE, in_signature='ssa{sv}', out_signature='o')
def CreateDevice(self, device_id, scope, props):
    uid = os.getuid()
    username = get_username(uid)
    device_path = PATH_PREFIX + '/devices/' + \
        escape_unit_name(device_id) + \
        '_' + escape_unit_name(username) + '_' + str(uid)
    self.devices[device_id] = device_path
    self.AddObject(device_path,
                   DEVICE_IFACE,
                   {
                     'DeviceId': device_id,
                     'Profiles': dbus.types.Array(signature='o'),
                     'Enabled': True,
                     'ProfilingInhibitors': dbus.types.Array(signature='s'),
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
    try:
        return self.devices[device_id]
    except KeyError:
        raise dbus.exceptions.DBusException(
            f"Device {device_id} not found",
            name="org.freedesktop.ColorManager.NotFound"
        )


@dbus.service.method(MAIN_IFACE, in_signature='ssha{sv}', out_signature='o')
def CreateProfileWithFd(self, profile_id, scope, handle, props):
    uid = os.getuid()
    username = get_username(uid)
    profile_path = PATH_PREFIX + '/profiles/' + \
        escape_unit_name(profile_id) + \
        '_' + escape_unit_name(username) + '_' + str(uid)

    if profile_id in self.profiles:
        raise ColordAlreadyExistsException()

    self.profiles[profile_id] = profile_path
    self.AddObject(profile_path,
                   PROFILE_IFACE,
                   {
                     'ProfileId': profile_id,
                     'Enabled': True,
                     'Filename': props['Filename'],
                   },
                   [])
    self.EmitSignal(MAIN_IFACE, 'ProfileAdded', 'o', [profile_path])
    return profile_path

@dbus.service.method(MAIN_IFACE, in_signature='o')
def DeleteProfile(self, profile_path):
    self.RemoveObject(profile_path)
    profile_id = profile_id_from_path(self, profile_path)
    del self.profiles[profile_id]
    self.EmitSignal(MAIN_IFACE, 'ProfileRemoved', 'o', [profile_path])


@dbus.service.method(MAIN_IFACE, in_signature='s', out_signature='o')
def FindProfileById(self, profile_id):
    if profile_id in self.devices:
        return self.devices[profile_id]
    else:
        raise ColordNotFoundException()


@dbus.service.method(MOCK_IFACE)
def Reset(self):
    for device_path in self.devices.values():
        self.RemoveObject(device_path)
    self.devices = {}
    for profile_path in self.profiles.values():
        self.RemoveObject(profile_path)
    self.profiles = {}

@dbus.service.method(MOCK_IFACE, in_signature='ss')
def AddSystemProfile(self, profile_id, file_path):
    profile_path = PATH_PREFIX + '/profiles/' + \
        escape_unit_name(profile_id)
    self.profiles[profile_id] = profile_path
    self.AddObject(profile_path,
                   PROFILE_IFACE,
                   {
                     'ProfileId': profile_id,
                     'Filename': file_path,
                     'Enabled': True,
                   },
                   [])
    self.EmitSignal(MAIN_IFACE, 'ProfileAdded', 'o', [profile_path])

@dbus.service.method(MOCK_IFACE, in_signature='sas')
def SetDeviceProfiles(self, device_id, profile_ids):
    device_path = self.devices[device_id]
    device = mockobject.objects[device_path]
    profile_paths = [
        dbus.types.ObjectPath(self.profiles[profile_id])
        for profile_id in profile_ids
    ]
    device.UpdateProperties(DEVICE_IFACE, {'Profiles': dbus.types.Array(profile_paths)})
    device.EmitSignal(DEVICE_IFACE, 'Changed', '', [])
