#!/usr/bin/bash

set -e

dconf update
glib-compile-schemas $GSETTINGS_SCHEMA_DIR

export MUTTER_DEBUG_DUMMY_MODE_SPECS="800x600@10.0"

xvfb-run -s '+iglx -noreset' \
    meson test -C build --no-rebuild -t 10 --wrap catchsegv

exit_code=$?

python3 .gitlab-ci/meson-junit-report.py \
        --project-name=mutter \
        --job-id "${CI_JOB_NAME}" \
        --output "build/${CI_JOB_NAME}-report.xml" \
        build/meson-logs/testlog-catchsegv.json

exit $exit_code
