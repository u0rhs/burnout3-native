#!/bin/bash
# Builds b3view.exe, b3replay.exe (also the live renderer: b3replay --live, §13), b3assetdump.exe and b3modtool.exe (the mod
# kit's asset tool, docs/modding/b3modtool.md) for Windows (docs/native-renderer.md §10) with the verified MSYS2 toolchain
# (tools/toolchain/toolchain.sha256) through cmd.exe, against SDL3 3.4.2 from the MSYS2 package
# mingw-w64-x86_64-sdl3-3.4.2-1 (unpacked into ../ext/sdl3-3.4.2-mingw64; hash in tools/render/README.md).
# Output: $WIN/b3view/{b3view.exe,b3replay.exe,b3assetdump.exe,b3modtool.exe,SDL3.dll,b3render.dll (the in-process renderer, §23),
# Burnout3Launcher.exe (the setup program beside the game, src/b3launcher; remaster-features §8)} (Windows side, outside
# the repo). Builds only; runs nothing. tools/native/build_win.sh RENDER=1 builds them into native_play.exe's folder.
#   tools/render/build_win.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../.." && pwd)
. "$PORT/tools/local_env.sh"; b3_local_env "$PORT"   # the machine's paths (local.env)
WIN=${WIN:-$PORT/build-win}   # local.env: where the Windows toolchain (toolchain/mingw64) and outputs live
TC=$WIN/toolchain/mingw64
SDL=${SDL3_MINGW:-$(dirname "$PORT")/ext/sdl3-3.4.2-mingw64/mingw64}
OUT=${OUT:-$WIN/b3view}   # OUT=<dir>: another output folder (a worktree build must not replace the main b3view.exe)
ST=$OUT/stage

(cd "$TC" && sha256sum --quiet -c "$PORT/tools/toolchain/toolchain.sha256") || { echo "toolchain does not match toolchain.sha256" >&2; exit 1; }
echo "12a874d28420f436d6bb43edc69ce579b4d4c85ec5a239e5f1e8fe49fe5f615d  ${MSYS2_PKGS:-$(dirname "$PORT")/ext/msys2-pkgs}/mingw-w64-x86_64-sdl3-3.4.2-1-any.pkg.tar.zst" \
  | sha256sum --quiet -c || { echo "SDL3 MSYS2 package hash mismatch" >&2; exit 1; }
python3 -I -c 'import shutil,sys; shutil.rmtree(sys.argv[1], ignore_errors=True)' "$ST"
mkdir -p "$ST/sdl3" "$ST/obj"
cp -r "$PORT/include" "$ST/"
mkdir -p "$ST/src" "$ST/tools"
cp -r "$PORT/src/b3asset" "$PORT/src/b3gfx" "$PORT/src/b3view" "$PORT/src/b3replay" "$PORT/src/b3settings" "$ST/src/"
cp "$PORT/tools/render/b3assetdump.cpp" "$ST/tools/"
cp -r "$PORT/src/gameview" "$ST/src/"; cp "$PORT/tools/native/gameview_replay.cpp" "$ST/tools/"   # pic1
mkdir -p "$ST/src/native"; cp "$PORT/src/native/input_keys.h" "$PORT/src/native/input_keys.cpp" "$PORT/src/native/input_report.cpp" "$PORT/src/native/input_bindings.h" "$PORT/src/native/input_bindings.cpp" "$PORT/src/native/input_keys_sdl.h" "$PORT/src/native/pad_actuator.h" "$PORT/src/native/pad_gyro.h" "$PORT/src/native/pad_triggers.h" "$PORT/src/native/audio_device.h" "$PORT/src/native/audio_device.cpp" "$PORT/src/native/sha256.h" "$ST/src/native/"   # pad1: pad_gyro.h, pad_triggers.h; sdk2b: sha256.h (b3mod/codemod.cpp); play1: the window as the native game's pad
# mac5: the launcher (src/b3launcher, with stb_truetype in third_party) and the save files it shows / imports / exports
cp -r "$PORT/src/b3launcher" "$ST/src/"
cp -r "$PORT/src/b3mod" "$ST/src/"   # modkit1: the launcher's Mods page (include/b3mod/mods.h)
cp -r "$PORT/src/b3modtool" "$ST/src/"; cp "$PORT/tools/modding/b3modtool.cpp" "$ST/tools/"   # modtool1: b3modtool.exe
# test builds: the bug report (src/b3report: the ZIP writer, the report, the system description) and the version stamp
cp -r "$PORT/src/b3report" "$ST/src/"
"$PORT/tools/release/version_info.sh" "$ST/src/b3report/version_info.cpp"
cp "$PORT"/src/native/save_card.* "$PORT"/src/native/save_formats.* "$PORT"/src/native/save_store.* "$PORT"/src/native/save_stats.* "$PORT"/src/native/replay_file.h "$ST/src/native/"   # replay1: the Replays page
# the launcher's Music page: the custom soundtrack's folder scan and tags (src/native/custom_music.h, its decoders)
cp "$PORT"/src/native/custom_music.* "$PORT"/src/native/music_decoders.cpp "$ST/src/native/"; cp -r "$PORT/src/native/third_party" "$ST/src/native/"
cp "$PORT"/src/native/now_playing* "$PORT"/src/native/dbus_dl.h "$ST/src/native/"   # nowplaying1: the Music page's OTHER MUSIC APPS line
cp -r "$SDL/include" "$ST/sdl3/"
cp "$SDL/lib/libSDL3.dll.a" "$SDL/lib/libSDL3.a" "$ST/sdl3/"

