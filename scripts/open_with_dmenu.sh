#!/bin/sh
# Helper script for rfm: Open a file with a program selected via dmenu
# Requires dmenu: http://tools.suckless.org/dmenu/
# For wayland (sway) also requires bemenu
#
# Copyright (c) 2014 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.

if [ ! -z "$DISPLAY" ]; then
   FONT="terminus:size=12"
   normbordercolor="#444444"
   normbgcolor="#222222"
   normfgcolor="#bbbbbb"
   selbordercolor="#005577"
   selbgcolor="#005577"
   selfgcolor="#eeeeee"
   menuCMD="dmenu -fn $FONT -nb $normbgcolor -nf $normfgcolor -sb $selbgcolor -sf $selfgcolor"
else
   if [ ! -z "$WAYLAND_DISPLAY" ]; then
      menuCMD=bemenu
   else
      echo "ERROR: no window manager found"
      exit 1
   fi
fi

# Show user defined list if exists: config is a list of apps (executable only) to show, each app on a new line
config=${XDG_CONFIG_HOME:-"$HOME/.config"}/dwm/dmenu.lst

if [ -f "$config" ]; then
   pathCMD="cat $config"
else
   if [ -x /usr/bin/dmenu_path ]; then
      pathCMD="dmenu_path"
   else
      pathCMD="ls /usr/bin"
   fi
fi

exe=$($pathCMD | $menuCMD) && exec "$exe" "$@"
