#!/bin/sh

# A lot of this script came from here:
# http://stackoverflow.com/questions/96882/how-do-i-create-a-nice-looking-dmg-for-mac-os-x-using-command-line-tools

make -C mlvfs

source="install_temp"
title="MLVFS"
finalDMGName="MLVFS.dmg"
size=1024

mkdir "${source}"
cp -R MLVFS.workflow "${source}"
cp -f mlvfs/mlvfs "${source}/MLVFS.workflow/Contents/"
cp LICENSE "${source}"
cp README.md "${source}"

hdiutil create -srcfolder "${source}" -volname "${title}" -fs HFS+ -fsargs "-c c=64,a=16,e=16" -format UDRW -size ${size}k pack.temp.dmg
device=$(hdiutil attach -readwrite -noverify -noautoopen "pack.temp.dmg" | egrep '^/dev/' | sed 1q | awk '{print $1}')
sleep 2
chmod -Rf go-w /Volumes/"${title}"
sync
sync
hdiutil detach ${device}
hdiutil convert "pack.temp.dmg" -format UDZO -imagekey zlib-level=9 -o "${finalDMGName}"
rm -f pack.temp.dmg
rm -R "${source}"