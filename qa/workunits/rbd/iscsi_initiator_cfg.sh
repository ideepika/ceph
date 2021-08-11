#!/usr/bin/env bash
set -ex

client_iqn="iqn.1994-05.com.redhat:client"

if [ ! -d /etc/iscsi ]; then
  sudo mkdir /etc/iscsi/
fi

echo "InitiatorName=$client_iqn" | sudo tee /etc/iscsi/initiatorname.iscsi

# the restart is needed after the above change is applied
sudo systemctl restart iscsid

sudo modprobe dm_multipath
sudo mpathconf --enable

echo "devices {
        device {
                vendor                 "LIO-ORG"
                product                "LIO-ORG"
                hardware_handler       "1 alua"
                path_grouping_policy   "failover"
                path_selector          "queue-length 0"
                failback               60
                path_checker           tur
                prio                   alua
                prio_args              exclusive_pref_bit
                fast_io_fail_tmo       25
                no_path_retry          queue
        }
}" | sudo tee /etc/multipath.conf

sudo systemctl start multipathd
