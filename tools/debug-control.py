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

def get_debug_control():
    bus = dbus.SessionBus()
    return bus.get_object(NAME, OBJECT_PATH)

def status():
    debug_control = get_debug_control()
    props = debug_control.GetAll(INTERFACE, dbus_interface=PROPS_IFACE)
    for prop in props:
        print(f"{prop}: {bool_to_string(props[prop])}")

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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Get and set debug state')

    parser.add_argument('--status', action='store_true')
    parser.add_argument('--enable', metavar='PROPERTY', type=str, nargs='?')
    parser.add_argument('--disable', metavar='PROPERTY', type=str, nargs='?')
    parser.add_argument('--toggle', metavar='PROPERTY', type=str, nargs='?')

    args = parser.parse_args()
    if args.status:
        status()
    elif args.enable:
        enable(args.enable)
    elif args.disable:
        disable(args.disable)
    elif args.toggle:
        toggle(args.toggle)
    else:
        parser.print_usage()
