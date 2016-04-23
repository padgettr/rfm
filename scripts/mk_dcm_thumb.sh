#!/bin/dash
#
# Generate DICOM image thumbnails using the dicom toolkit from OFFIS (dcmtk)
#
# Copyright (c) 2009-2011 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.
#
# Call script as $0 /path/to/source/file /path/to/thumbnail thumbnail_size
# Thumbnails are generated according to the freedesktop thumbnail specification
# (http://jens.triq.net/thumbnail-spec/index.html) version 0.7.0
# Specifically:
#     	 * Maximum thumbnail size for the ~/.thumbnails/normal directory is 128x128 pixels
#          Note: The aspect ratio should be preserved but scale so that maximum dimension is 128.
#     	 * The following keys and their appropriate values must be provided by every program
#     	   which supports this standard -
#     	    Thumb::URI	The absolute canonical uri for the original file. (eg file:///home/jens/photo/me.jpg)
#     	    Thumb::MTime	The modification time of the original file (as indicated by stat, which is represented as seconds since January 1st, 1970).
#     	 * If MTime is not available, no thumbnail should be stored.
#     	 * To prevent two programs creating the same thumbnail at the same time, write to a temporary filename, then move
#     	 * The files should be chmod 600
#
# NOTE: using convert with -thumbnail option results in thumbnails which will
#       NOT load into thunar. Use -scale instead.
# ChangeLog:
# 23-02-2012: changed shell to dash (faster)
# 20-05-2014: Add convert option: -resize $thumbSize -gravity center -extent $thumbSize
#             to ensure that number of columns in the image is not greater than $3
#

dcmtk_path="/usr/local/bin"
pname=$(basename $0)

if [ ! -x $dcmtk_path/dcmj2pnm ]; then
   echo "$pname: dcmj2pnm not found!"
   exit 1
fi

size=$3
mtime=$(stat --format=%Y "$1")
if [ "x$mtime" = x ]; then
   echo "$pname: No mtime available for $1!"
   exit 1
fi

thumbSize=x$size
thumbSize=$size$thumbSize

$dcmtk_path/dcmj2pnm --scale-y-size $thumbSize --min-max-window-n --write-jpeg "$1" | \
   convert jpg:- -type TrueColor \
      -resize $thumbSize -gravity center -extent $thumbSize \
      -set "Thumb::MTime" $mtime \
      -set "Thumb::URI" "file://$1" "png:$2.$$" > /dev/null 2>&1

if [ $? -gt 0 ]; then
   echo "$pname: Can't generate thumbnail for $1."
   exit 1
fi

mv "$2.$$" "$2"
chmod 600 "$2"
