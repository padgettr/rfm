#!/bin/bash
# Show FFT plot of music file and other info
# Requires sox: http://sox.sourceforge.net/

inFile="$1"
outFile=$(basename "$1").png

if [ -x /usr/bin/sox ]; then
	printf "<b>Output from sox stats filter:</b>\n"
	sox "$inFile" --null spectrogram -o "$HOME/$outFile" stats 2>&1
	printf "<i>Spectrogram written to: $HOME/$outFile</i>\n"
	exit 0
fi
ffplay -f lavfi "amovie=$1,showspectrum=mode=combined:color=intensity:slide=1" > /dev/null 2>&1
