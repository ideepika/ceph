#!/bin/bash

#
# Helpers
#

function setup_gitea() {
    export EMAIL=somename@example.com
    export github_endpoint="http://${MY_IP}:8781/ceph/ceph"
    export github_api_endpoint="http://${MY_IP}:8781/api/v1"
    export github_pull_path="pulls"
    $SRC/tests/gitea-helper.py setup || return 1
    cp $TMP/gitea-contributor-token $HOME/.github_token || return 1
    init_github_token
    set_github_user_from_github_token quiet
    test "$github_user" = "contributor" || return 1
    test_git
    test_upstream_url="http://${MY_IP}:8781/ceph/ceph"
    export upstream_remote="origin"
    test_fork_url="http://oauth2:${github_token}@${MY_IP}:8781/contributor/ceph"
    export fork_remote="contributor"
    export expected_base_branch="master"
    git clone $test_upstream_url
    cd ceph
    git remote add contributor $test_fork_url
    git fetch contributor
    local release
    for release in $(ls $TMP | sed -n 's/milestone-//p') ; do
	eval export milestone_$release=$(cat $TMP/milestone-$release)
    done
}

function teardown_gitea() {
    $SRC/tests/gitea-helper.py teardown
}

function setup_redmine() {
    export redmine_endpoint="http://${MY_IP}:8081"
    cp $SRC/ceph-workbench/tests/redmine-api-key $HOME/.redmine_key || return 1
    init_redmine_key
    test -n "$redmine_key" || return 1
}

function teardown_redmine() {
    $SRC/tests/redmine-helper.py teardown
}

function ingest_functions() {
    sed -e '1,/^SCRIPT_VERSION/s/.*//' \
	-e '/are we in a local git clone/,$s/.*//' < $SRC/../ceph-backport.sh > $TMP/functions-ceph-backport.sh
    source $TMP/functions-ceph-backport.sh
}

function setup() {
    ingest_functions || return 1
    setup_gitea || return 1
    setup_redmine || return 1
    vet_setup --report >& $TMP/out
    if ! grep -q 'setup is OK' $TMP/out ; then
	cat $TMP/out
	return 1
    fi
}

function teardown() {
    teardown_gitea || return 1
    teardown_redmine || return 1
    rm -fr "$TMP" ; mkdir "$TMP"
}

function run_tests() {
    local funcs="$@"

    shopt -s -o xtrace
    PS4='${BASH_SOURCE[0]}:$LINENO: ${FUNCNAME[0]}:  '

    : ${MY_IP:=0.0.0.0}
    export TMP=$(mktemp -d)
    trap "rm -fr $TMP" EXIT
    SRC=$(pwd)
    export HOME=$TMP
    export display_url_stdout=true
    export PYTHONPATH=$SRC/ceph-workbench
    export PATH=.:$PATH # make sure program from sources are preferred

    teardown
    for func in $funcs ; do
	cd $TMP
	setup || return 1
        if ! $func; then
            teardown
            return 1
	else
	    teardown
        fi
    done
    return 0
}

#
# Tests
#

function test_missing_issue() {
    local out=$TMP/run.out
    if $SRC/../ceph-backport.sh "$@" >& $out ; then
	cat $out
	return 1
    fi
    if ! grep -q 'Invalid or missing argument' $out ; then
	cat $out
	return 1
    fi
}

function test_pr_commits_count() {
    local release=pacific
    local original_pr=$(cat $TMP/merged-pr)
    local remote_api_output=$(curl -u ${github_user}:${github_token} --silent "${github_api_endpoint}/repos/ceph/ceph/pulls/${original_pr}")
    #
    # .commits is missing
    #
    git fetch "$upstream_remote" "pull/$original_pr/head:pr-$original_pr"
    number_of_commits=$(pr_commits_count "${remote_api_output}")
    test "$number_of_commits" = 1 || return 1
    #
    # .commits is present
    #
    local expected_number_of_commits=12345
    local with_commits=$(echo "$remote_api_output" | jq ". += {\"commits\": $expected_number_of_commits}")
    number_of_commits=$(pr_commits_count "${with_commits}")
    test "$number_of_commits" = "$expected_number_of_commits" || return 1
}

