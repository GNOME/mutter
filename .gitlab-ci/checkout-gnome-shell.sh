#!/usr/bin/bash

mutter_branch=$(git describe --contains --all HEAD)
gnome_shell_target=

git clone https://gitlab.gnome.org/GNOME/gnome-shell.git

if [ $? -ne 0 ]; then
  echo Checkout failed
  exit 1
fi

cd gnome-shell

if [ "$CI_MERGE_REQUEST_TARGET_BRANCH_NAME" ]; then
  merge_request_remote=${CI_MERGE_REQUEST_SOURCE_PROJECT_URL//mutter/gnome-shell}
  merge_request_branch=$CI_MERGE_REQUEST_SOURCE_BRANCH_NAME

  echo Looking for $merge_request_branch on remote ...
  if git fetch $merge_request_remote $merge_request_branch >/dev/null 2>&1; then
    gnome_shell_target=FETCH_HEAD
  fi
fi

if [ -z "$gnome_shell_target" ]; then
  gnome_shell_target=$(git branch -r -l $mutter_branch)
  gnome_shell_target=${gnome_shell_target:-origin/master}
  echo Using $gnome_shell_target instead
fi

git checkout $gnome_shell_target
