#!/usr/bin/env bash

echo "Checking possible misuses of the default main context:"
git grep -E '^ *(g_idle_add |g_timeout_add |g_source_remove |g_source_attach .*NULL)' clutter/ cogl/ src/ |grep -v src/tests
exit_code=$?

if [ $exit_code -ne 1 ]
then
  echo 'Instances found. Consider MTK helpers for those'
  exit 1
else
  echo 'All good'
  exit 0
fi
