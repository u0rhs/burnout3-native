#!/bin/sh
# add-to-menu.sh -- puts "Burnout 3 (port test build)" into your desktop's application menu (and so into Steam's
# "Add a Non-Steam Game" list): a menu entry for the b3launcher in this folder, with the B3 icon.
#   ./add-to-menu.sh            add (or update, after moving this folder)
#   ./add-to-menu.sh --remove   take it out again
# It writes only ~/.local/share/applications/b3port.launcher.desktop (or under $XDG_DATA_HOME); no root needed.
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
APPS=${XDG_DATA_HOME:-$HOME/.local/share}/applications
ENTRY=$APPS/b3port.launcher.desktop
if [ "${1:-}" = --remove ]; then
    rm -f "$ENTRY"
    ICONS=${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor
    rm -f "$ICONS"/*/apps/burnout3-native.png
    command -v gtk-update-icon-cache > /dev/null 2>&1 && [ -f "$ICONS/icon-theme.cache" ] && gtk-update-icon-cache -f -t "$ICONS" > /dev/null 2>&1 || true
    command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database "$APPS" > /dev/null 2>&1 || true
    echo "Removed $ENTRY"
    exit 0
fi
[ -x "$DIR/b3launcher" ] || { echo "No b3launcher in $DIR (keep this script in the unpacked folder)" >&2; exit 1; }
case "$DIR" in
    *'"'* | *'\'* | *'`'* | *'$'* | *'%'*)
        echo "The folder's path has a character a menu entry cannot hold (\" \\ \` \$ %): move the folder" >&2; exit 1;;
esac
mkdir -p "$APPS"
# the icon: this folder's icons/hicolor tree into the user's icon theme (Icon=burnout3-native, found by name)
ICONS=${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor
if [ -d "$DIR/icons/hicolor" ]; then
    mkdir -p "$ICONS"
    cp -r "$DIR/icons/hicolor/." "$ICONS/"
    command -v gtk-update-icon-cache > /dev/null 2>&1 && [ -f "$ICONS/icon-theme.cache" ] && gtk-update-icon-cache -f -t "$ICONS" > /dev/null 2>&1 || true
fi
# Exec / Path with this folder's absolute path (quoted: spaces are fine; | and & escaped for sed)
D=$(printf '%s' "$DIR" | sed 's/[|&]/\\&/g')
sed -e "s|^Exec=.*|Exec=\"$D/b3launcher\"|" -e "/^Icon=/a Path=$D" -e '/^#/d' "$DIR/burnout3-native.desktop" > "$ENTRY.tmp"
chmod 755 "$ENTRY.tmp"
mv "$ENTRY.tmp" "$ENTRY"
command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database "$APPS" > /dev/null 2>&1 || true
echo "Added $ENTRY"
echo "(\"Burnout 3 (port test build)\" in your application menu; run this again if you move the folder)"
