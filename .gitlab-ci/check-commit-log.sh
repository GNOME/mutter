#!/usr/bin/env bash

export

test -z "$CI_MERGE_REQUEST_TARGET_BRANCH_NAME" && { echo Cannot review non-merge request; exit 1; }

branch_point=$(git merge-base HEAD $CI_MERGE_REQUEST_TARGET_BRANCH_NAME)

commits=$(git log --format='format:%H' $branch_point..$CI_COMMIT_SHA)

test -z "$commits" && { echo Commit range empty; exit 1; }

for commit in $commits; do
  git show -s --format='format:%b' $commit | grep -qe "\($CI_PROJECT_URL/\(issues\|merge_requests\)/[0-9]\+\|https://bugzilla.gnome.org/show_bug.cgi?id=[0-9]\+\)" ||
    { echo "Missing merge request or issue URL on commit $(echo $commit | cut -c -8)"; exit 1; }
done
