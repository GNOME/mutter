# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.

__author__ = 'Marco Trevisan'
__copyright__ = '(c) 2021 Canonical Ltd.'

import dbus
import time

BUS_NAME = 'org.freedesktop.locale1'
MAIN_OBJ = '/org/freedesktop/locale1'
MAIN_IFACE = 'org.freedesktop.locale1'
SYSTEM_BUS = True


def load(mock, parameters):
    mock.AddProperty(MAIN_IFACE, 'Locale', dbus.Array(['LANG=C'], signature='s'))
    mock.AddProperty(MAIN_IFACE, 'X11Layout', dbus.String())
    mock.AddProperty(MAIN_IFACE, 'X11Model', dbus.String())
    mock.AddProperty(MAIN_IFACE, 'X11Variant', dbus.String())
    mock.AddProperty(MAIN_IFACE, 'X11Options', dbus.String())
    mock.AddProperty(MAIN_IFACE, 'VConsoleKeymap', dbus.String())
    mock.AddProperty(MAIN_IFACE, 'VConsoleKeymapToggle', dbus.String())

def simulate_interaction():
    time.sleep(1)

@dbus.service.method(MAIN_IFACE, in_signature='asb')
def SetLocale(self, locale, interactive):
    if interactive:
        simulate_interaction()
    self.Set(MAIN_IFACE, 'Locale', locale)


@dbus.service.method(MAIN_IFACE, in_signature='ssbb')
def SetVConsoleKeyboard(self, keymap, keymap_toggle, convert, interactive):
    if interactive:
        simulate_interaction()
    self.Set(MAIN_IFACE, 'VConsoleKeymap', keymap)
    self.Set(MAIN_IFACE, 'VConsoleKeymapToggle', keymap_toggle)


@dbus.service.method(MAIN_IFACE, in_signature='ssssbb')
def SetVConsoleKeyboard(self, layout, model, variant, options, convert, interactive):
    if interactive:
        simulate_interaction()
    self.Set(MAIN_IFACE, 'X11Layout', layout)
    self.Set(MAIN_IFACE, 'X11Model', model)
    self.Set(MAIN_IFACE, 'X11Variant', variant)
    self.Set(MAIN_IFACE, 'X11Options', options)
