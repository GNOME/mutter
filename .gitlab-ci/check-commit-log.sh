#!/usr/bin/env bash

if [ -z "$CI_MERGE_REQUEST_TARGET_BRANCH_NAME" ]; then
  echo Cannot review non-merge request
  exit 1
fi

git fetch $CI_MERGE_REQUEST_PROJECT_URL.git $CI_MERGE_REQUEST_TARGET_BRANCH_NAME

branch_point=$(git merge-base HEAD FETCH_HEAD)

commits=$(git log --format='format:%H' $branch_point..$CI_COMMIT_SHA)

if [ -z "$commits" ]; then
  echo Commit range empty
  exit 1
fi

function commit_message_has_url() {
  commit=$1
  commit_message=$(git show -s --format='format:%b' $commit)
  echo "$commit_message" | grep -qe "\($CI_MERGE_REQUEST_PROJECT_URL/\(-/\)\?\(issues\|merge_requests\)/[0-9]\+\|https://bugzilla.gnome.org/show_bug.cgi?id=[0-9]\+\)"
  return $?
}

function commit_message_subject_is_compliant() {
  commit=$1
  commit_message_subject=$(git show -s --format='format:%s' $commit)

  if echo "$commit_message_subject" | grep -qe "\(^meta-\|^Meta\)"; then
    echo " - message subject should not be prefixed with 'meta-' or 'Meta'"
    return 1
  fi

  if echo "$commit_message_subject" | grep -qe "\.[ch]:"; then
    echo " - message subject prefix should not include .c, .h, etc."
    return 1
  fi

  return 0
}

RET=0
for commit in $commits; do
  commit_short=$(echo $commit | cut -c -8)

  if ! commit_message_has_url $commit; then
    echo "Commit $commit_short needs a merge request or issue URL"
    exit 1
  fi

  errors=$(commit_message_subject_is_compliant $commit)
  if [ $? != 0 ]; then
    echo "Commit message for $commit_short is not compliant:"
    echo "$errors"
    RET=1
  fi
done

exit $RET
