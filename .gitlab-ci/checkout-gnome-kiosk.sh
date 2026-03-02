#!/bin/bash

fetch() {
  local remote=$1
  local ref=$2

  git fetch --quiet --depth=1 $remote $ref 2>/dev/null
}

gnome_kiosk_target=

echo -n Cloning into gnome-kiosk ...
if git clone --quiet --depth=1 https://gitlab.gnome.org/GNOME/gnome-kiosk.git; then
  echo \ done
else
  echo \ failed
  exit 1
fi

cd gnome-kiosk

if [ "$CI_MERGE_REQUEST_TARGET_BRANCH_NAME" ]; then
  merge_request_remote=${CI_MERGE_REQUEST_SOURCE_PROJECT_URL//mutter/gnome-kiosk}
  merge_request_branch=$CI_MERGE_REQUEST_SOURCE_BRANCH_NAME

  echo -n Looking for $merge_request_branch on remote ...
  if fetch $merge_request_remote $merge_request_branch; then
    echo \ found
    gnome_kiosk_target=FETCH_HEAD
  else
    echo \ not found

    echo -n Looking for $CI_MERGE_REQUEST_TARGET_BRANCH_NAME instead ...
    if fetch origin $CI_MERGE_REQUEST_TARGET_BRANCH_NAME; then
      echo \ found
      gnome_kiosk_target=FETCH_HEAD
    else
      echo \ not found
    fi
  fi
fi

if [ -z "$gnome_kiosk_target" ]; then
  ref_remote=${CI_PROJECT_URL//mutter/gnome-kiosk}
  echo -n Looking for $CI_COMMIT_REF_NAME on remote ...
  if fetch $ref_remote $CI_COMMIT_REF_NAME; then
    echo \ found
    gnome_kiosk_target=FETCH_HEAD
  else
    echo \ not found
    gnome_kiosk_target=HEAD
    echo Using $gnome_kiosk_target instead
  fi
fi

git checkout -q $gnome_kiosk_target
