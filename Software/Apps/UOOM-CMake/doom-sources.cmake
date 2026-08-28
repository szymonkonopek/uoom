# The DOOM translation units UOOM links.
#
# Derived from upstream's own Makefile.soso -- the bare-OS backend, and so the
# closest thing doomgeneric has to a "no operating system" source list. Two
# substitutions:
#
#   w_file_stdc.c      -> uoom_file.c        (no stdio on the watch)
#   doomgeneric_soso.c -> doomgeneric_uoom.c (our backend)
#
# Kept explicit rather than globbed: an upstream file appearing must not
# silently get linked into a RAM-bound binary.

set(DOOM_SOURCES
    # engine core
    ${DOOM_PATH}/doomgeneric.c
    ${DOOM_PATH}/doomdef.c
    ${DOOM_PATH}/doomstat.c
    ${DOOM_PATH}/dstrings.c
    ${DOOM_PATH}/dummy.c
    ${DOOM_PATH}/d_event.c
    ${DOOM_PATH}/d_items.c
    ${DOOM_PATH}/d_iwad.c
    ${DOOM_PATH}/d_loop.c
    ${DOOM_PATH}/d_main.c
    ${DOOM_PATH}/d_mode.c
    ${DOOM_PATH}/d_net.c
    ${DOOM_PATH}/info.c
    ${DOOM_PATH}/tables.c

    # game logic
    ${DOOM_PATH}/g_game.c
    ${DOOM_PATH}/p_ceilng.c
    ${DOOM_PATH}/p_doors.c
    ${DOOM_PATH}/p_enemy.c
    ${DOOM_PATH}/p_floor.c
    ${DOOM_PATH}/p_inter.c
    ${DOOM_PATH}/p_lights.c
    ${DOOM_PATH}/p_map.c
    ${DOOM_PATH}/p_maputl.c
    ${DOOM_PATH}/p_mobj.c
    ${DOOM_PATH}/p_plats.c
    ${DOOM_PATH}/p_pspr.c
    ${DOOM_PATH}/p_saveg.c
    ${DOOM_PATH}/p_setup.c
    ${DOOM_PATH}/p_sight.c
    ${DOOM_PATH}/p_spec.c
    ${DOOM_PATH}/p_switch.c
    ${DOOM_PATH}/p_telept.c
    ${DOOM_PATH}/p_tick.c
    ${DOOM_PATH}/p_user.c

    # renderer
    ${DOOM_PATH}/r_bsp.c
    ${DOOM_PATH}/r_data.c
    ${DOOM_PATH}/r_draw.c
    ${DOOM_PATH}/r_main.c
    ${DOOM_PATH}/r_plane.c
    ${DOOM_PATH}/r_segs.c
    ${DOOM_PATH}/r_sky.c
    ${DOOM_PATH}/r_things.c
    ${DOOM_PATH}/v_video.c

    # HUD / menu / intermission / automap
    ${DOOM_PATH}/am_map.c
    ${DOOM_PATH}/f_finale.c
    ${DOOM_PATH}/f_wipe.c
    ${DOOM_PATH}/hu_lib.c
    ${DOOM_PATH}/hu_stuff.c
    ${DOOM_PATH}/m_menu.c
    ${DOOM_PATH}/st_lib.c
    ${DOOM_PATH}/st_stuff.c
    ${DOOM_PATH}/wi_stuff.c

    # misc / util
    ${DOOM_PATH}/m_argv.c
    ${DOOM_PATH}/m_bbox.c
    ${DOOM_PATH}/m_cheat.c
    ${DOOM_PATH}/m_config.c
    ${DOOM_PATH}/m_controls.c
    ${DOOM_PATH}/m_fixed.c
    ${DOOM_PATH}/m_misc.c
    ${DOOM_PATH}/m_random.c
    ${DOOM_PATH}/memio.c
    ${DOOM_PATH}/sha1.c

    # WAD access -- w_file_stdc.c deliberately absent, uoom_file.c replaces it
    ${DOOM_PATH}/w_checksum.c
    ${DOOM_PATH}/w_file.c
    ${DOOM_PATH}/w_main.c
    ${DOOM_PATH}/w_wad.c
    ${DOOM_PATH}/z_zone.c

    # platform shims that upstream keeps even on bare metal
    ${DOOM_PATH}/i_input.c
    ${DOOM_PATH}/i_system.c
    ${DOOM_PATH}/i_timer.c
    ${DOOM_PATH}/i_video.c

    # sound: the dispatchers stay (game code calls into them unconditionally);
    # every actual backend is gone. See UOOM_ENABLE_SOUND in uoom_config.h.
    ${DOOM_PATH}/i_sound.c
    ${DOOM_PATH}/s_sound.c
    ${DOOM_PATH}/sounds.c

    # Trim candidates -- present because upstream's bare-metal build keeps
    # them, and something in the engine references each. Measure before
    # dropping: i_scale.c in particular carries lookup tables we never use.
    ${DOOM_PATH}/i_cdmus.c
    ${DOOM_PATH}/i_endoom.c
    ${DOOM_PATH}/i_joystick.c
    ${DOOM_PATH}/i_scale.c
    ${DOOM_PATH}/statdump.c
)

# The renderer, split out so CMakeLists can build it -O2 while everything else
# stays -Os. FixedDiv's 64-bit divide runs per column through __aeabi_ldivmod;
# this is where the frame time actually goes.
set(DOOM_RENDERER_SOURCES
    ${DOOM_PATH}/r_bsp.c
    ${DOOM_PATH}/r_draw.c
    ${DOOM_PATH}/r_main.c
    ${DOOM_PATH}/r_plane.c
    ${DOOM_PATH}/r_segs.c
    ${DOOM_PATH}/r_things.c
    ${DOOM_PATH}/m_fixed.c
    ${DOOM_PATH}/tables.c
)

# Explicitly NOT linked, and why:
#
#   doomgeneric_{sdl,win,xlib,soso,sosox,linuxvt,allegro,emscripten}.c
#                       other platforms' backends
#   w_file_stdc.c       stdio; replaced by uoom_file.c
#   w_file_win32.c      n/a
#   i_sdlsound.c i_sdlmusic.c i_allegrosound.c i_allegromusic.c
#                       no audio path on this hardware
#   mus2mid.c gusconf.c MUS->MIDI conversion and GUS patch config: music only
#   icon.c              an SDL window icon, as a C array
