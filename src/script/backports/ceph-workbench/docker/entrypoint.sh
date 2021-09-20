#!/bin/bash

CONF_DIR='/opt/.ceph-workbench'

function get_openrc() {
    while [ $# -ge 1 ] ; do
        if test "$1" = '--openrc' ; then
            shift
            echo "$1"
            return
        elif echo $1 | grep --quiet '^--openrc=' ; then
            echo $1 | sed -e 's/^--openrc=//'
            return
        fi
        shift
    done
    echo "openrc.sh"
}

function source_openrc() {
    local openrc=$(get_openrc "$@")
    if test -f $CONF_DIR/$openrc ; then
        source $CONF_DIR/$openrc
    else
        echo "~/.ceph-workbench/$openrc" does not exist >&2
        return 1
    fi
}


function run() {
    # Source openrc.sh only if ceph-qa-suite is in the argument
    for i in "$@" ; do
        if [ $i = "ceph-qa-suite" ] || [ $i = "release" ] ; then
            source_openrc "$@"
            break
        fi
    done
    
    adduser --disabled-password --gecos Teuthology --quiet --uid $USER_ID $USER_NAME
    if ! test -d /home/$USER_NAME/.ceph-workbench ; then
        ln -s /opt/.ceph-workbench /home/$USER_NAME/.ceph-workbench
    fi
    sed -i -e '/Defaults	env_reset/d' /etc/sudoers
    sed -i -e '/Defaults	secure_path/d'  /etc/sudoers
    sudo --set-home --preserve-env PATH=/opt/ceph-workbench/virtualenv/bin:$PATH --user $USER_NAME "$@"
}

function run_tests() {
    shopt -s -o xtrace
    PS4='${BASH_SOURCE[0]}:$LINENO: ${FUNCNAME[0]}:  '

    test 'foo.sh' = $(get_openrc --openrc foo.sh) || return 1
    test 'bar.sh' = $(get_openrc --openrc=bar.sh) || return 1
    test 'openrc.sh' = $(get_openrc) || return 1

    CONF_DIR=$HOME/.ceph-workbench
    rm -f $CONF_DIR/UNLIKELY.sh
    ! source_openrc --openrc UNLIKELY.sh || return 1
    mkdir -p $CONF_DIR
    echo VAR=VALUE | tee $CONF_DIR/UNLIKELY.sh
    source_openrc --openrc UNLIKELY.sh || return 1
    test $VAR = VALUE || return 1
    rm $CONF_DIR/UNLIKELY.sh
}

if test "$1" = TESTS ; then
    run_tests
elif test "$1" = install ; then
    cat <<'EOF'
function ceph-workbench() {
   mkdir -p $HOME/.ceph-workbench
   sudo docker run --rm -ti \
       -v $HOME/.ceph-workbench:/opt/.ceph-workbench \
       -v /var/run/docker.sock:/run/docker.sock \
       -v $(which docker):/bin/docker \
       -w /opt/.ceph-workbench \
       --env USER_ID=$(id -u) --env USER_NAME=$(id -un) \
       dachary/ceph-workbench \
       /opt/ceph-workbench/virtualenv/bin/ceph-workbench "$@"
}

function ceph-workbench-debug() {
   mkdir -p $HOME/.ceph-workbench
   sudo docker run --rm -ti \
       -v $HOME:$HOME \
       -v $HOME/.ceph-workbench:/opt/.ceph-workbench \
       -v /var/run/docker.sock:/run/docker.sock \
       -v $(which docker):/bin/docker \
       -v $(pwd):$(pwd) \
       -w $(pwd) \
       --env USER_ID=$(id -u) --env USER_NAME=$(id -un) \
       dachary/ceph-workbench \
       virtualenv/bin/ceph-workbench "$@"
}

function ceph-workbench-shell() {
   mkdir -p $HOME/.ceph-workbench
   sudo docker run --rm -ti \
       -v $HOME:$HOME \
       -v $HOME/.ceph-workbench:/opt/.ceph-workbench \
       -v /var/run/docker.sock:/run/docker.sock \
       -v $(which docker):/bin/docker \
       -v $(pwd):$(pwd) \
       -w $(pwd) \
       --env USER_ID=$(id -u) --env USER_NAME=$(id -un) \
       dachary/ceph-workbench \
       "$@"
}
EOF
else
    run "$@"
fi
