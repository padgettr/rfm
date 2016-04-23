#!/bin/dash

inFile="$1"
outFile=$(basename "$1").png

printf "<b>Output from sox stats filter:</b>\n"
sox "$inFile" --null spectrogram -o "$HOME/$outFile" stats 2>&1
printf "<i>Spectrogram written to: $HOME/$outFile</i>\n"
exit 0
