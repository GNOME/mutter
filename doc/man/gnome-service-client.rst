===================
gnome-service-client
===================

-------------------
GNOME Service Client
-------------------

:Manual section: 1
:Manual group: User Commands

SYNOPSIS
--------

gnome-service-client [option ...] -- COMMAND

DESCRIPTION
-----------
gnome-service-client provides a way to spawn a Wayland client and optionally set
a default window tag for all its windows.

It requires a compositor that supports the ``org.gnome.Mutter.ServiceChannel``
D-Bus API, such as GNOME Shell or GNOME Kiosk.

OPTIONS
-------
``--help``, ``-h``

  Show a help message and exit.

``--tag``, ``-t``

  Optionally specifies the tag to set for all windows of the client.

EXAMPLES
--------

Start the client "gnome-calculator" without any tag

::

  gnome-service-client -- gnome-calculator

Start the client "gnome-tour" with the tag "demo"

::

  gnome-service-client -t demo -- gnome-tour

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
