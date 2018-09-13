#!/bin/dash
#
# Generate DICOM image thumbnails using the dicom toolkit from OFFIS (dcmtk)
#
# Copyright (c) 2009-2018 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.
#
# DEPENDENCY: This script *requires* image magick (https://www.imagemagick.org/) and optionally
# the DICOM toolkit, dcmtk (http://git.dcmtk.org/)
#
# Call script as $0 /path/to/source/file /path/to/thumbnail/file thumbnail_size
# Thumbnails are generated according to the freedesktop thumbnail specification
# (http://jens.triq.net/thumbnail-spec/index.html) version 0.7.0
# Specifically:
#     	 * Maximum thumbnail size for the ~/.thumbnails/normal directory is 128x128 pixels
#          Note: The aspect ratio should be preserved but scale so that maximum dimension is 128.
#     	 * The following keys and their appropriate values must be provided by every program
#     	   which supports this standard -
#     	    Thumb::URI   The absolute canonical uri for the original file. (eg file:///home/jens/photo/me.jpg)
#     	    Thumb::MTime The modification time of the original file (as indicated by stat, which is represented as seconds since January 1st, 1970).
#     	 * If MTime is not available, no thumbnail should be stored.
#     	 * To prevent two programs creating the same thumbnail at the same time, write to a temporary filename, then move
#     	 * The files should be chmod 600
#
# NOTE: using convert with -thumbnail option results in thumbnails which will
#       NOT load into thunar. Use -scale instead.
# ChangeLog:
# 23-02-2012: changed shell to dash (faster)
# 20-05-2014: Add convert option: -resize $THUMB_SIZE -gravity center -extent $THUMB_SIZE
#             to ensure that number of columns in the image is not greater than $3
# 13-03-2018: If dcmtk not available, fall back on image magick: compressed DICOM not supported in that case!
#             Correct some dodgy syntax, e.g. missing quotes, variable names.
#
DCMTK_PATH="/usr/local/bin"
PNAME="$(basename $0)"


if [ ! -f "$1" ]; then
   echo "$PNAME: File path: $1; not found!"
   exit 1
fi

if [ -d "$2" ]; then
   echo "$PNAME: Thumb path: $2; file name required!"
   exit 1
fi

THUMB_SIZE="x$3"
THUMB_SIZE="$size$THUMB_SIZE"

MTIME="$(stat --format=%Y "$1")"
if [ -z "$MTIME" ]; then
   echo "$PNAME: No mtime available for $1!"
   exit 1
fi

if [ ! -x $DCMTK_PATH/dcmj2pnm ]; then
   convert "$1" -type TrueColor \
      -resize $THUMB_SIZE -gravity center -extent $THUMB_SIZE \
      -set "Thumb::MTime" $MTIME \
      -set "Thumb::URI" "file://$1" "png:$2.$$" > /dev/null 2>&1
else
   $DCMTK_PATH/dcmj2pnm --scale-y-size $THUMB_SIZE --min-max-window-n --write-jpeg "$1" | \
   convert jpg:- -type TrueColor \
      -resize $THUMB_SIZE -gravity center -extent $THUMB_SIZE \
      -set "Thumb::MTime" $MTIME \
      -set "Thumb::URI" "file://$1" "png:$2.$$" > /dev/null 2>&1
fi

if [ $? -gt 0 ]; then
   echo "$PNAME: Can't generate thumbnail for $1."
   exit 1
fi

mv "$2.$$" "$2"
chmod 600 "$2"
