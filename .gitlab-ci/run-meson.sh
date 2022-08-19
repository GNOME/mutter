#!/usr/bin/bash

set -e

wireplumber &
sleep 1

meson "$@"
