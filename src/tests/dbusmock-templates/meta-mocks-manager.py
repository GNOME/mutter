# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.

__author__ = 'Marco Trevisan'
__copyright__ = '(c) 2021 Canonical Ltd.'

import dbus
import fcntl
import os
import subprocess

from collections import OrderedDict
from dbusmock import DBusTestCase

BUS_NAME = 'org.gnome.Mutter.TestDBusMocksManager'
MAIN_OBJ = '/org/gnome/Mutter/TestDBusMocksManager'
MAIN_IFACE = 'org.gnome.Mutter.TestDBusMocksManager'
SYSTEM_BUS = True


def load(mock, parameters):
    mock.mocks = OrderedDict()
    DBusTestCase.setUpClass()
    mock.dbus_mock = DBusTestCase()
    mock.dbus_mock.setUp()
    mock.templates_dir = parameters['templates-dir']


def set_nonblock(fd):
    '''Set a file object to non-blocking'''

    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)


@dbus.service.method(MAIN_IFACE, in_signature='s')
def StartFromTemplate(self, template):
    if template in self.mocks.keys():
        raise KeyError('Template {} already started'.format(template))

    mock_server, mock_obj = self.dbus_mock.spawn_server_template(template, {})

    self.mocks[template] = (mock_server, mock_obj)


@dbus.service.method(MAIN_IFACE, in_signature='s')
def StartFromLocalTemplate(self, template):
    path = os.path.join(self.templates_dir, template + '.py')
    return self.StartFromTemplate(path)


@dbus.service.method(MAIN_IFACE, in_signature='s')
def StopTemplate(self, template):
    (mock_server, mock_obj) = self.mocks.pop(template)
    mock_server.terminate()
    mock_server.wait()


@dbus.service.method(MAIN_IFACE, in_signature='s')
def StopLocalTemplate(self, template):
    path = os.path.join(self.templates_dir, template + '.py')
    return self.StopTemplate(path)


@dbus.service.method(MAIN_IFACE)
def Cleanup(self):
    for (mock_server, mock_obj) in reversed(self.mocks.values()):
        mock_server.terminate()
        mock_server.wait()

    self.dbus_mock.tearDown()
    DBusTestCase.tearDownClass()


@dbus.service.method(MAIN_IFACE, out_signature='as')
def ListRunningTemplates(self):
    return list(self.mocks.keys())

