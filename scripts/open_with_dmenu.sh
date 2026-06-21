#!/bin/sh
# Helper script for rfm: Open a file with a program selected via dmenu
# Requires dmenu: http://tools.suckless.org/dmenu/
# ChangeLog
#  04-01-2025: Modified to work properly on wayland
#              For wayland (sway / dwl / hyprland) also requires suitable menu
#  15-08-2025: Swap order of check for $WAYLAND_DISPLAY and $DISPLAY: prefer wayland if using wayland ($DISPLAY is still be defined if using Xwayland)
#  21-06-2026: Use bemenu for wayland. Edit below to change wayland menu and font as required.

# Copyright (c) 2014 Rodney Padgett <rod_padgett@hotmail.com>
# See LICENSE for details.
#
#
# Show user defined list if exists: dmenu.lst is a list of apps (executable only) to show, each app on a new line
config=${XDG_CONFIG_HOME:-"$HOME/.config"}/dwm/dmenu.lst

if [ -f "$config" ]; then
   pathCMD=$(cat $config)
else
   if [ -x /usr/bin/dmenu_path ]; then
      pathCMD=$(/usr/bin/dmenu_path)
   fi
fi
if [ -z "$pathCMD" ]; then
   echo "$0: WARNING: pathCMD is empty! Falling back to ls"
   #pathCMD=$(ls /usr/bin /usr/local/bin | sed -r 's/[^ ]+://g')
   pathCMD=$(ls /usr/bin && ls /usr/local/bin)
fi

if [ ! -z "$WAYLAND_DISPLAY" ]; then
   # Change wayland menu and font as required
   exe=$(echo "$pathCMD" | bemenu --fn "Terminus 20" --hp 10 --list 8 down --center --width-factor 0.6 --border 5 --prompt "Select:") && exec "$exe" "$@"
else
   if [ ! -z "$DISPLAY" ]; then
      FONT="terminus:size=12"
      normbordercolor="#444444"
      normbgcolor="#222222"
      normfgcolor="#bbbbbb"
      selbordercolor="#005577"
      selbgcolor="#005577"
      selfgcolor="#eeeeee"
      exe=$(echo "$pathCMD" | /usr/bin/dmenu -fn $FONT -nb $normbgcolor -nf $normfgcolor -sb $selbgcolor -sf $selfgcolor) && exec "$exe" "$@"
   else
      echo "$0: ERROR: no window manager found"
      exit 1
   fi
fi
