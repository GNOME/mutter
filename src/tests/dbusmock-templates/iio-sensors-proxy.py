'''sensors proxy mock template
'''

# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.

__author__ = 'Marco Trevisan'
__copyright__ = '(c) 2021 Canonical Ltd.'

import re

import dbus
from dbusmock import MOCK_IFACE

BUS_NAME = 'net.hadess.SensorProxy'
MAIN_OBJ = '/net/hadess/SensorProxy'
MAIN_IFACE = 'net.hadess.SensorProxy'
COMPASS_IFACE = 'net.hadess.SensorProxy.Compass'
SYSTEM_BUS = True

CAMEL_TO_SNAKE_CASE_RE = re.compile(r'(?<!^)(?=[A-Z])')


def load(mock, parameters=None):
    mock.has_accelerometer = False
    mock.accelerometer_owners = dict()
    mock.accelerometer_orientation = 'undefined'
    mock.has_ambient_light = False
    mock.ambient_light_owners = dict()
    mock.light_level_unit = 'lux'
    mock.light_level = 0.0
    mock.has_proximity = False
    mock.proximity_near = False
    mock.proximity_owners = dict()
    mock.has_compass = False
    mock.compass_owners = dict()
    mock.compass_heading = -1.0

    if parameters:
        for p, v in parameters.items():
            setattr(mock, p, v)

    for iface in [MAIN_IFACE, COMPASS_IFACE]:
        mock.AddProperties(iface, mock.GetAll(iface))


def emit_signal_to_destination(mock, interface, name, signature, destination, *args):
    # We need to do this manually, could be made easier via
    # https://gitlab.freedesktop.org/dbus/dbus-python/-/merge_requests/13
    message = dbus.lowlevel.SignalMessage(mock.path, interface, name)
    if destination:
        message.set_destination(destination)
    message.append(signature=signature, *args)
    for location in mock.locations:
        location[0].send_message(message)


def emit_properties_changed(mock, interface=MAIN_IFACE, properties=None,
                            destination=None):
    if properties is None:
        properties = mock.GetAll(interface)
    elif isinstance(properties, str):
        properties = [properties]

    if isinstance(properties, (list, set)):
        properties = {p: mock.Get(interface, p) for p in properties}
    elif not isinstance(properties, dict):
        raise TypeError('Unsupported properties type')

    emit_signal_to_destination(mock, dbus.PROPERTIES_IFACE, 'PropertiesChanged',
                               'sa{sv}as', destination, interface, properties, [])


@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='ss',
                     out_signature='v')
def Get(self, interface, prop):
    if interface == MAIN_IFACE:
        if prop == 'HasAccelerometer':
            return dbus.Boolean(self.has_accelerometer)
        if prop == 'AccelerometerOrientation':
            return dbus.String(self.accelerometer_orientation)

    raise TypeError('Tried to get property {} on interface {}'.format(prop, interface))

@dbus.service.method(dbus.PROPERTIES_IFACE, in_signature='s',
                     out_signature='a{sv}')
def GetAll(self, interface):
    if interface == MAIN_IFACE:
        return {
            'HasAccelerometer': dbus.Boolean(self.has_accelerometer),
            'AccelerometerOrientation': dbus.String(self.accelerometer_orientation),
            'HasAmbientLight': dbus.Boolean(self.has_ambient_light),
            'LightLevelUnit': dbus.String(self.light_level_unit),
            'LightLevel': dbus.Double(self.light_level),
            'HasProximity': dbus.Boolean(self.has_proximity),
            'ProximityNear': dbus.Boolean(self.proximity_near),
        }
    if interface == COMPASS_IFACE:
        return {
            'HasCompass': dbus.Boolean(self.has_compass),
            'CompassHeading': dbus.Double(self.compass_heading),
        }
    return dbus.Dictionary({}, signature='sv')


def register_owner(self, owners_dict, name):
    if name in owners_dict:
        owners_dict[name][1] += 1
        return

    def name_cb(unique_name):
        if unique_name:
            return
        owners_dict.pop(name)[0].cancel()

    owners_dict[name] = [self.connection.watch_name_owner(name, name_cb), 1]


def unregister_owner(owners_dict, name):
    owners_dict[name][1] -= 1

    if owners_dict[name][1] == 0:
        watcher = owners_dict.pop(name, None)
        watcher[0].cancel()


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ClaimAccelerometer(self, sender):
    register_owner(self, self.accelerometer_owners, sender)


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ReleaseAccelerometer(self, sender):
    unregister_owner(self.accelerometer_owners, sender)


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ClaimLight(self, sender):
    register_owner(self, self.ambient_light_owners, sender)


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ReleaseLight(self, sender):
    unregister_owner(self.ambient_light_owners, sender)


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ClaimProximity(self, sender):
    register_owner(self, self.proximity_owners, sender)


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ReleaseProximity(self, sender):
    unregister_owner(self.proximity_owners, sender)


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ClaimCompass(self, sender):
    register_owner(self, self.compass_owners, sender)


@dbus.service.method(MAIN_IFACE, sender_keyword='sender')
def ReleaseCompass(self, sender):
    unregister_owner(self.compass_owners, sender)


def sensor_to_attribute(sensor):
    if sensor == 'light':
        return 'ambient_light'
    return sensor


def is_valid_sensor_for_interface(sensor, interface):
    if interface == 'net.hadess.SensorProxy':
        return sensor in ['accelerometer', 'ambient_light', 'proximity']

    if interface == 'net.hadess.SensorProxy.Compass':
        return sensor == 'compass'

    return False


@dbus.service.method(MOCK_IFACE, in_signature='ssv')
def SetInternalProperty(self, interface, property_name, value):
    property_attribute = CAMEL_TO_SNAKE_CASE_RE.sub('_', property_name).lower()
    sensor = sensor_to_attribute(property_attribute.split('_')[0])

    owners = None
    if is_valid_sensor_for_interface(sensor, interface):

        if not getattr(self, 'has_{}'.format(sensor)):
            raise Exception('No {} sensor available'.format(sensor))

        owners = getattr(self, '{}_owners'.format(sensor))
        # We allow setting a property from any client here, even if not claiming
        # but only owners, if any, will be notified about sensors changes

    pre_value = getattr(self, property_attribute)
    if pre_value != value:
        setattr(self, property_attribute, value)
        if owners:
            for owner in owners.keys():
                emit_properties_changed(self, interface, property_name, owner)
        elif owners is None:
            emit_properties_changed(self, interface, property_name, None)


@dbus.service.method(MOCK_IFACE, in_signature='s')
def GetInternalProperty(self, property_name):
    property_attribute = CAMEL_TO_SNAKE_CASE_RE.sub('_', property_name).lower()
    value = getattr(self, property_attribute)

    if property_name.endswith('Owners'):
        return dbus.Array(value.keys(), signature='s')
    return value
