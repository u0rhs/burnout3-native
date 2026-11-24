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
. "$PORT/tools/toolchain/wincc.sh"   # 1.3.1: staging by content, ccache (CCACHE=0: none)
WIN=${WIN:-$PORT/build-win}   # local.env: where the Windows toolchain (toolchain/mingw64) and outputs live
TC=$WIN/toolchain/mingw64
SDL=${SDL3_MINGW:-$(dirname "$PORT")/ext/sdl3-3.4.2-mingw64/mingw64}
OUT=${OUT:-$WIN/b3view}   # OUT=<dir>: another output folder (a worktree build must not replace the main b3view.exe)
ST=$OUT/stage

(cd "$TC" && sha256sum --quiet -c "$PORT/tools/toolchain/toolchain.sha256") || { echo "toolchain does not match toolchain.sha256" >&2; exit 1; }
echo "12a874d28420f436d6bb43edc69ce579b4d4c85ec5a239e5f1e8fe49fe5f615d  ${MSYS2_PKGS:-$(dirname "$PORT")/ext/msys2-pkgs}/mingw-w64-x86_64-sdl3-3.4.2-1-any.pkg.tar.zst" \
  | sha256sum --quiet -c || { echo "SDL3 MSYS2 package hash mismatch" >&2; exit 1; }
# 1.3.1: staged by content (tools/toolchain/wincc.sh wincc_stage): an unchanged file is not written again (was: the stage
# deleted and copied afresh each run)
mkdir -p "$ST/sdl3" "$ST/obj" "$ST/tools"
n_st=$(wincc_stage "$PORT" "$ST" include src/b3asset src/b3gfx src/b3view src/b3replay src/b3settings src/gameview src/b3launcher \
    src/b3mod src/b3modtool src/b3report src/native/third_party \
    src/native/input_keys.h src/native/input_keys.cpp src/native/input_report.cpp src/native/input_bindings.h src/native/input_bindings.cpp \
    src/native/input_keys_sdl.h src/native/pad_actuator.h src/native/pad_gyro.h src/native/pad_triggers.h src/native/audio_device.h \
    src/native/audio_queue.h src/native/audio_device.cpp src/native/sha256.h \
    $(cd "$PORT" && ls src/native/save_card.* src/native/save_formats.* src/native/save_store.* src/native/save_stats.* src/native/custom_music.* \
                       src/native/now_playing* src/native/dbus_dl.h) \
    src/native/replay_file.h src/native/pcsx2_cards.h src/native/music_decoders.cpp)
# pad1: pad_gyro.h, pad_triggers.h; sdk2b: sha256.h (b3mod/codemod.cpp); play1: the window as the native game's pad; replay1: the
# Replays page; 1.1: pcsx2_cards.h (IMPORT SAVE); nowplaying1: the Music page's OTHER MUSIC APPS line; modkit1: the launcher's
# Mods page (include/b3mod/mods.h); modtool1: b3modtool.exe; pic1: gameview_replay; audioq: audio_queue.h (audio_device.h
# includes it); customseries: the Mods page's custom cars folder (tools/native/extra_series.h, traffic_series.h)
n_st=$((n_st + $(wincc_stage "$PORT/tools/render" "$ST/tools" b3assetdump.cpp) + $(wincc_stage "$PORT/tools/native" "$ST/tools" gameview_replay.cpp) \
        + $(wincc_stage "$PORT/tools/native" "$ST/tools/native" extra_series.h traffic_series.h) \
        + $(wincc_stage "$PORT/tools/modding" "$ST/tools" b3modtool.cpp) + $(wincc_stage "$SDL" "$ST/sdl3" include) \
        + $(wincc_stage "$SDL/lib" "$ST/sdl3" libSDL3.dll.a libSDL3.a)))
"$PORT/tools/release/version_info.sh" "$ST/src/b3report/version_info.cpp"
echo "render build_win: $n_st files staged (new or changed)" >&2

