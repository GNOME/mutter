import dbus
import os
import re

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
