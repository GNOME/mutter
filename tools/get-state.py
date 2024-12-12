#!/usr/bin/env python3

import argparse
import enum
import subprocess
import sys

from gi.repository import GLib, Gio


NAME = 'org.gnome.Mutter.DisplayConfig'
INTERFACE = 'org.gnome.Mutter.DisplayConfig'
OBJECT_PATH = '/org/gnome/Mutter/DisplayConfig'

TRANSFORM_STRINGS = {
    0: 'normal',
    1: '90',
    2: '180',
    3: '270',
    4: 'flipped',
    5: 'flipped-90',
    6: 'flipped-180',
    7: 'flipped-270',
}

COLOR_MODES = {
    0: 'default',
    1: 'BT.2100',
}


class Source(enum.Enum):
    DBUS = 1
    COMMAND_LINE = 2
    FILE = 3


class MonitorConfig:
    CONFIG_VARIANT_TYPE = GLib.VariantType.new(
        '(ua((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})a{sv})')

    def get_current_state(self) -> GLib.Variant:
        raise NotImplementedError()

    def parse_data(self):
        """TODO: add data parser so that can be used for reconfiguring"""

    def print_data(self, *, level, is_last, lines, data):
        if is_last:
            link = '└'
        else:
            link = '├'
        padding = ' '

        if level >= 0:
            indent = level
            buffer = f'{link:{padding}>{indent * 4}}──{data}'
            buffer = list(buffer)
            for line in lines:
                if line == level:
                    continue
                index = line * 4
                if line > 0:
                    index -= 1
                buffer[index] = '│'
            buffer = ''.join(buffer)
        else:
            buffer = data

        print(buffer)

        if is_last and level in lines:
            lines.remove(level)
        elif not is_last and level not in lines:
            lines.append(level)

    def maybe_describe(self, property, value):
        if property == 'color-modes':
            return [COLOR_MODES.get(color_mode)
                    for color_mode in value]
        else:
            return value

    def print_properties(self, *, level, lines, properties):
        property_list = list(properties)

        self.print_data(level=level, is_last=True, lines=lines,
                        data=f'Properties: ({len(property_list)})')
        for property in property_list:
            is_last = property == property_list[-1]
            property_value = self.maybe_describe(property, properties[property])
            self.print_data(level=level + 1, is_last=is_last, lines=lines,
                            data=f'{property} ⇒ {property_value}')

    def print_current_state(self, short):
        variant = self.get_current_state()

        print('Serial: {}'.format(variant[0]))
        print()
        print('Monitors:')
        monitors = variant[1]
        lines = []
        for monitor in monitors:
            is_last = monitor == monitors[-1]
            spec = monitor[0]
            modes = monitor[1]
            properties = monitor[2]
            self.print_data(level=0, is_last=is_last, lines=lines,
                            data='Monitor {}'.format(spec[0]))
            self.print_data(level=1, is_last=False, lines=lines,
                            data=f'EDID: vendor: {spec[1]}, product: {spec[2]}, serial: {spec[3]}')

            mode_count = len(modes)
            if short:
                modes = [mode for mode in modes if len(mode[6]) > 0]
                self.print_data(level=1, is_last=False, lines=lines,
                                data=f'Modes ({len(modes)}, {mode_count - len(modes)} omitted)')
            else:
                self.print_data(level=1, is_last=False, lines=lines,
                                data=f'Modes ({len(modes)})')

            for mode in modes:
                is_last = mode == modes[-1]
                self.print_data(level=2, is_last=is_last, lines=lines,
                                data=f'{mode[0]}')
                self.print_data(level=3, is_last=False, lines=lines,
                                data=f'Dimension: {mode[1]}x{mode[2]}')
                self.print_data(level=3, is_last=False, lines=lines,
                                data=f'Refresh rate: {mode[3]:.3f}')
                self.print_data(level=3, is_last=False, lines=lines,
                                data=f'Preferred scale: {mode[4]}')
                self.print_data(level=3, is_last=False, lines=lines,
                                data=f'Supported scales: {mode[5]}')

                mode_properties = mode[6]
                self.print_properties(level=3, lines=lines,
                                      properties=mode_properties)

            self.print_properties(level=1, lines=lines, properties=properties)

        print()
        print('Logical monitors:')
        logical_monitors = variant[2]
        index = 1
        for logical_monitor in logical_monitors:
            is_last = logical_monitor == logical_monitors[-1]
            properties = logical_monitor[2]
            self.print_data(level=0, is_last=is_last, lines=lines,
                            data=f'Logical monitor #{index}')
            self.print_data(level=1, is_last=False, lines=lines,
                            data=f'Position: ({logical_monitor[0]}, {logical_monitor[1]})')
            self.print_data(level=1, is_last=False, lines=lines,
                            data=f'Scale: {logical_monitor[2]}')
            self.print_data(level=1, is_last=False, lines=lines,
                            data=f'Transform: {TRANSFORM_STRINGS.get(logical_monitor[3])}')
            self.print_data(level=1, is_last=False, lines=lines,
                            data=f'Primary: {logical_monitor[4]}')
            monitors = logical_monitor[5]
            self.print_data(level=1, is_last=False, lines=lines,
                            data=f'Monitors: ({len(monitors)})')
            for monitor in monitors:
                is_last = monitor == monitors[-1]
                self.print_data(level=2, is_last=is_last, lines=lines,
                                data=f'{monitor[0]} ({monitor[1]}, {monitor[2]}, {monitor[3]})')

            properties = logical_monitor[6]
            self.print_properties(level=1, lines=lines, properties=properties)

            index += 1

        properties = variant[3]
        print()
        self.print_properties(level=-1, lines=lines, properties=properties)


