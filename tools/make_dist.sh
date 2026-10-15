#!/bin/bash
# Builds D:\tlg-pc\dist: the program, a clean "user" profile (settings, bindings, button
# textures; no saves, logs, screenshots or dumps), shortcuts installer, README and icon.
# Usage: tools/make_dist.sh   (after build.bat; takes bin/tlg.exe)
set -e
cd /d/tlg-pc
out=dist
name="The Last Guardian"
rm -rf "$out"
mkdir -p "$out/user"
cp bin/tlg.exe "$out/$name.exe"
printf 'D:\\Games\\CUSA03627\\eboot.bin' > "$out/game_path.txt"
for d in captures cheats custom_configs custom_modules custom_trophy data download fonts \
         game_data licenses patches shader sys_modules temp trophy; do
    mkdir -p "$out/user/$d"
done
cp -r tools/dist_user/. "$out/user/"
mkdir -p "$out/user/sotc_textures"
cp -r testrun/user/sotc_textures/keyboard testrun/user/sotc_textures/xbox "$out/user/sotc_textures/"
cp tools/dist/* "$out/"
tools/venv/Scripts/python -c "
from PIL import Image
Image.open('D:/Games/CUSA03627/sce_sys/icon0.png').save('$out/game.ico',
    sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])"
du -sh "$out"
