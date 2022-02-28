#!/bin/bash
# Mount/Unmount script for rfm
# cifs mounts are handles through sudo:
# Requires sudo and an entry in the sudoers file, e.g. if all cifs mounts are
# in /smb the following can be added to sudoers file (using visudo)
# to allow mounting and unmounting:
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
#  02-04-2019: fsType: use cut to ignore comments in fstab, and alter awk command to use either space or TAB or both as delimiter
#  24-07-2019: If the supplied mount point is a block device, use udisks2 to mount and unmount.
#              Use $XDG_VTNR to check if user is at the terminal or logged in via network (may be ssh or remote X)
#              Add execMountCmd() function to avoid repetition of the password dialog code
#              Apparantly some devices need to be powered down before they can be safely removed. Add -s option to umount and power down - only applies to device paths!
# 16-11-2019:  Allow any user to mount cifs (added user options to mount: -o user=$USER,uid=$USER,gid=users
# 26-06-2021:  Add in a wayland capable terminal for passwords if $WAYLAND_DISPLAY is defined.

execMountCmd() {
   MNT_CMD="bash --norc -i -c \"$1 || { echo 'Press <return> to continue'; read -r $STUFF; }\""
   if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
      bash --norc -i -c "$1"
   else
      if [ ! -z "$WAYLAND_DISPLAY" ] || [ ! -x /usr/bin/xterm ] ; then
         #alacritty --class Dialog --title "Mount: Password" --option window.columns=80 window.lines=5 -e $MNT_CMD
         vte-2.91 --name Dialog-vte --extra-margin 10 --cursor-blink=off --cursor-background-color=red --cursor-foreground-color=white \
            --background-color=black --foreground-color=white --no-decorations --no-scrollbar \
            --geometry=80x5 --command="$MNT_CMD"
      else
         xterm -class Dialog \
            -geometry 80x5 \
            -title "Mount: Password" \
            -e "$MNT_CMD"
      fi
   fi
}

if [ $# -eq 0 ]; then
   echo "Usage: $0 [-u -s] <mount point || block device>"
   echo "Options:"
   echo "   -u   unmount the specified mount point or block device"
   echo "   -s unmount and power down the specified mount point or block device"
   exit 1
fi

uFlag=0   # unmount flag
if [ "$1" = "-u" ]; then
   uFlag=1
   shift 1
fi

sFlag=0   # unmount and power down: eject / safely remove flag
if [ "$1" = "-s" ]; then
   sFlag=1
   shift 1
fi

mType=$(stat -L --format=%F "$1")
[ $? -ne 0 ] && exit 1

case "$mType" in
   "block special file")
      mountDev="$1"
      if [ $uFlag -eq 1 ]; then
         /usr/bin/udisksctl unmount --no-user-interaction -b "$mountDev"
         exit $?
      fi

      if [ $sFlag -eq 1 ]; then
         /usr/bin/udisksctl unmount --no-user-interaction -b "$mountDev" > /dev/null 2>&1
         if [ -z $XDG_VTNR ]; then
            execMountCmd "/usr/bin/udisksctl power-off -b $mountDev" # NOTE: udisksctl command is detach in older versions of udisks
         else
            /usr/bin/udisksctl power-off --no-user-interaction -b "$mountDev"
         fi
         exit $?
      fi

      if [ -z $XDG_VTNR ]; then
         execMountCmd "/usr/bin/udisksctl mount -b $mountDev"
         /usr/bin/findmnt -m "$mountDev" > /dev/null
      else
         /usr/bin/udisksctl mount --no-user-interaction -b "$mountDev"
      fi
      exit $?
   ;;

   "directory")
      if [ $sFlag -eq 1 ]; then
         echo "WARNING: umount only: NOT powering device down: device path required."
         uFlag=1
      fi

      mountDir="${1%/}"   # Strip off any trailing /

      # Ignore comments, search fstab and return 3rd field (file system type)
      fsType=$(cut -d \# -f 1 /etc/fstab | grep -w -F "$mountDir" |  awk -F "[ \t]*|[ \t]+" '{print $3}')
      if [ -z "$fsType" ]; then
         fsType=$(grep -w -F "$mountDir" /proc/mounts |  awk '{print $3 }')
         uFlag=1
      fi
   ;;
   *)
      echo "File type $mType not supported for mounting"
      exit 1;
   ;;
esac

# A mount point was specified
if [ "$fsType" = "cifs" ]; then
   if [ $uFlag -eq 1 ]; then
      sudo umount "$mountDir"
   else
      execMountCmd "sudo mount $mountDir -o user=$USER,uid=$USER,gid=users"
      mount | grep -w "$mountDir" >/dev/null 2>&1
   fi
   exit $?
fi

if [ "$fsType" = "sshfs" ]; then
   if [ $uFlag -eq 1 ]; then
      fusermount3 -u "$mountDir"
   else
      execMountCmd "mount $mountDir"
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
