#!/bin/bash

# Take etc config files from local source tree and upload to both SCs. Doesn't restart any daemons

separator="-------------------------------------------------------------------------------"
ETC_SRC=../config_files/

sc1_ip=192.168.1.137
sc2_ip=192.168.1.138

echo $separator
echo "SC1:"
echo $separator

echo "Uploading chrony.conf..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/chrony.conf starcam@$sc1_ip:/etc/chrony/
# echo "Uploading netplan config..."
# rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/00-installer-config_sc1.yaml starcam@$sc1_ip:/etc/netplan/
echo "Uploading rc.local config for lens controller..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/rc.local starcam@$sc1_ip:/etc/
echo "Uploading ids_peak.conf config for vendor library runtime linking..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/ids_peak.conf starcam@$sc1_ip:/etc/ld.so.conf.d/
echo "Uploading flight software script..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/start_sc_soft.sh starcam@$sc1_ip:/usr/local/sbin/
echo "Uploading flight software service..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/starcam_software.service starcam@$sc1_ip:/etc/systemd/system/
echo "Uploading astrometry.net config file..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/astrometry.cfg starcam@$sc1_ip:/usr/local/astrometry/etc/

echo $separator
echo "SC2:"
echo $separator

echo "Uploading chrony.conf..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/chrony/starcam/chrony.conf starcam@$sc2_ip:/etc/chrony/
# echo "Uploading netplan config..."
# rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/netplan/00-installer-config_sc2.yaml starcam@$sc2_ip:/etc/netplan/
echo "Uploading rc.local config for lens controller..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/rc.local starcam@$sc2_ip:/etc/
echo "Uploading ids_peak.conf config for vendor library runtime linking..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/ids_peak.conf starcam@$sc2_ip:/etc/ld.so.conf.d/
echo "Uploading flight software script..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/start_sc_soft.sh starcam@$sc2_ip:/usr/local/sbin/
echo "Uploading flight software service..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/systemd/starcam_software.service fc1user@$sc2_ip:/etc/systemd/system/
echo "Uploading astrometry.net config file..."
rsync -avz --rsync-path="sudo rsync" --delete $ETC_SRC/astrometry.cfg starcam@$sc2_ip:/usr/local/astrometry/etc/