class MonitorConfigDBus(MonitorConfig):
    def __init__(self):
        self._proxy = Gio.DBusProxy.new_for_bus_sync(
            bus_type=Gio.BusType.SESSION,
            flags=Gio.DBusProxyFlags.NONE,
            info=None,
            name=NAME,
            object_path=OBJECT_PATH,
            interface_name=INTERFACE,
            cancellable=None,
        )

    def get_current_state(self) -> GLib.Variant:
        variant = self._proxy.call_sync(
            method_name='GetCurrentState',
            parameters=None,
            flags=Gio.DBusCallFlags.NO_AUTO_START,
            timeout_msec=-1,
            cancellable=None
        )
        assert variant.get_type().equal(self.CONFIG_VARIANT_TYPE)
        return variant


class MonitorConfigCommandLine(MonitorConfig):
    def get_current_state(self) -> GLib.Variant:
        command = ('gdbus call -e '
                   f'-d {NAME} '
                   f'-o {OBJECT_PATH} '
                   f'-m {INTERFACE}.GetCurrentState')

        result = subprocess.run(command, shell=True,
                                check=True, capture_output=True, text=True)
        return GLib.variant_parse(self.CONFIG_VARIANT_TYPE, result.stdout)


class MonitorConfigFile(MonitorConfig):
    def __init__(self, file_path):
        if file_path == '-':
            self._data = sys.stdin.read()
        else:
            with open(file_path) as file:
                self._data = file.read()

    def get_current_state(self) -> GLib.Variant:
        return GLib.variant_parse(self.CONFIG_VARIANT_TYPE, self._data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Get display state')
    parser.add_argument('--file', metavar='FILE', type=str, nargs='?',
                        help='Read the output from gdbus call instead of calling D-Bus')
    parser.add_argument('--gdbus', action='store_true')
    parser.add_argument('--short', action='store_true')

    args = parser.parse_args()

    if args.file and args.gdbus:
        raise argparse.ArgumentTypeError('Incompatible arguments')

    if args.file:
        monitor_config = MonitorConfigFile(args.file)
    elif args.gdbus:
        monitor_config = MonitorConfigCommandLine()
    else:
        monitor_config = MonitorConfigDBus()

    monitor_config.print_current_state(short=args.short)
