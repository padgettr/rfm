#!/bin/sh
# Helper script for rfm: Display info about input files
#
# Copyright (c) 2012 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.
#
# ChangeLog:
#   29-01-2013: Changed listing method for multiple files.
#   06-03-2014: Added file_path: useful for copying path to a shell command.
#   13-06-2014: Use ls instead of stat
#   02-04-2015: Specify date format for ls

show_info() {
	printf "\tPerms : <i>$1</i>\n"
	# Column 2 is number of hard links; ignore this one!
	printf "\tOwner : <i>$3</i>\n"
	printf "\tGroup : <i>$4</i>\n"
	printf "\tSize  : <i>$5</i>\n"
	printf "\tDate  : <i>$6</i>\n"
   printf "\tmtime : <i>$7</i>\n"
}

if [ $# -eq 1 ]; then
	ls_info=$(ls -ahld --time-style="+%d-%m-%Y %H:%M" "$1")
	file_info=$(file -b "$1" | sed 's/\&/\&amp\;/g; s/</\&lt\;/g; s/>/\&gt\;/g')
	mime_type=$(file -b -i "$1")
	# Escape pango markup characters in filename: replace & with &amp; < with &lt; and > with &gt;
	file_name=$(echo $(basename "$1") | sed 's/\&/\&amp\;/g; s/</\&lt\;/g; s/>/\&gt\;/g')
	if [ -d "$1" ]; then
		file_path=$(echo "$1" | sed 's/\&/\&amp\;/g; s/</\&lt\;/g; s/>/\&gt\;/g')
	else
		file_path=$(echo $(dirname "$1") | sed 's/\&/\&amp\;/g; s/</\&lt\;/g; s/>/\&gt\;/g')
	fi

	printf "<b>Properties for:</b>\n"
	printf "\t<b>$file_name</b>\n"
	printf "\n"
	printf "<b>Path:</b>\n"
	printf "\t<i>$file_path</i>\n"
	printf "\n"
	printf "<b>Info:</b>\n"
	show_info $ls_info
	printf "\n"
	printf "<b>Mime type:</b>\n"
	printf "\t $mime_type\n"
	printf "\n"
	printf "<b>Contents Indicate:</b>\n"
	printf "\t $file_info\n"
else
	dir_name=$(dirname "$1")
	cd "$dir_name"

	printf "Properties for selected items\n"
	printf "\n"
	for file in "$@"; do
		ls -hld "$(basename "$file")"
	done
fi
