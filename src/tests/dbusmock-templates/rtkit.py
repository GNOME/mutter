# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.

__author__ = 'Jonas Ådahl'
__copyright__ = '(c) 2022 Red Hat Inc.'

import dbus
from dbusmock import MOCK_IFACE, mockobject

BUS_NAME = 'org.freedesktop.RealtimeKit1'
MAIN_OBJ = '/org/freedesktop/RealtimeKit1'
MAIN_IFACE = 'org.freedesktop.RealtimeKit1'
SYSTEM_BUS = True


def load(mock, parameters):
    mock.AddProperty(MAIN_IFACE, 'RTTimeUSecMax', dbus.Int64(200000))
    mock.AddProperty(MAIN_IFACE, 'MaxRealtimePriority', dbus.Int32(20))
    mock.AddProperty(MAIN_IFACE, 'MinNiceLevel', dbus.Int32(-15))
    mock.priorities = dict()

@dbus.service.method(MAIN_IFACE, in_signature='tu')
def MakeThreadRealtime(self, thread, priority):
    self.priorities[thread] = priority

@dbus.service.method(MAIN_IFACE, in_signature='tu')
def MakeThreadHighPriority(self, thread, priority):
    self.priorities[thread] = priority

@dbus.service.method(MOCK_IFACE)
def Reset(self):
    self.priorities = dict()

@dbus.service.method(MOCK_IFACE, in_signature='t', out_signature='u')
def GetThreadPriority(self, thread):
    if thread in self.priorities:
        return self.priorities[thread]
    else:
        return 0
