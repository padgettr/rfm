#!/bin/dash
# Create a tar.bz2 archive of the selected directory
# Call from filemanager
# R. Padgett (2009) <rod_padgett@hotmail.com>
#
# Changelog:
# Modified to create tar.xz files instead. Added variable aType.
# Added "2>&1" to tar command to ensure errors are also piped to xmessage
# Standardised error detection, i.e. test $?
# Converted to be POSIX compliant for dash - also fixed a small bug
# where if a directory was passed as /path/to/dir/ the archive file
# was moved to /path/to/dir/.aType, due to using $1 directly. Modified to
# use $path/$filename.$aType instead.
#
# 27-10-2016: For security reasons, don't create archive if $tmpDirName exists
#             Added random code to directory name
#
pname=$(basename "$0")
if [ ! -d "$1" ]; then
   xmessage "$pname: Directory expected!"
   exit 1
fi

rndCode=$(head -10 /dev/urandom | md5sum | cut -d " " -f 1)
tmpDirName="/tmp/createArchive.$$.$rndCode"
filename=$(basename "$1")
path=$(dirname "$1")
aType="tar.xz"

if [ -e "$tmpDirName" ]; then
	xmessage "$pname: $tmpDirName exists!"
   exit 1
fi

mkdir "$tmpDirName"
chmod 700 "$tmpDirName"

tar -C "$path" -vcJf "$tmpDirName/tmp.$aType" "$filename" 2>&1 | xmessage -file -
mv "$tmpDirName/tmp.$aType" "$path/$filename.$aType"
if [ $? -gt 0 ]; then
	mv "$tmpDirName/tmp.$aType" "$HOME/$filename.$aType" &&
	xmessage "$pname: Created archive $HOME/$filename.$aType" ||
	xmessage "$pname: ERROR: Archive created: $tmpDirName, but move failed."
fi
rm -rf "$tmpDirName"
