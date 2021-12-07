#!/usr/bin/bash

set -e

wireplumber &
sleep 1

catchsegv meson test -C build --no-rebuild -t 10