function test_create_backport_pr() {
    local release=pacific
    $SRC/tests/redmine-helper.py --backports $release --merged-pr $(cat $TMP/merged-pr) setup
    export redmine_pull_request_id_custom_field="$(cat $TMP/redmine_pull_request_id_custom_field)"
    local backport_issue=$(cat $TMP/backport-$release)
    local out=$TMP/run.out
    if ! env PS4="$PS4" bash -x $SRC/../ceph-backport.sh --debug "$backport_issue" >& $out ; then
	cat $out
	return 1
    fi
    grep "^DISPLAY_URL: $github_endpoint" $out || return 1
    grep "^DISPLAY_URL: $redmine_endpoint" $out || return 1
    backport_url="$(number_to_url "redmine" "${backport_issue}")"
    vet_backport_pr_is_staged $backport_url $release
}

function test_get_backport_issue() {
    local releases=octopus,pacific
    $SRC/tests/redmine-helper.py --backports $releases --merged-pr $(cat $TMP/merged-pr) setup
    local original_url=$(number_to_url "redmine" "$(cat $TMP/original-issue)")
    if get_backport_issue $original_url nautilus ; then
	return 1
    fi
    for release in octopus pacific ; do
	local expected_url=$(number_to_url "redmine" "$(cat $TMP/backport-$release)")
	local actual_url=$(get_backport_issue $original_url $release)
	if test "$expected_url" != "$actual_url" ; then
	    return 1
	fi
    done
    return 0
}

function test_vet_backport_pr_is_staged() {
    local release=pacific
    $SRC/tests/redmine-helper.py --backports $release --merged-pr $(cat $TMP/merged-pr) setup
    local backport_url=$(number_to_url "redmine" "$(cat $TMP/backport-$release)")
    if vet_backport_pr_is_staged $backport_url $release || test $? != 1 ; then
	return 1
    fi
    export redmine_pull_request_id_custom_field="$(cat $TMP/redmine_pull_request_id_custom_field)"
    local backport_issue=$(cat $TMP/backport-$release)
    local out=$TMP/run.out
    if ! $SRC/../ceph-backport.sh "$backport_issue" >& $out ; then
	cat $out
	return 1
    fi
    vet_backport_pr_is_staged $backport_url $release || return 1
}

function test_vet_backport_issue() {
    local release=pacific
    $SRC/tests/redmine-helper.py --backports $release --merged-pr $(cat $TMP/merged-pr) setup
    local original=$(number_to_url "redmine" "$(cat $TMP/original-issue)")
    if vet_backport_issue $original $release || test $? != 1 ; then
	return 1
    fi
    export redmine_pull_request_id_custom_field="$(cat $TMP/redmine_pull_request_id_custom_field)"
    local backport_issue=$(cat $TMP/backport-$release)
    local out=$TMP/run.out
    if ! $SRC/../ceph-backport.sh "$backport_issue" >& $out ; then
	cat $out
	return 1
    fi
    vet_backport_issue $original $release || return 1
}

function test_vet_backport_ordering() {
    $SRC/tests/redmine-helper.py --backports octopus,pacific --merged-pr $(cat $TMP/merged-pr) setup
    init_active_milestones
    #
    # Deny backport to octopus when there is no PR staged for pacific
    #
    local backport_octopus=$(cat $TMP/backport-octopus)
    redmine_url="$(number_to_url "redmine" "${backport_octopus}")"
    local backport_octopus_url=$redmine_url
    populate_original_issue
    if vet_backport_ordering $backport_octopus_url octopus || test $? != 1 ; then
	return 1
    fi
    #
    # Trying to backport to the latest release (pacific) is always ok
    #
    local backport_pacific=$(cat $TMP/backport-pacific)
    local backport_pacific_url="$(number_to_url "redmine" "${backport_pacific}")"
    vet_backport_ordering $backport_pacific_url pacific || return
    export redmine_pull_request_id_custom_field="$(cat $TMP/redmine_pull_request_id_custom_field)"
    local out=$TMP/run.out
    if ! $SRC/../ceph-backport.sh "$backport_pacific" >& $out ; then
	cat $out
	return 1
    fi
    #
    # Allow backport to octopus when there is a PR staged for pacific
    #
    redmine_url=$backport_octopus_url
    vet_backport_ordering $backport_octopus_url octopus || return 1
}

run_tests "${@:-$(set | sed -n -e 's/^\(test_[0-9a-z_]*\) .*/\1/p')}"
