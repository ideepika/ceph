#/bin/bash

# this script is run from build dir for dev setup

CEPH_SRC_PATH=../src
CEPH_EXECUTABLE_PATH=$(pwd)/bin
PATH=$CEPH_EXECUTABLE_PATH:$CEPH_SRC_PATH:$PATH
RBD_MIRROR_TEMPDIR="/tmp/tmp.FOkYipguDP/"
MIRROR_POOL_MODE=image
MIRROR_IMAGE_MODE=snapshot

if [ ! -d tmp ]; then
  mkdir tmp
fi

rm -rf ${RBD_MIRROR_TEMPDIR}/.*.pid
if [[ "$@" = "new" ]]; then
  echo "===setting up new cluster==="
  sed -i 's/\<setup_tempdir\>/setup/g' ../qa/workunits/rbd/rbd_mirror_snapshot.sh 
  RBD_MIRROR_TEMPDIR="${RBD_MIRROR_TEMPDIR}" RBD_MIRROR_NOCLEANUP=1 ../qa/workunits/rbd/rbd_mirror_snapshot.sh
else
  echo "===using old cluster==="
  sed -i 's/\<setup\>/setup_tempdir/g' ../qa/workunits/rbd/rbd_mirror_snapshot.sh 
  RBD_MIRROR_TEMPDIR="${RBD_MIRROR_TEMPDIR}" RBD_MIRROR_USE_EXISTING_CLUSTER=1 ../qa/workunits/rbd/rbd_mirror_snapshot.sh
fi