WTC=$(wslpath -w "$TC")
printf '@echo off\r\nset "PATH=%s\\bin;%%SystemRoot%%\\system32;%%SystemRoot%%"\r\n"%s\\bin\\%%1" @%%2\r\n' "$WTC" "$WTC" > "$ST/tc.cmd"
WCMD=$(wslpath -w "$ST/tc.cmd")
run() { (cd "$ST" && cmd.exe /c "$WCMD" "$1" "$2" < /dev/null); }
JOBS=${JOBS:-4}   # perf1: compiles at most JOBS at a time (the agents' build rule, -j4)
throttle() { while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done; }
W() { wslpath -m "$1"; }

FLAGS="-std=c++20 -O2 -DNDEBUG -Wall -Wextra -I$(W "$ST/include") -I$(W "$ST/sdl3/include") -I$(W "$ST/src/b3gfx")"
objs=()
for s in "$ST"/src/b3asset/*.cpp "$ST"/src/b3gfx/*.cpp "$ST"/src/b3view/*.cpp "$ST"/src/b3replay/*.cpp "$ST"/src/b3settings/*.cpp "$ST"/src/native/input_keys.cpp "$ST"/src/native/input_report.cpp "$ST"/src/native/input_bindings.cpp "$ST"/tools/b3assetdump.cpp \
         "$ST"/src/b3launcher/*.cpp "$ST"/src/native/save_card.cpp "$ST"/src/native/save_formats.cpp "$ST"/src/native/save_store.cpp "$ST"/src/native/save_stats.cpp \
         "$ST"/src/native/custom_music.cpp "$ST"/src/native/music_decoders.cpp "$ST"/src/native/now_playing.cpp "$ST"/src/native/now_playing_mpris.cpp "$ST"/src/native/now_playing_win.cpp "$ST"/src/b3report/*.cpp "$ST"/src/b3mod/*.cpp "$ST"/src/b3modtool/*.cpp "$ST"/tools/b3modtool.cpp; do   # (b3replay/inproc.cpp: the DLL's only)
  n=$(basename "${s%.cpp}")
  case "$s" in */src/b3replay/*) n=b3replay_$n ;; */src/b3settings/*) n=b3settings_$n ;; */src/b3launcher/*) n=b3launcher_$n ;; */src/b3report/*) n=b3report_$n ;; */src/b3mod/*) n=b3mod_$n ;; */src/b3modtool/*) n=b3modtool_$n ;; esac   # b3replay's main.cpp beside b3view's
  printf '%s\n' $FLAGS -c "$(W "$s")" -o "$(W "$ST/obj/$n.o")" > "$ST/obj/$n.rsp"
  throttle
  run g++.exe "obj/$n.rsp" &
  objs+=("$n")
done
# b3render.dll (inproc1, docs/native-renderer.md §23): b3replay's main.cpp without main(), plus the C ABI
printf '%s\n' $FLAGS -DB3REPLAY_LIBRARY -c "$(W "$ST/src/b3replay/main.cpp")" -o "$(W "$ST/obj/b3render_main.o")" > "$ST/obj/b3render_main.rsp"
throttle
run g++.exe obj/b3render_main.rsp &
objs+=(b3render_main)
wait
for n in "${objs[@]}"; do [ -f "$ST/obj/$n.o" ] || { echo "compile failed: $n" >&2; exit 1; }; done
ASSET=$(for f in "$ST"/src/b3asset/*.cpp; do W "$ST/obj/$(basename "${f%.cpp}").o"; done)
# nofallback1: the shipped programs (b3render.dll, b3modtool.exe, Burnout3Launcher.exe) without the VU interpreter core
# (vu1.cpp: the check tools' reference; nothing in them calls it), as the Makefile's B3ASSET_RT_OBJ
ASSET_RT=$(for f in "$ST"/src/b3asset/*.cpp; do case "$(basename "$f")" in vu1.cpp|particleshade.cpp) ;; *) W "$ST/obj/$(basename "${f%.cpp}").o" ;; esac; done)
# effects3: b3render.dll alone has the VU core (the particle layer runs the game's program 0x46D750 on the captured arrays)
ASSET_DLL="$ASSET_RT $(W "$ST/obj/vu1.o") $(W "$ST/obj/particleshade.o")"
# Static C++ runtime: the exes then need only SDL3.dll (which imports system DLLs only).
LINK="-static"   # libSDL3.dll.a is passed by path, so SDL3 stays a DLL
[ "${STRIP:-0}" = 1 ] && LINK="$LINK -s"   # STRIP=1: no symbols (a release: tools/release/package_win.sh)
printf '%s\n' $(W "$ST/obj/b3gfx_sdl.o") $(W "$ST/obj/scene.o") $(W "$ST/obj/carscene.o") $(W "$ST/obj/crashscene.o") $(W "$ST/obj/sun.o") $(W "$ST/obj/main.o") $ASSET "$(W "$ST/sdl3/libSDL3.dll.a")" \
  $LINK -o "$(W "$OUT/b3view.exe")" > "$ST/obj/b3view.link.rsp"
run g++.exe obj/b3view.link.rsp
printf '%s\n' $(W "$ST/obj/b3gfx_sdl.o") $(W "$ST/obj/scene.o") $(W "$ST/obj/carscene.o") $(W "$ST/obj/crashscene.o") $(W "$ST/obj/sun.o") $(W "$ST/obj/b3replay_capture.o") \
  $(W "$ST/obj/b3replay_effects.o") $(W "$ST/obj/b3replay_coronas.o") $(W "$ST/obj/b3replay_sparks.o") $(W "$ST/obj/b3replay_globe.o") $(W "$ST/obj/b3replay_hud.o") $(W "$ST/obj/b3replay_view.o") \
  $(W "$ST/obj/b3replay_live.o") $(W "$ST/obj/b3replay_livefeed.o") $(W "$ST/obj/b3replay_nativepad.o") $(W "$ST/obj/b3replay_pcpage.o") $(W "$ST/obj/b3replay_nativeaudio.o") $(W "$ST/obj/input_keys.o") $(W "$ST/obj/input_report.o") $(W "$ST/obj/input_bindings.o") $(W "$ST/obj/b3replay_fpspanel.o") $(W "$ST/obj/b3replay_skiphint.o") $(W "$ST/obj/b3replay_menumouse.o") $(W "$ST/obj/b3replay_prompts.o") $(W "$ST/obj/b3replay_glyphdraw.o") $(W "$ST/obj/b3replay_pcsettings.o") $(W "$ST/obj/b3settings_settings.o") $(W "$ST/obj/b3settings_safemode.o") $(W "$ST/obj/b3replay_notice.o") $(W "$ST/obj/b3replay_photomode.o") $(W "$ST/obj/b3replay_photohud.o") $(W "$ST/obj/b3replay_a11y.o") $(W "$ST/obj/b3replay_ghost.o") $(W "$ST/obj/b3replay_speedrun.o") $(W "$ST/obj/b3replay_speedrunpanel.o") $(W "$ST/obj/b3replay_tuneconsole.o") $(W "$ST/obj/b3replay_carlook.o") $(W "$ST/obj/b3replay_carlookpanel.o") $(W "$ST/obj/b3replay_trafficfade.o") $(W "$ST/obj/b3replay_splitlayout.o") $(W "$ST/obj/b3replay_modoverlay.o") $(W "$ST/obj/b3mod_codemod.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3report_bug_report.o") $(W "$ST/obj/b3report_sysinfo.o") $(W "$ST/obj/b3report_version_info.o") $(W "$ST/obj/b3settings_page.o") $(W "$ST/obj/b3settings_menu.o") $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3replay_main.o") $ASSET \
  "$(W "$ST/sdl3/libSDL3.dll.a")" -lz $LINK -o "$(W "$OUT/b3replay.exe")" > "$ST/obj/b3replay.link.rsp"
run g++.exe obj/b3replay.link.rsp
# b3render.dll: SDL3 linked statically (its own SDL inside the game process, beside the kit's SDL3.dll: §23), the C++
# runtime too; it exports only the b3r_ functions (dllexport turns off the export of everything else).
printf '%s\n' -shared $(W "$ST/obj/b3gfx_sdl.o") $(W "$ST/obj/scene.o") $(W "$ST/obj/carscene.o") $(W "$ST/obj/crashscene.o") $(W "$ST/obj/sun.o") $(W "$ST/obj/b3replay_capture.o") \
  $(W "$ST/obj/b3replay_effects.o") $(W "$ST/obj/b3replay_coronas.o") $(W "$ST/obj/b3replay_sparks.o") $(W "$ST/obj/b3replay_globe.o") $(W "$ST/obj/b3replay_hud.o") $(W "$ST/obj/b3replay_view.o") \
  $(W "$ST/obj/b3replay_live.o") $(W "$ST/obj/b3replay_livefeed.o") $(W "$ST/obj/b3replay_nativepad.o") $(W "$ST/obj/b3replay_pcpage.o") $(W "$ST/obj/b3replay_nativeaudio.o") $(W "$ST/obj/input_keys.o") $(W "$ST/obj/input_report.o") $(W "$ST/obj/input_bindings.o") $(W "$ST/obj/b3replay_fpspanel.o") $(W "$ST/obj/b3replay_skiphint.o") $(W "$ST/obj/b3replay_menumouse.o") $(W "$ST/obj/b3replay_prompts.o") $(W "$ST/obj/b3replay_glyphdraw.o") $(W "$ST/obj/b3replay_pcsettings.o") $(W "$ST/obj/b3settings_settings.o") $(W "$ST/obj/b3settings_safemode.o") $(W "$ST/obj/b3replay_notice.o") $(W "$ST/obj/b3replay_photomode.o") $(W "$ST/obj/b3replay_photohud.o") $(W "$ST/obj/b3replay_a11y.o") $(W "$ST/obj/b3replay_ghost.o") $(W "$ST/obj/b3replay_speedrun.o") $(W "$ST/obj/b3replay_speedrunpanel.o") $(W "$ST/obj/b3replay_tuneconsole.o") $(W "$ST/obj/b3replay_carlook.o") $(W "$ST/obj/b3replay_carlookpanel.o") $(W "$ST/obj/b3replay_trafficfade.o") $(W "$ST/obj/b3replay_splitlayout.o") $(W "$ST/obj/b3replay_modoverlay.o") $(W "$ST/obj/b3mod_codemod.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3report_bug_report.o") $(W "$ST/obj/b3report_sysinfo.o") $(W "$ST/obj/b3report_version_info.o") $(W "$ST/obj/b3settings_page.o") $(W "$ST/obj/b3settings_menu.o") $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3render_main.o") $(W "$ST/obj/b3replay_inproc.o") $ASSET_DLL \
  "$(W "$ST/sdl3/libSDL3.a")" -lz $LINK -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 \
  -o "$(W "$OUT/b3render.dll")" > "$ST/obj/b3render.link.rsp"
run g++.exe obj/b3render.link.rsp
printf '%s\n' $(W "$ST/obj/b3assetdump.o") $ASSET $LINK -o "$(W "$OUT/b3assetdump.exe")" > "$ST/obj/b3assetdump.link.rsp"
run g++.exe obj/b3assetdump.link.rsp
# b3modtool.exe (modtool1): a console program, zlib and the C++ runtime static (needs no DLL)
printf '%s\n' $(W "$ST/obj/b3modtool.o") $(for f in "$ST"/src/b3modtool/*.cpp; do W "$ST/obj/b3modtool_$(basename "${f%.cpp}").o"; done) \
  $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3launcher_showroom.o") $ASSET_RT -lz $LINK -o "$(W "$OUT/b3modtool.exe")" > "$ST/obj/b3modtool.link.rsp"   # models3c: the showroom's car names
run g++.exe obj/b3modtool.link.rsp
# gameview_replay.exe (pic1, docs/native-renderer.md §32): a scene capture through the game view's capture code into
# b3render.dll (LoadLibrary), as the native backend drives it
for n in gameview_replay scene_capture hud_capture; do
  case $n in gameview_replay) s=$ST/tools/$n.cpp ;; *) s=$ST/src/gameview/$n.cpp ;; esac
  printf '%s\n' -std=gnu++20 -O2 -DNDEBUG -Wall -Wextra -I$(W "$ST/include") -c "$(W "$s")" -o "$(W "$ST/obj/gv_$n.o")" > "$ST/obj/gv_$n.rsp"
  run g++.exe "obj/gv_$n.rsp"
done
printf '%s\n' $(W "$ST/obj/gv_gameview_replay.o") $(W "$ST/obj/gv_scene_capture.o") $(W "$ST/obj/gv_hud_capture.o") -lz $LINK -o "$(W "$OUT/gameview_replay.exe")" > "$ST/obj/gameview_replay.link.rsp"
run g++.exe obj/gameview_replay.link.rsp
# the programs' icon (assets/icon/b3-icon.ico, tools/release/make_icons.py): a resource object for the launcher
printf '1 ICON "%s"\n' "$(W "$PORT/assets/icon/b3-icon.ico")" > "$ST/obj/b3icon.rc"
printf '%s\n' -O coff -i "$(W "$ST/obj/b3icon.rc")" -o "$(W "$ST/obj/b3icon.res.o")" > "$ST/obj/b3icon.rsp"
run windres.exe obj/b3icon.rsp
# Burnout3Launcher.exe (mac5, remaster-features §8): a windows-subsystem program (no console; SDL_main.h's WinMain), the
# C++ runtime static, SDL3.dll beside it as for the others; photo2: its SHOWROOM page draws with b3gfx and b3view's car scene;
# update1: WinHTTP (a system DLL) for the update check
printf '%s\n' -mwindows $(W "$ST/obj/b3icon.res.o") $(W "$ST/obj/b3launcher_main.o") $(W "$ST/obj/b3launcher_showroom.o") $(W "$ST/obj/b3launcher_showroom_view.o") $(W "$ST/obj/b3gfx_sdl.o") $(W "$ST/obj/scene.o") $(W "$ST/obj/carscene.o") $(W "$ST/obj/b3launcher_core.o") $(W "$ST/obj/b3launcher_update.o") $(W "$ST/obj/b3launcher_update_net.o") $(W "$ST/obj/b3launcher_spawn.o") $(W "$ST/obj/b3launcher_gameart.o") \
  $(W "$ST/obj/b3launcher_font.o") $(W "$ST/obj/b3launcher_draw.o") $(W "$ST/obj/b3settings_settings.o") $(W "$ST/obj/b3settings_page.o") $(W "$ST/obj/b3settings_menu.o") \
  $(W "$ST/obj/b3settings_safemode.o") $(W "$ST/obj/input_keys.o") $(W "$ST/obj/input_report.o") $(W "$ST/obj/input_bindings.o") $(W "$ST/obj/save_card.o") $(W "$ST/obj/save_formats.o") \
  $(W "$ST/obj/save_store.o") $(W "$ST/obj/save_stats.o") $(W "$ST/obj/custom_music.o") $(W "$ST/obj/music_decoders.o") $(W "$ST/obj/now_playing.o") $(W "$ST/obj/now_playing_mpris.o") $(W "$ST/obj/now_playing_win.o") $(W "$ST/obj/b3replay_prompts.o") $(W "$ST/obj/b3replay_glyphdraw.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3report_bug_report.o") $(W "$ST/obj/b3report_sysinfo.o") $(W "$ST/obj/b3report_version_info.o") $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3mod_archive.o") $(W "$ST/obj/b3mod_codemod.o") $ASSET_RT "$(W "$ST/sdl3/libSDL3.dll.a")" -lz -pthread -lwinhttp $LINK -o "$(W "$OUT/Burnout3Launcher.exe")" > "$ST/obj/launcher.link.rsp"
run g++.exe obj/launcher.link.rsp
cp "$SDL/bin/SDL3.dll" "$OUT/"
cp "$PORT/assets/b3-icon.png" "$OUT/"   # the launcher's B3 icon (header, window icon)
echo "built $(wslpath -w "$OUT")\\b3view.exe, b3replay.exe, b3assetdump.exe, b3modtool.exe (+ SDL3.dll), b3render.dll, gameview_replay.exe, Burnout3Launcher.exe"
