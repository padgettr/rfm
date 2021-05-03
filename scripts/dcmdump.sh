#!/bin/bash
# Helper script for rfm: Display DICOM header info
# Requires dicom toolkit from offis: https://dicom.offis.de/dcmtk.php.en
# Replace non-printable characters in the current locale with '?' using tr
#
# Copyright (c) 2012 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.
# ChangeLog
# 21-08-2014: Added LANG=c, otherwise get an error
# 19-02-2018: Added dsrdump; if file is dicom dose SR use this rather than dcmdump
# 02-04-2020: Improved logic; remove dsrdump and replace with filtered dcmdump tree view
LANG=c
if [ ! -x /usr/local/bin/dcmdump ]; then
   echo "Can't exec /usr/local/bin/dcmdump!"
   exit 1
fi

SOP_CLASS_UID=$(dcmdump +T -M -q --search "0008,0016" --search-first "$1" | cut -d'=' -f2)

case "$SOP_CLASS_UID" in
   XRayRadiationDoseSRStorage)
#     /usr/local/bin/dsrdump "$1" | tr -c '[:cntrl:][:print:]' '?'
      /usr/local/bin/dcmdump +T -M -q --search "fffe,e000" "$1" | tr -c '[:cntrl:][:print:]' '?'
   ;;
   *)
      /usr/local/bin/dcmdump -M "$1" | tr -c '[:cntrl:][:print:]' '?'
   ;;
esac

exit 0

