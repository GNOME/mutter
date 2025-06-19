=====
gdctl
=====

------------------------
GNOME Display Controller
------------------------

:Manual section: 1
:Manual group: User Commands

SYNOPSIS
--------
**gdctl** [-h] COMMAND ...

DESCRIPTION
-----------
gdctl provides means to show the active monitor configuration, and set new
monitor configuration using command line arguments.

It requires a compositor that supports the ``org.gnome.Mutter.DisplayConfig``
D-Bus API, such as GNOME Shell.

COMMANDS
--------
``show``

  Show the current display configuration

``set``

  Set a new display configuration

``pref``

  Set display related preferences.

SHOW OPTIONS
------------
``--help``, ``-h``

  Show a help message and exit.

``--modes``, ``-m``

  List available monitor modes.

``--properties``, ``-p``

  List properties.

``--verbose``, ``-v``

  Display all available information (equivalent to ``--modes --properties``).

SET OPTIONS
-----------

``--help``, ``-h``

  Show a help message and exit.

``--persistent``, ``-P``

  Store applied configuration on persistent storage and restore when applicable.

``--verbose``, ``-v``

  Print configuration to standard out before applying it.

``--verify``, ``-V``

  Only verify, without applying, the configuration.

``--layout-mode LAYOUT-MODE``, ``-l``

  Specify the layout mode the configuration should use. Either ``logical``, or
  ``physical``.

``--logical-monitor``, ``-L``

  Add and configure a logical monitor. See LOGICAL MONITOR OPTIONS.

``--for-lease-monitor CONNECTOR``, ``-e CONNECTOR``

  Set a monitor, that is not part of any logical monitor, available for lease.

LOGICAL MONITOR OPTIONS
-----------------------

``--monitor CONNECTOR``, ``-M CONNECTOR``

  Add a monitor to the currently configured logical monitor. All monitors
  within the same logical monitor must have the same monitor resolution.

``--primary``, ``-p``

  Mark currently configured logical monitor as primary.

``--scale SCALE``, ``-s SCALE``

  Scale monitors within the currently configured logical monitor with
  ``SCALE``. Must be a scale supported by all monitors and their configured
  modes.

``--transform TRANSFORM``, ``-t TRANSFORM``

  Transform monitors within the currently configured logical monitor using
  ``TRANSFORM``. Possible transforms are ``normal``, ``90``, ``180``, ``270``,
  ``flipped``, ``flipped-90``, ``flipped-270`` and ``flipped-180``.

``--x X``, ``-x X``

  Set the X position of the currently configured logical monitor.

``--y Y``, ``-y Y``             Y position

  Set the Y position of the currently configured logical monitor.

``--right-of CONNECTOR``

  Place the logical monitor to the right of the logical monitor ``CONNECTOR``
  belongs to.

``--left-of CONNECTOR``   Place left of other monitor

  Place the logical monitor to the left of the logical monitor ``CONNECTOR``
  belongs to.

``--above CONNECTOR``

  Place the logical monitor above the logical monitor ``CONNECTOR`` belongs to.

``--below CONNECTOR``

  Place the logical monitor below the logical monitor ``CONNECTOR`` belongs to.

MONITOR OPTIONS
---------------

``--mode MODE``, ``-m MODE``

  Set the mode of the monitor.

``--color-mode COLOR-MODE``, ``-c COLOR-MODE``

  Set the color mode of the monitor. Available color modes are ``default`` and
  ``bt2100``.


``--rgb-range RGB-RANGE``, ``-r RGB-RANGE``

  Set the RGB quantization range of the monitor. Available ranges are ``auto``,
  ``full`` and ``limited`` (16:235).

PREFS OPTIONS
-------------

``--monitor CONNECTOR``, ``-M CONNECTOR``

  Change monitor preferences. See MONITOR PREFS OPTIONS.

MONITOR PREFS OPTIONS
---------------------

``--luminance LUMINANCE``, ``-l LUMINANCE``

  Set the luminance of the monitor for the current color mode.

``--reset-luminance``

  Reset the luminance of the monitor for the current color mode to its default.

EXAMPLES
--------

Mirror DP-1 and eDP-1, and place DP-2, transformed by 270 degrees, to the right
of the two mirrored monitors.

::

  gdctl set --logical-monitor
            --primary
            --monitor DP-1
            --monitor eDP-1
            --logical-monitor
            --monitor DP-2
            --right-of DP-1
            --transform 270

Set eDP-1 and DP-2 as available for lease.

::

  gdctl set --logical-monitor
            --primary
            --monitor DP-1
            --for-lease-monitor eDP-1
            --for-lease-monitor DP-2

BUGS
----
The bug tracker can be reached by visiting the website
https://gitlab.gnome.org/GNOME/mutter/-/issues.
Before sending a bug report, please verify that you have the latest version
of gnome-shell. Many bugs (major and minor) are fixed at each release, and
if yours is out of date, the problem may already have been solved.

ADDITIONAL INFORMATION
----------------------
For further information, visit the website
https://gitlab.gnome.org/GNOME/mutter/-/blob/main/README.md.
