# Code Overview

Mutter consists of four relatively distinguished and isolated parts.

## Cogl

Hardware acceleration pipeline abstraction layer. Handles things like allocating framebuffer, allocating, importing and drawing textures, internally using OpenGL. Originally a fork of [the Cogl project](https://gitlab.gnome.org/GNOME/cogl).

## Clutter

Compositing toolkit, containing an actor and render node based scene graph, and has features such as input event routing, transformation and animation. Handles compositing, both Wayland surfaces, X11 windows, and is the basis of the UI toolkit implemented by [GNOME Shell](https://gitlab.gnome.org/GNOME/gnome-shell). Originally a fork of [the Clutter project](https://gitlab.gnome.org/GNOME/clutter).

* [Frame Scheduling](clutter-frame-scheduling.md)
* [Rendering Model](clutter-rendering-model.md)

## Mtk

The Meta Toolkit containing utilities shared by other parts of mutter.

## Mutter

The display server and window manager library. Contains a X11 window manager and compositing manager implementation, as well as a Wayland display server implementation.

* [Compositor stage and hardware relationships](mutter-relationships.md)
* [KMS abstraction](mutter-kms-abstractions.md)
* [Window constraints](mutter-constraints.txt)
* [How to get focus right](mutter-focus.txt)
