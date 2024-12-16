#!/bin/bash
# Helper script for rfm: Display info about input files
#
# Copyright (c) 2012 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.
#
# ChangeLog:
#   29-01-2013: Changed listing method for multiple files.
#   06-03-2014: Added file_path: useful for copying path to a shell command.
#   13-06-2014: Use ls instead of stat
#   09-09-2017: Added ffprobe to display media info
#   12-05-2021: Go back to using stat: some servers have spaces in user and group names.
#   15-03-2023: Use du for file sizes on disk and mtime: format of mtime from stat is not great.
#               filesize of directories now reflects the contents.
#   13-10-2023: Bug fixes; show md5sum for images (allows thumbnail to be located and copied).

if [ "$#" -eq 1 ]; then
   #stat_info=$(stat --printf "\tPerms : <i>%A</i>\n\tOwner : <i>%U</i>\n\tGroup : <i>%G</i>\n\tBytes  : <i>%s</i>\n\tmtime : <i>%y</i>\n" "$1")
   stat_info=$(stat --printf "\tPerms:\t<i>%A</i>\n\tOwner:\t<i>%U</i>\n\tGroup:\t<i>%G</i>\n" "$1")
   file_info=$(file -b "$1" | sed 's/\&/\&amp\;/g; s/</\&lt\;/g; s/>/\&gt\;/g')
   mime_type=$(file -b -i "$1")
   mime_root=$(echo "$mime_type" | cut -d "/" -f 1)
   du_info=$(du --time -hs "$1" | awk '{ printf ("\tdSize:\t<i>%s</i>\n\tmTime:\t<i>%s %s</i>\n",$1,$2,$3) }')
   file_name=$(basename "$1")
   if [ -d "$1" ]; then
      file_path=$(echo "$1")
   else
      file_path=$(dirname "$1")
   fi

   # Show info for some mime types
   case "$mime_root" in
      audio) media_info=$(/usr/bin/ffprobe -hide_banner "$1" 2>&1 | grep Stream) ;;
      video) media_info=$(/usr/bin/ffprobe -hide_banner "$1" 2>&1 | grep Stream) ;;
      image) media_info="\t md5sum: "$(printf "file://$1" | md5sum | cut -d " " -f 1) ;;
      *) media_info="" ;;
   esac

   # Escape pango markup characters in filename: replace & with &amp; < with &lt; and > with &gt;
   file_name_escaped=$(echo "$file_name" | sed 's/\&/\&amp\;/g; s/</\&lt\;/g; s/>/\&gt\;/g')
   file_path_escaped=$(echo "$file_path" | sed 's/\&/\&amp\;/g; s/</\&lt\;/g; s/>/\&gt\;/g')

   printf "<b>Properties for:</b>\n"
   printf "\t<b>$file_name_escaped</b>\n"
   printf "\n"
   printf "<b>Path:</b>\n"
   printf "\t<i>$file_path_escaped</i>\n"
   printf "\n"
   printf "<b>Info:</b>\n"
   printf "$stat_info\n"
   printf "$du_info\n"
   printf "<b>Mime type:</b>\n"
   printf "\t $mime_type\n"
   printf "\n"
   printf "<b>Contents Indicate:</b>\n"
   printf "\t $file_info\n"
   [ ! -z "$media_info" ] && printf "$media_info\n"
else
   printf "Properties for selected items (ls display format)\n"
   printf "\n"
   printf "<tt>\n"
   ls -ahld --time-style="+%d-%m-%Y %H:%M" "$@"
   printf "</tt>\n"
fi
