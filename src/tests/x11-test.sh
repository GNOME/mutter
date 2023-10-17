#!/bin/bash

set -e

MUTTER="$1"

if [ -z "$MUTTER" ]; then
  echo Usage: $0 PATH-TO-MUTTER > /dev/stderr
  exit 1
fi

export GDK_BACKEND=x11
export G_DEBUG=fatal-warnings

echo \# Launching mutter > /dev/stderr
$MUTTER --x11 --mutter-plugin="$MUTTER_TEST_PLUGIN_PATH" &
MUTTER1_PID=$!
gdbus wait --session org.gnome.Mutter.IdleMonitor
echo \# Launched with pid $MUTTER1_PID

sleep 2

echo Launching a couple of X11 clients > /dev/stderr
zenity --warning &
ZENITY1_PID=$!
sleep 2
zenity --info &
ZENITY2_PID=$!
sleep 4

echo \# Replacing existing mutter with a new instance > /dev/stderr
$MUTTER --x11 --replace --mutter-plugin="$MUTTER_TEST_PLUGIN_PATH" &
echo \# Launched with pid $MUTTER2_PID
MUTTER2_PID=$!
wait $MUTTER1_PID

echo \# Waiting for the second mutter to finish loading
gdbus wait --session org.gnome.Mutter.IdleMonitor

sleep 2

echo \# Terminating clients > /dev/stderr
kill $ZENITY1_PID
sleep 1
kill $ZENITY2_PID
sleep 1

echo \# Terminating mutter > /dev/stderr
kill $MUTTER2_PID
wait $MUTTER2_PID
