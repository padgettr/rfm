#!/bin/bash
# Mount/Unmount script for sudo mounts
# If the mount is of type CIFS, execute mount from xterm in case there are
# passwords to be entered. Otherwise, just run mount.
# Requires sudo and an entry in the sudoers file:
# visudo
# <userName> <host>=NOPASSWD:/bin/mount /smb/*
# <userName> <host>=NOPASSWD:/bin/umount /smb/*

#
# R. Padgett (2009) <rod_padgett@hotmail.com>
#
# Change log:
#
#   08-03-2010: Simplified to remove Xdialog dependence - password option to mount.cifs stopped working
#   19-04-2010: Mounts cifs filesystems using sudo; cifs will no longer allow SUID.
#   10-08-2010: Added -w option for grep to match whole words only.
#   03-08-2011: Added unmount option -u & some error checking to exit with proper exit codes.
#   28-08-2014: Correct a small bug in exit codes for cifs handling: zero was returned if cifs umount failed.
#   15-02-2015: Changed for use with wayland
#   10-06-2016: Add support for unmounting simple-mtpfs -> read /proc/mounts if path not found in fstab; don't exit on fail, try to umount anyway.

if [ $# -eq 0 ]; then
	echo "Usage: $0 [-u] <mount point>"
	exit 1
fi

uFlag=0
if [ "$1" = "-u" ]; then
        uFlag=1
        shift 1
fi

mountDir="${1%/}"   # Strip off any trailing /
fsType=$(grep -w -F "$mountDir" /etc/fstab |  awk '{print $3 }')
if [ -z "$fsType" ]; then
   fsType=$(grep -w -F "$mountDir" /proc/mounts |  awk '{print $3 }')
   uFlag=1
fi

if [ "$fsType" = "cifs" ]; then
	if [ $uFlag -eq 1 ]; then
		sudo umount "$mountDir"
	else
		if [ -z $DISPLAY ]; then
			vte-2.91 --no-toolbar \
                	      --geometry 80x5 \
                	      -c "sudo mount $mountDir" > /dev/null 2>&1
		else
	                xterm -class Dialog \
	                      -geometry 80x5 \
	                      -title "Mount: Password" \
	                      -e "sudo mount $OPTS $mountDir || { echo 'Press <return> to continue'; read -r $STUFF; }"
		fi
		mount | grep -w "$mountDir" >/dev/null 2>&1
	fi
	exit $?
fi

if [ "$fsType" = "fuse.simple-mtpfs" ]; then
   if [ $uFlag -eq 1 ]; then
      fusermount -u "$mountDir"
   fi
   exit $?
fi

if [ $uFlag -eq 1 ]; then
	umount "$mountDir"
else
	mount "$mountDir"
fi
