import dbus
import os
import re
import sys


host_system_bus_connection = None


def ensure_system_bus(address):
    global host_system_bus_connection

    if host_system_bus_connection is None:
        bus = dbus.bus.BusConnection(address)
        host_system_bus_connection = bus

    return host_system_bus_connection


def open_file_direct(major, minor):
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
    fd = os.open('/dev/' + devname, os.O_RDWR | os.O_CLOEXEC)
    unix_fd = dbus.types.UnixFd(fd)
    os.close(fd)
    return (unix_fd, False)


def call_host(address, object_path, interface, method, typesig, args):
    bus = ensure_system_bus(address)

    return bus.call_blocking('org.freedesktop.login1',
                             object_path,
                             interface,
                             method,
                             typesig,
                             args)
