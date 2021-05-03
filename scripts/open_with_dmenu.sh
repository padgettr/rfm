#!/bin/bash
# Helper script for rfm: Open a file with a program selected via dmenu
# Requires dmenu: http://tools.suckless.org/dmenu/
#
# Copyright (c) 2014 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.

FONT="terminus:size=12"
normbordercolor="#444444";
normbgcolor="#222222";
normfgcolor="#bbbbbb";
selbordercolor="#005577";
selbgcolor="#005577";
selfgcolor="#eeeeee";

#config=${XDG_CONFIG_HOME:-"$HOME/.config"}/dwm/dmenu.lst
#exe=$(cat $config | dmenu -fn $FONT -nb $normbgcolor -nf $normfgcolor -sb $selbgcolor -sf $selfgcolor) && exec $exe "$@"

exe=$(dmenu_path | dmenu -fn "$FONT" -nb "$normbgcolor" -nf "$normfgcolor" -sb "$selbgcolor" -sf "$selfgcolor") && exec "$exe" "$@"
