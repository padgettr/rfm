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
#   14-01-2017: Changed mtpfs support to umount any fuse filesystem (tested with sshfs)
#   03-03-2018: Added support for sshfs; remove wayland stuff and use xterm
#               note if the shell is not specified as interactive (-i option to bash) filesystem is unmounted when the Dialog shell is closed.
#               This happens because sshfs receives the HUP signal when xterm exits; can be prevented by using nohup command instead of using bash -i -c,
#               but this will redirect stdout to a file.

if [ $# -eq 0 ]; then
   echo "Usage: $0 [-u] <mount point>"
   exit 1
fi

if [ -z $DISPLAY ]; then
   echo "X DISPLAY unset!"
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
      xterm -class Dialog \
            -geometry 80x5 \
            -title "Mount: Password" \
            -e "sudo mount $mountDir || { echo 'Press <return> to continue'; read -r $STUFF; }"
      mount | grep -w "$mountDir" >/dev/null 2>&1
   fi
   exit $?
fi

if [ "$fsType" = "sshfs" ]; then
   if [ $uFlag -eq 1 ]; then
      fusermount3 -u "$mountDir"
   else
      xterm -class Dialog \
            -geometry 80x5 \
            -title "Mount: Password" \
            -e bash --norc -i -c "mount $mountDir || { echo 'Press <return> to continue'; read -r $STUFF; }"
      mount | grep -w "$mountDir" >/dev/null 2>&1
   fi
   exit $?
fi

if [ "${fsType%.*}" = "fuse" ]; then
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
