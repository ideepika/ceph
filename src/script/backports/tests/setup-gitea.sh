#!/bin/bash

set -ex

function prepare_environment() {
    : ${MY_IP:=0.0.0.0}
}

function wait_for() {
    success=false
    for delay in 1 1 5 5 15 15 15 30 30 30 30 ; do
	if "$@" |& tee tests/setup-gitea.out ; then
	    success=true
	    break
	fi
	sleep $delay
    done
    $success
}

function setup_gitea() {
    docker run --name gitea -p 8781:3000 -p 2721:22 \
	   -e "GITEA__security__INSTALL_LOCK=true" \
	   -e "GITEA__server__DOMAIN=${MY_IP}" \
	   -e "GITEA__server__SSH_DOMAIN=${MY_IP}" \
	   -e "GITEA__server__ROOT_URL=http://${MY_IP}:8781/" \
	   -e "GITEA__service__DEFAULT_KEEP_EMAIL_PRIVATE=true" \
	   -d gitea/gitea:1.15.2
    sleep 5 # for some reason trying to run "gitea admin" while gitea is booting will permanently break everything
    wait_for docker exec gitea gitea admin user create --access-token --admin --username root --password Wrobyak4 --email admin@example.com
}

function setup() {
    setup_gitea
}

function teardown() {
    docker stop gitea || true
    docker rm -f gitea || true
}

for f in prepare_environment ${@:-teardown setup} ; do
    $f
done
