#!/bin/bash

set -ex

source=$1
destination=$2

cp -a $source $destination.tmp
(
    cd $destination.tmp
    git init
    git add *
    git -c user.name="Name" -c user.email=m@m.com commit -a -m 'init'
    git -c user.name="Name" -c user.email=m@m.com tag --annotate -m 'version comment' 'v0.20.0'
)
git clone --bare $destination.tmp $destination
(
    cd $destination
    git update-server-info
)
