#!/bin/bash -ex

if test "$(id -u)" != 0 ; then
    SUDO=sudo
fi

: ${MY_IP:=127.0.0.1}

function setup_redmine() {
    docker run --name=postgresql-redmine -d --env='DB_NAME=redmine_production' --env='DB_USER=redmine' --env='DB_PASS=password' sameersbn/postgresql:9.6-4
    docker run --name=redmine -d --link=postgresql-redmine:postgresql --publish=8081:80 --env='REDMINE_PORT=8081' --volume=redmine-volume:/home/redmine/data sameersbn/redmine:4.2.2
    success=false
    for delay in 15 15 15 15 15 30 30 30 30 30 30 30 30 60 60 60 60 120 240 512 ; do
	sleep $delay
	if poetry run tests/redmine-init.py http://${MY_IP}:8081 admin admin123 ; then
            success=true
            break
	fi
    done
}

function setup() {
    setup_redmine
}

function teardown() {
    for i in redmine postgresql-redmine ; do docker stop $i || true ; docker rm $i || true ; done
    docker volume rm --force redmine-volume
}

for f in ${@:-teardown setup} ; do
    $f
done

