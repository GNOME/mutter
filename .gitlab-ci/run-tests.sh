#!/usr/bin/bash

set -e

dconf update
glib-compile-schemas $GSETTINGS_SCHEMA_DIR

# Disable e.g. audio support to not dead lock screen cast tests
rm -f /usr/share/pipewire/media-session.d/with-*

PIPEWIRE_DEBUG=2 PIPEWIRE_LOG="$CI_PROJECT_DIR/build/meson-logs/pipewire.log" \
  pipewire &

sleep 2

meson test -C build --no-rebuild --setup CI

exit_code=$?

exit $exit_code
