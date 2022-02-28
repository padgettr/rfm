#!/bin/bash
# Create a tar.gz archive of the selected directory
# Call from filemanager
# R. Padgett (2009) <rod_padgett@hotmail.com>
#
# Changelog:
# 27-10-2016: For security reasons, don't create archive if $tmpDirName exists
#             Added random code to directory name
# 16-07-2020: Add zstd as compression format; add in ability to easily change.
#             Default compression options are used. To add comression options pipe tar through compression program, e.g. to use autodetected number of threads in zstd:
#              tar -C "$path" -cf - "$filename" | zstd -T0 -q -f -o "$tmpDirName/tmp.$aType"

# Set compression extension: gz - gzip; xz - xz; zst - zstd
cExt="gz"

# Set how messages are shown
show_msg() {
   gxmessage -fn monospace "$1"
}

pname=$(basename "$0")
if [ ! -d "$1" ]; then
   show_msg "$pname: Directory expected!"
   exit 1
fi

rndCode=$(head -10 /dev/urandom | md5sum | cut -d " " -f 1)
tmpDirName="/tmp/createArchive.$$.$rndCode"
filename=$(basename "$1")
path=$(dirname "$1")
aType="tar.$cExt"

if [ -e "$tmpDirName" ]; then
   show_msg "$pname: $tmpDirName exists!"
   exit 1
fi

mkdir "$tmpDirName"
chmod 700 "$tmpDirName"

case "$cExt" in
   gz)  CMP_TYPE="--gz" ;;
   xz)  CMP_TYPE="--xz" ;;
   zst) CMP_TYPE="--zstd" ;;
   *) CMP_TYPE="--zstd" ;;
esac

tar -C "$path" $CMP_TYPE -vcf "$tmpDirName/tmp.$aType" "$filename" > "$HOME/$pname_$filename.log" 2>&1

mv "$tmpDirName/tmp.$aType" "$path/$filename.$aType"
if [ $? -gt 0 ]; then
   mv "$tmpDirName/tmp.$aType" "$HOME/$filename.$aType" &&
   show_msg "$pname: Created archive $HOME/$filename.$aType" ||
   show_msg "$pname: ERROR: Archive created: $tmpDirName, but move failed."
fi
rm -rf "$tmpDirName"
