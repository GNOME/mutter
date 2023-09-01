'''org.freedesktop.Screensaver proxy mock template
'''

# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.

__author__ = 'Jonas Ådahl'
__copyright__ = '(c) 2023 Red Hat Inc.'

import dbus
import os
import random
from dbusmock import MOCK_IFACE


BUS_NAME = 'org.freedesktop.Screensaver'
MAIN_OBJ = '/org/freedesktop/Screensaver'
MAIN_IFACE = BUS_NAME
SYSTEM_BUS = False


def load(mock, parameters=None):
    pass


@dbus.service.method(MAIN_IFACE, in_signature='ss', out_signature='u')
def Inhibit(self, application_name, reason):
    return random.randint(0, 10000)

@dbus.service.method(MAIN_IFACE, in_signature='u')
def Uninhibit(self, cookie):
    pass
