#!/usr/bin/env python3

import argparse
import enum
import subprocess
import sys

from gi.repository import GLib


class Source(enum.Enum):
    DBUS = 1
    FILE = 2


parser = argparse.ArgumentParser(description='Get display state')
parser.add_argument('file', metavar='FILE', type=str, nargs='?',
                    help='Read the output from gdbus call instead of calling D-Bus')

args = parser.parse_args()

if args.file:
  source = Source.FILE
  path = args.file
else:
  source = Source.DBUS

type_signature = '(ua((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})a{sv})'
variant_type = GLib.VariantType.new(type_signature)

if source == Source.DBUS:
    command = 'gdbus call -e '\
        '-d org.gnome.Mutter.DisplayConfig '\
        '-o /org/gnome/Mutter/DisplayConfig '\
        '-m org.gnome.Mutter.DisplayConfig.GetCurrentState'
    result = subprocess.run(command,
                            shell=True, check=True, capture_output=True, text=True)
    data = result.stdout
else:
    if path == '-':
        data = sys.stdin.read()
    else:
        with open(path) as file:
            data = file.read()

variant = GLib.variant_parse(variant_type, data)

def transform_to_string(transform):
    match transform:
        case 0: return 'normal'
        case 1: return '90'
        case 2: return '180'
        case 3: return '270'
        case 4: return 'flipped'
        case 5: return 'flipped-90'
        case 6: return 'flipped-180'
        case 7: return 'flipped-270'

def print_data(level, is_last, lines, data):
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

def print_properties(level, lines, properties):
    property_list = list(properties)

    print_data(level, True, lines, f'Properties: ({len(property_list)})')
    for property in property_list:
        is_last = property == property_list[-1]
        print_data(level + 1, is_last, lines,
                   f'{property} ⇒ {properties[property]}')

print('Serial: {}'.format(variant[0]))
print()
print('Monitors:')
monitors = variant[1]
lines=[]
for monitor in monitors:
    is_last = monitor == monitors[-1]
    spec = monitor[0]
    modes = monitor[1]
    properties = monitor[2]
    print_data(0, is_last, lines, 'Monitor {}'.format(spec[0]))
    print_data(1, False, lines, f'EDID: vendor: {spec[1]}, product: {spec[2]}, serial: {spec[3]}')
    print_data(1, False, lines, f'Modes ({len(modes)})')

    for mode in modes:
        is_last = mode == modes[-1]
        print_data(2, is_last, lines, f'{mode[0]}')
        print_data(3, False, lines, f'Dimension: {mode[1]}x{mode[2]}')
        print_data(3, False, lines, f'Refresh rate: {mode[3]}')
        print_data(3, False, lines, f'Preferred scale: {mode[4]}')
        print_data(3, False, lines, f'Supported scales: {mode[5]}')

        mode_properties = mode[6]
        print_properties(3, lines, mode_properties)

    print_properties(1, lines, properties)

print()
print('Logical monitors:')
logical_monitors = variant[2]
index = 1
for logical_monitor in logical_monitors:
    is_last = logical_monitor == logical_monitors[-1]
    properties = logical_monitor[2]
    print_data(0, is_last, lines, f'Logical monitor #{index}')
    print_data(1, False, lines,
               f'Position: ({logical_monitor[0]}, {logical_monitor[1]})')
    print_data(1, False, lines,
               f'Scale: {logical_monitor[2]}')
    print_data(1, False, lines,
               f'Transform: {transform_to_string(logical_monitor[3])}')
    print_data(1, False, lines,
               f'Primary: {logical_monitor[4]}')
    monitors = logical_monitor[5]
    print_data(1, False, lines,
               f'Monitors: ({len(monitors)})')
    for monitor in monitors:
        is_last = monitor == monitors[-1]
        print_data(2, is_last, lines,
                   f'{monitor[0]} ({monitor[1]}, {monitor[2]}, {monitor[3]})')

    properties = logical_monitor[6]
    print_properties(1, lines, properties)

    index += 1

properties = variant[3]
print()
print_properties(-1, lines, properties)
