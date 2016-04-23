#!/bin/dash
# Helper script for rfm: Display DICOM header info
# Replace non-printable characters in the current locale with '?' using tr
#
# Copyright (c) 2012 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.
#
if [ -x /usr/local/bin/dcmdump ]; then
   /usr/local/bin/dcmdump -M "$1" | tr -c '[:print:][:cntrl:]' '?'
	exit 0
else
   echo "Could not exec dcmdump."
	exit 1
fi
