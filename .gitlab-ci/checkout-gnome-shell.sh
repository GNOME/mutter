#!/usr/bin/bash

gnome_shell_target=

echo -n Cloning into gnome-shell ...
if git clone --quiet https://gitlab.gnome.org/GNOME/gnome-shell.git; then
  echo \ done
else
  echo \ failed
  exit 1
fi

cd gnome-shell

if [ "$CI_MERGE_REQUEST_TARGET_BRANCH_NAME" ]; then
  merge_request_remote=${CI_MERGE_REQUEST_SOURCE_PROJECT_URL//mutter/gnome-shell}
  merge_request_branch=$CI_MERGE_REQUEST_SOURCE_BRANCH_NAME

  echo -n Looking for $merge_request_branch on remote ...
  if git fetch -q $merge_request_remote $merge_request_branch 2>/dev/null; then
    echo \ found
    gnome_shell_target=FETCH_HEAD
  else
    echo \ not found
    gnome_shell_target=origin/$CI_MERGE_REQUEST_TARGET_BRANCH_NAME
    echo Using $gnome_shell_target instead
  fi
fi

if [ -z "$gnome_shell_target" ]; then
  echo -n Looking for $CI_COMMIT_REF_NAME on remote ...
  gnome_shell_target=$(git branch -r -l origin/$CI_COMMIT_REF_NAME)
  if [ "$gnome_shell_target" ]; then
    echo \ found
  else
    echo \ not found
    gnome_shell_target=origin/master
    echo Using $gnome_shell_target instead
  fi
fi

git checkout -q $gnome_shell_target