wincc_init "$TC" "$PORT" "$ST"
run() { wincc_run "$ST" "$1" "$2"; }   # $2: the rsp, relative to the stage
JOBS=${JOBS:-4}   # perf1: compiles at most JOBS at a time (the agents' build rule, -j4)
throttle() { while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done; }
W() { wslpath -m "$1"; }

# 1.3.1: the compiles' paths relative to the stage (ccache's depend mode: the -MMD lists)
DEBUG_G=$(case "${DEBUG_INFO:-0}" in 1) echo -g1 ;; 2) echo -g ;; esac)   # debuginfo: DEBUG_INFO=1 -g1 / 2 -g on every compile (tools/native/build_win.sh)
FLAGS="-std=c++20 -O2 -DNDEBUG -Wall -Wextra -ffp-contract=off -Iinclude -Isdl3/include -Isrc/b3gfx $DEBUG_G"
objs=()
for s in "$ST"/src/b3asset/*.cpp "$ST"/src/b3gfx/*.cpp "$ST"/src/b3view/*.cpp "$ST"/src/b3replay/*.cpp "$ST"/src/b3settings/*.cpp "$ST"/src/native/input_keys.cpp "$ST"/src/native/input_report.cpp "$ST"/src/native/input_bindings.cpp "$ST"/tools/b3assetdump.cpp \
         "$ST"/src/b3launcher/*.cpp "$ST"/src/native/save_card.cpp "$ST"/src/native/save_formats.cpp "$ST"/src/native/save_store.cpp "$ST"/src/native/save_stats.cpp \
         "$ST"/src/native/custom_music.cpp "$ST"/src/native/music_decoders.cpp "$ST"/src/native/now_playing.cpp "$ST"/src/native/now_playing_mpris.cpp "$ST"/src/native/now_playing_win.cpp "$ST"/src/b3report/*.cpp "$ST"/src/b3mod/*.cpp "$ST"/src/b3modtool/*.cpp "$ST"/tools/b3modtool.cpp; do   # (b3replay/inproc.cpp: the DLL's only)
  n=$(basename "${s%.cpp}")
  case "$s" in */src/b3replay/*) n=b3replay_$n ;; */src/b3settings/*) n=b3settings_$n ;; */src/b3launcher/*) n=b3launcher_$n ;; */src/b3report/*) n=b3report_$n ;; */src/b3mod/*) n=b3mod_$n ;; */src/b3modtool/*) n=b3modtool_$n ;; esac   # b3replay's main.cpp beside b3view's
  printf '%s\n' $FLAGS -c "${s#"$ST/"}" -o "obj/$n.o" -MMD -MF "obj/$n.o.d" > "$ST/obj/$n.rsp"
  throttle
  rm -f "$ST/obj/$n.o"   # 1.3.1: the stage's objects stay between runs: a failed compile leaves none
  run g++.exe "obj/$n.rsp" &
  objs+=("$n")
done
# b3render.dll (inproc1, docs/native-renderer.md §23): b3replay's main.cpp without main(), plus the C ABI
printf '%s\n' $FLAGS -DB3REPLAY_LIBRARY -c src/b3replay/main.cpp -o obj/b3render_main.o -MMD -MF obj/b3render_main.o.d > "$ST/obj/b3render_main.rsp"
throttle
rm -f "$ST/obj/b3render_main.o"
run g++.exe obj/b3render_main.rsp &
objs+=(b3render_main)
# chd1: libchdr's C unit (b3asset/discfs.h's CHD images; src/native/third_party/libchdr), compiled as C, in every
# program that has discfs (the ASSET lists)
printf '%s\n' -std=gnu11 -O2 -DNDEBUG -w $DEBUG_G -c src/b3asset/chd_libchdr.c -o obj/chd_libchdr.o -MMD -MF obj/chd_libchdr.o.d > "$ST/obj/chd_libchdr.rsp"
throttle
rm -f "$ST/obj/chd_libchdr.o"
run gcc.exe obj/chd_libchdr.rsp &
objs+=(chd_libchdr)
wait
for n in "${objs[@]}"; do [ -f "$ST/obj/$n.o" ] || { echo "compile failed: $n" >&2; exit 1; }; done
ASSET="$(for f in "$ST"/src/b3asset/*.cpp; do W "$ST/obj/$(basename "${f%.cpp}").o"; done) $(W "$ST/obj/chd_libchdr.o")"
# nofallback1: the shipped programs (b3render.dll, b3modtool.exe, Burnout3Launcher.exe) without the VU interpreter core
# (vu1.cpp: the check tools' reference; nothing in them calls it), as the Makefile's B3ASSET_RT_OBJ
ASSET_RT="$(for f in "$ST"/src/b3asset/*.cpp; do case "$(basename "$f")" in vu1.cpp|particleshade.cpp) ;; *) W "$ST/obj/$(basename "${f%.cpp}").o" ;; esac; done) $(W "$ST/obj/chd_libchdr.o")"
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
  $(W "$ST/obj/b3replay_hudmods.o") $(W "$ST/obj/b3replay_live.o") $(W "$ST/obj/b3replay_livefeed.o") $(W "$ST/obj/b3replay_nativepad.o") $(W "$ST/obj/b3replay_garagecam.o") $(W "$ST/obj/b3replay_orbitcam.o") $(W "$ST/obj/b3replay_trackrays.o") $(W "$ST/obj/b3replay_finishcam.o") $(W "$ST/obj/b3replay_replaycams.o") $(W "$ST/obj/b3replay_pcpage.o") $(W "$ST/obj/b3replay_nativeaudio.o") $(W "$ST/obj/input_keys.o") $(W "$ST/obj/input_report.o") $(W "$ST/obj/input_bindings.o") $(W "$ST/obj/b3replay_fpspanel.o") $(W "$ST/obj/b3replay_skiphint.o") $(W "$ST/obj/b3replay_racetime.o") $(W "$ST/obj/b3replay_paintsel.o") $(W "$ST/obj/b3replay_menumouse.o") $(W "$ST/obj/b3replay_prompts.o") $(W "$ST/obj/b3replay_glyphdraw.o") $(W "$ST/obj/b3replay_pcsettings.o") $(W "$ST/obj/b3settings_settings.o") $(W "$ST/obj/b3settings_safemode.o") $(W "$ST/obj/b3replay_notice.o") $(W "$ST/obj/b3replay_photomode.o") $(W "$ST/obj/b3replay_photohud.o") $(W "$ST/obj/b3replay_a11y.o") $(W "$ST/obj/b3replay_ghost.o") $(W "$ST/obj/b3replay_speedrun.o") $(W "$ST/obj/b3replay_speedrunpanel.o") $(W "$ST/obj/b3replay_tuneconsole.o") $(W "$ST/obj/b3replay_carlook.o") $(W "$ST/obj/b3replay_carlookpanel.o") $(W "$ST/obj/b3replay_trafficfade.o") $(W "$ST/obj/b3replay_splitlayout.o") $(W "$ST/obj/b3replay_modoverlay.o") $(W "$ST/obj/b3mod_codemod.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3report_bug_report.o") $(W "$ST/obj/b3report_sysinfo.o") $(W "$ST/obj/b3report_version_info.o") $(W "$ST/obj/b3settings_page.o") $(W "$ST/obj/b3settings_menu.o") $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3replay_main.o") $ASSET \
  "$(W "$ST/sdl3/libSDL3.dll.a")" -lz $LINK -o "$(W "$OUT/b3replay.exe")" > "$ST/obj/b3replay.link.rsp"
run g++.exe obj/b3replay.link.rsp
# b3render.dll: SDL3 linked statically (its own SDL inside the game process, beside the kit's SDL3.dll: §23), the C++
# runtime too; it exports only the b3r_ functions (dllexport turns off the export of everything else).
printf '%s\n' -shared $(W "$ST/obj/b3gfx_sdl.o") $(W "$ST/obj/scene.o") $(W "$ST/obj/carscene.o") $(W "$ST/obj/crashscene.o") $(W "$ST/obj/sun.o") $(W "$ST/obj/b3replay_capture.o") \
  $(W "$ST/obj/b3replay_effects.o") $(W "$ST/obj/b3replay_coronas.o") $(W "$ST/obj/b3replay_sparks.o") $(W "$ST/obj/b3replay_globe.o") $(W "$ST/obj/b3replay_hud.o") $(W "$ST/obj/b3replay_view.o") \
  $(W "$ST/obj/b3replay_hudmods.o") $(W "$ST/obj/b3replay_live.o") $(W "$ST/obj/b3replay_livefeed.o") $(W "$ST/obj/b3replay_nativepad.o") $(W "$ST/obj/b3replay_garagecam.o") $(W "$ST/obj/b3replay_orbitcam.o") $(W "$ST/obj/b3replay_trackrays.o") $(W "$ST/obj/b3replay_finishcam.o") $(W "$ST/obj/b3replay_replaycams.o") $(W "$ST/obj/b3replay_pcpage.o") $(W "$ST/obj/b3replay_nativeaudio.o") $(W "$ST/obj/input_keys.o") $(W "$ST/obj/input_report.o") $(W "$ST/obj/input_bindings.o") $(W "$ST/obj/b3replay_fpspanel.o") $(W "$ST/obj/b3replay_skiphint.o") $(W "$ST/obj/b3replay_racetime.o") $(W "$ST/obj/b3replay_paintsel.o") $(W "$ST/obj/b3replay_menumouse.o") $(W "$ST/obj/b3replay_prompts.o") $(W "$ST/obj/b3replay_glyphdraw.o") $(W "$ST/obj/b3replay_pcsettings.o") $(W "$ST/obj/b3settings_settings.o") $(W "$ST/obj/b3settings_safemode.o") $(W "$ST/obj/b3replay_notice.o") $(W "$ST/obj/b3replay_photomode.o") $(W "$ST/obj/b3replay_photohud.o") $(W "$ST/obj/b3replay_a11y.o") $(W "$ST/obj/b3replay_ghost.o") $(W "$ST/obj/b3replay_speedrun.o") $(W "$ST/obj/b3replay_speedrunpanel.o") $(W "$ST/obj/b3replay_tuneconsole.o") $(W "$ST/obj/b3replay_carlook.o") $(W "$ST/obj/b3replay_carlookpanel.o") $(W "$ST/obj/b3replay_trafficfade.o") $(W "$ST/obj/b3replay_splitlayout.o") $(W "$ST/obj/b3replay_modoverlay.o") $(W "$ST/obj/b3mod_codemod.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3report_bug_report.o") $(W "$ST/obj/b3report_sysinfo.o") $(W "$ST/obj/b3report_version_info.o") $(W "$ST/obj/b3settings_page.o") $(W "$ST/obj/b3settings_menu.o") $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3render_main.o") $(W "$ST/obj/b3replay_inproc.o") $ASSET_DLL \
  "$(W "$ST/sdl3/libSDL3.a")" -lz $LINK -lm -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 \
  -o "$(W "$OUT/b3render.dll")" > "$ST/obj/b3render.link.rsp"
run g++.exe obj/b3render.link.rsp
printf '%s\n' $(W "$ST/obj/b3assetdump.o") $ASSET $LINK -o "$(W "$OUT/b3assetdump.exe")" > "$ST/obj/b3assetdump.link.rsp"
run g++.exe obj/b3assetdump.link.rsp
# b3modtool.exe (modtool1): a console program, zlib and the C++ runtime static (needs no DLL)
printf '%s\n' $(W "$ST/obj/b3modtool.o") $(for f in "$ST"/src/b3modtool/*.cpp; do W "$ST/obj/b3modtool_$(basename "${f%.cpp}").o"; done) \
  $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3launcher_showroom.o") $ASSET_RT -lz $LINK -lshell32 -o "$(W "$OUT/b3modtool.exe")" > "$ST/obj/b3modtool.link.rsp"   # models3c: the showroom's car names
run g++.exe obj/b3modtool.link.rsp
# gameview_replay.exe (pic1, docs/native-renderer.md §32): a scene capture through the game view's capture code into
# b3render.dll (LoadLibrary), as the native backend drives it
for n in gameview_replay scene_capture hud_capture; do
  case $n in gameview_replay) s=$ST/tools/$n.cpp ;; *) s=$ST/src/gameview/$n.cpp ;; esac
  printf '%s\n' -std=gnu++20 -O2 -DNDEBUG -Wall -Wextra -Iinclude $DEBUG_G -c "${s#"$ST/"}" -o "obj/gv_$n.o" -MMD -MF "obj/gv_$n.o.d" > "$ST/obj/gv_$n.rsp"
  rm -f "$ST/obj/gv_$n.o"
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
# update1: WinHTTP (a system DLL) for the update check; update2: the update's install step (installer.cpp)
printf '%s\n' -mwindows $(W "$ST/obj/b3icon.res.o") $(W "$ST/obj/b3launcher_main.o") $(W "$ST/obj/b3launcher_showroom.o") $(W "$ST/obj/b3launcher_showroom_view.o") $(W "$ST/obj/b3gfx_sdl.o") $(W "$ST/obj/scene.o") $(W "$ST/obj/carscene.o") $(W "$ST/obj/b3launcher_core.o") $(W "$ST/obj/b3launcher_update.o") $(W "$ST/obj/b3launcher_update_net.o") $(W "$ST/obj/b3launcher_installer.o") $(W "$ST/obj/b3launcher_portable.o") $(W "$ST/obj/b3launcher_sign.o") $(W "$ST/obj/b3launcher_oneclick.o") $(W "$ST/obj/b3launcher_steam.o") $(W "$ST/obj/b3launcher_steam_art.o") $(W "$ST/obj/b3launcher_spawn.o") $(W "$ST/obj/b3launcher_gameart.o") \
  $(W "$ST/obj/b3launcher_font.o") $(W "$ST/obj/b3launcher_draw.o") $(W "$ST/obj/b3settings_settings.o") $(W "$ST/obj/b3settings_page.o") $(W "$ST/obj/b3settings_menu.o") \
  $(W "$ST/obj/b3settings_safemode.o") $(W "$ST/obj/input_keys.o") $(W "$ST/obj/input_report.o") $(W "$ST/obj/input_bindings.o") $(W "$ST/obj/save_card.o") $(W "$ST/obj/save_formats.o") \
  $(W "$ST/obj/save_store.o") $(W "$ST/obj/save_stats.o") $(W "$ST/obj/custom_music.o") $(W "$ST/obj/music_decoders.o") $(W "$ST/obj/now_playing.o") $(W "$ST/obj/now_playing_mpris.o") $(W "$ST/obj/now_playing_win.o") $(W "$ST/obj/b3replay_prompts.o") $(W "$ST/obj/b3replay_glyphdraw.o") $(W "$ST/obj/b3report_zip.o") $(W "$ST/obj/b3report_bug_report.o") $(W "$ST/obj/b3report_sysinfo.o") $(W "$ST/obj/b3report_version_info.o") $(W "$ST/obj/b3mod_mods.o") $(W "$ST/obj/b3mod_archive.o") $(W "$ST/obj/b3mod_codemod.o") $ASSET_RT "$(W "$ST/sdl3/libSDL3.dll.a")" -lz -pthread -lwinhttp $LINK -o "$(W "$OUT/Burnout3Launcher.exe")" > "$ST/obj/launcher.link.rsp"
run g++.exe obj/launcher.link.rsp
cp "$SDL/bin/SDL3.dll" "$OUT/"
cp "$PORT/assets/b3-icon.png" "$OUT/"   # the launcher's B3 icon (header, window icon)
echo "built $(wslpath -w "$OUT")\\b3view.exe, b3replay.exe, b3assetdump.exe, b3modtool.exe (+ SDL3.dll), b3render.dll, gameview_replay.exe, Burnout3Launcher.exe"
