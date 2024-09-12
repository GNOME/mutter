#!/usr/bin/env python3

import argparse
import dbus

NAME = 'org.gnome.Mutter.DebugControl'
INTERFACE = 'org.gnome.Mutter.DebugControl'
OBJECT_PATH = '/org/gnome/Mutter/DebugControl'

PROPS_IFACE = 'org.freedesktop.DBus.Properties'

def bool_to_string(value):
    if value:
        return "true"
    else:
        return "false"

def string_to_bool(value):
    if value == "true":
        return True
    if value == "false":
        return False
    raise BaseException(f"bad boolean value: {value}")

def value_to_string(value):
    if isinstance(value, dbus.Boolean):
        return bool_to_string(value)
    return f"{value}"

def string_to_value(current, value):
    if isinstance(current, dbus.Boolean):
        return dbus.Boolean(string_to_bool(value), variant_level=1)
    if isinstance(current, dbus.UInt32):
        return dbus.UInt32(int(value), variant_level=1)
    return value

def get_debug_control():
    bus = dbus.SessionBus()
    try:
        debug_control = bus.get_object(NAME, OBJECT_PATH)
    except dbus.exceptions.DBusException:
        print("The DebugControl service is not available.")
        print("You may have to enable the `debug-control` flag in looking glass.")
        exit(-1)
    return debug_control

def status():
    debug_control = get_debug_control()
    props = debug_control.GetAll(INTERFACE, dbus_interface=PROPS_IFACE)
    for prop, value in props.items():
        value = value_to_string (value)
        print(f"{prop}: {value}")

def enable(prop):
    debug_control = get_debug_control()
    debug_control.Set(INTERFACE, prop, dbus.Boolean(True, variant_level=1),
                      dbus_interface=PROPS_IFACE)

def disable(prop):
    debug_control = get_debug_control()
    debug_control.Set(INTERFACE, prop, dbus.Boolean(False, variant_level=1),
                      dbus_interface=PROPS_IFACE)

def toggle(prop):
    debug_control = get_debug_control()

    value = debug_control.Get(INTERFACE, prop, dbus_interface=PROPS_IFACE)
    debug_control.Set(INTERFACE, prop, dbus.Boolean(not value, variant_level=1),
                      dbus_interface=PROPS_IFACE)

def set_value(kv):
    debug_control = get_debug_control()
    [prop, value] = kv

    current = debug_control.Get(INTERFACE, prop, dbus_interface=PROPS_IFACE)
    value = string_to_value (current, value)

    debug_control.Set(INTERFACE, prop, value, dbus_interface=PROPS_IFACE)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Get and set debug state')

    parser.add_argument('--status', action='store_true')
    parser.add_argument('--enable', metavar='PROPERTY', type=str, nargs='?')
    parser.add_argument('--disable', metavar='PROPERTY', type=str, nargs='?')
    parser.add_argument('--toggle', metavar='PROPERTY', type=str, nargs='?')
    parser.add_argument('--set', metavar=('PROPERTY', 'VALUE'), type=str, nargs=2)

    args = parser.parse_args()
    if args.status:
        status()
    elif args.enable:
        enable(args.enable)
    elif args.disable:
        disable(args.disable)
    elif args.toggle:
        toggle(args.toggle)
    elif args.set:
        set_value(args.set)
    else:
        parser.print_usage()
