#!/usr/bin/env python3
"""Apply UOOM's changes to a clean doomgeneric checkout, in place.

This is the *source of truth* for the port's engine modifications. Running it
against a clean tree and diffing produces tools/patches/*.patch, which is what
tools/fetch-doomgeneric.sh applies. Keeping the edits as a script rather than
hand-maintained diffs means they survive an upstream bump: a substitution that
no longer matches fails loudly instead of a patch hunk silently rotting.

Every edit asserts its own match count. A zero means upstream moved and the
port needs looking at.
"""
import re
import sys
from pathlib import Path

DG = None
edits = 0


def rewrite(name, pattern, repl, count=1, flags=re.S, groups=False):
    """Substitute in doomgeneric/<name>, asserting how many sites changed.

    `repl` is inserted literally unless groups=True, in which case it is a
    normal re template and may use \1. Literal-by-default matters: several
    replacements contain C escapes like \n inside string literals, which a
    template would turn into real newlines and produce code that does not
    compile.
    """
    global edits
    p = DG / name
    src = p.read_text()
    out, n = re.subn(pattern, repl if groups else (lambda m: repl), src,
                     flags=flags)
    if n != count:
        sys.exit(f"FAIL {name}: expected {count} match(es) for "
                 f"{pattern[:60]!r}, got {n}")
    p.write_text(out)
    edits += n
    print(f"  {name}: {n} edit(s)")


# --------------------------------------------------------------- 0001 video
#
# The core of the port. doomgeneric's I_FinishUpdate expands DOOM's 8-bit
# screen into a 32-bit DG_ScreenBuffer -- 256KB we do not have, to produce a
# format the panel does not want. Worse, it computes
# `fb_scaling = s_Fb.xres / SCREENWIDTH`, which for a 240-wide panel is 0, so
# the copy loop never runs and the screen stays black.

def patch_video():
    rewrite("i_video.c",
            r'#include "doomgeneric\.h"\n',
            '#include "doomgeneric.h"\n\n/* UOOM: 8bpp straight to the panel. */\n'
            '#include "uoom_hooks.h"\n')

    rewrite("i_video.c",
            r'void I_FinishUpdate \(void\)\n\{.*?\n\tDG_DrawFrame\(\);\n\}',
            'void I_FinishUpdate (void)\n'
            '{\n'
            '    /* UOOM: the panel is itself 8 bits per pixel, so the palette\n'
            '     * is applied during the resample and the 32-bit intermediate\n'
            '     * buffer is gone entirely. */\n'
            '    UOOM_FinishUpdate((const unsigned char *) I_VideoBuffer);\n'
            '}')

    rewrite("i_video.c",
            r'void I_SetPalette \(byte\* palette\)\n\{.*?#endif  // CMAP256\n\}',
            'void I_SetPalette (byte* palette)\n'
            '{\n'
            '    /* UOOM: gamma-correct here, quantise to ABGR2222 in the port\n'
            '     * layer. 256 conversions per palette change instead of 64000\n'
            '     * per frame. */\n'
            '    static byte gpal[768];\n'
            '    int i;\n'
            '\n'
            '    for (i = 0; i < 768; ++i)\n'
            '    {\n'
            '        gpal[i] = gammatable[usegamma][palette[i]];\n'
            '    }\n'
            '    UOOM_SetPalette(gpal);\n'
            '}')

    rewrite("doomgeneric.c",
            r'\tDG_ScreenBuffer = malloc\(DOOMGENERIC_RESX \* DOOMGENERIC_RESY \* 4\);\n',
            '\t/* UOOM: nothing reads DG_ScreenBuffer once I_FinishUpdate is\n'
            '\t * hooked, and this malloc is 256KB on a budget of a few\n'
            '\t * hundred. Left NULL. */\n')


# ---------------------------------------------------------------- 0002 system
#
# I_ZoneBase asks for 6MB and refuses to start below 6MB. A static array of the
# real budget means the *linker* rejects an over-large zone at build time,
# with a number, instead of the allocator rejecting it on the watch in front of
# the user.

def patch_system():
    rewrite("i_system.c",
            r'#include "z_zone\.h"\n',
            '#include "z_zone.h"\n\n/* UOOM */\n#include "uoom_hooks.h"\n')

    rewrite("i_system.c",
            r'static byte \*AutoAllocMemory\(int \*size, int default_ram, int min_ram\)\n\{',
            '/* UOOM: unreachable -- I_ZoneBase no longer probes for RAM. */\n'
            '#if 0\n'
            'static byte *AutoAllocMemory(int *size, int default_ram, int min_ram)\n{')

    rewrite("i_system.c",
            r'    return zonemem;\n\}\n\nbyte \*I_ZoneBase \(int \*size\)\n\{.*?\n    return zonemem;\n\}',
            '    return zonemem;\n'
            '}\n'
            '#endif  /* UOOM */\n'
            '\n'
            'byte *I_ZoneBase (int *size)\n'
            '{\n'
            '    /* UOOM: the kernel allocator, not malloc(6MB) and not .bss.\n'
            '     * See uoom_zone_base() for why the heap is the right home\n'
            '     * for this on the UNA platform. */\n'
            '    byte *zonemem = (byte *) uoom_zone_base(size);\n'
            '\n'
            '    printf("zone: %d bytes\\n", *size);\n'
            '\n'
            '    return zonemem;\n'
            '}')

    rewrite("i_system.c",
            r'void I_Error \(char \*error, \.\.\.\)\n\{\n    char msgbuf\[512\];',
            'void I_Error (char *error, ...)\n'
            '{\n'
            '    /* UOOM: no stderr and no exit() on this target. Put the\n'
            '     * message on the panel -- a black screen tells the user\n'
            '     * nothing -- and stop. Does not return. */\n'
            '    {\n'
            '        char    uoomMsg[256];\n'
            '        va_list uoomAp;\n'
            '\n'
            '        va_start(uoomAp, error);\n'
            '        M_vsnprintf(uoomMsg, sizeof(uoomMsg), error, uoomAp);\n'
            '        va_end(uoomAp);\n'
            '        uoom_fatal("%s", uoomMsg);\n'
            '    }\n'
            '\n'
            '    char msgbuf[512];')


# ----------------------------------------------------------------- 0003 limits
#
# These six constants are 45% of DOOM's static footprint. Upstream sized them
# for a PC with memory to spare; see docs/03-memory-budget.md for the arithmetic.

def patch_limits():
    rewrite("r_plane.c",
            r'#define MAXVISPLANES\s+128',
            '#include "uoom_config.h"        /* UOOM */\n'
            '#define MAXVISPLANES    UOOM_MAXVISPLANES')
    rewrite("r_plane.c",
            r'#define MAXOPENINGS\s+SCREENWIDTH\*64',
            '#define MAXOPENINGS     (SCREENWIDTH*64/UOOM_MAXOPENINGS_DIV)')

    rewrite("r_defs.h",
            r'#define MAXDRAWSEGS\s+256',
            '#include "uoom_config.h"        /* UOOM */\n'
            '#define MAXDRAWSEGS     UOOM_MAXDRAWSEGS')

    rewrite("r_things.h",
            r'#define MAXVISSPRITES\s+128',
            '#include "uoom_config.h"        /* UOOM */\n'
            '#define MAXVISSPRITES   UOOM_MAXVISSPRITES')

    # MAXWIDTH/MAXHEIGHT exist for hi-res builds this port will never do.
    rewrite("r_draw.c", r'#define MAXWIDTH\s+1120',
            '#define MAXWIDTH        SCREENWIDTH')
    rewrite("r_draw.c", r'#define MAXHEIGHT\s+832',
            '#define MAXHEIGHT       SCREENHEIGHT')

    # 20KB of network tic backlog for a game that cannot be networked.
    rewrite("net_defs.h", r'#define NET_MAXPLAYERS\s+8',
            '#define NET_MAXPLAYERS  1       /* UOOM: no multiplayer */')
    rewrite("net_defs.h", r'#define BACKUPTICS\s+128',
            '#define BACKUPTICS      8       /* UOOM: was 128 */')


# ------------------------------------------------------------------ 0004 input
#
# I_GetEvent's drain loop breaks out after the first key-up in a tic. With four
# buttons emitting press+release pairs into a queue that overwrites its oldest
# entry, a dropped key-up is a key stuck down forever -- the player walks into
# a wall until the app is restarted.

def patch_input():
    rewrite("i_input.c",
            r'(            event\.data2 = 0;\n'
            r'\n'
            r'            if \(event\.data1 != 0\)\n'
            r'            \{\n'
            r'                D_PostEvent\(&event\);\n'
            r'            \}\n)'
            r'            break;\n',
            r'\1'
            '            /* UOOM: upstream breaks here, which drops every\n'
            '             * key-up after the first one in a tic. On a\n'
            '             * four-button device that is a stuck key. */\n',
            groups=True)


# ------------------------------------------------------------------ 0005 wipes
#
# The melt between levels allocates three extra 320x200 buffers plus a
# transform scratch: ~192KB of zone peak for two seconds of nostalgia.

def patch_wipes():
    rewrite("d_main.c",
            r'#include "d_main\.h"\n',
            '#include "d_main.h"\n\n#include "uoom_config.h"       /* UOOM */\n')
    rewrite("d_main.c",
            r'// save the current screen if about to wipe\n'
            r'\s*if \(gamestate != wipegamestate\)\n'
            r'\s*\{\n'
            r'\s*wipe = true;\n'
            r'\s*wipe_StartScreen\(0, 0, SCREENWIDTH, SCREENHEIGHT\);\n'
            r'\s*\}\n'
            r'\s*else\n'
            r'\s*wipe = false;\n',
            '    // save the current screen if about to wipe\n'
            '#if UOOM_ENABLE_WIPES\n'
            '    if (gamestate != wipegamestate)\n'
            '    {\n'
            '        wipe = true;\n'
            '        wipe_StartScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);\n'
            '    }\n'
            '    else\n'
            '        wipe = false;\n'
            '#else\n'
            '    /* UOOM: ~192KB of zone peak for the melt. Not on this\n'
            '     * budget -- see docs/03-memory-budget.md. */\n'
            '    wipe = false;\n'
            '#endif\n')


# ------------------------------------------------------------------ 0006 stdio
#
# Everything left that reaches for a hosted C library: directory creation on
# the startup path, IWAD probing, and the savegame serialiser's byte-at-a-time
# FILE* traffic.

def patch_stdio():
    rewrite("m_misc.c",
            r'#include "m_misc\.h"\n',
            '#include "m_misc.h"\n\n/* UOOM */\n#include "uoom_plat.h"\n')

    # M_MakeDirectory runs unconditionally from M_SetConfigDir and
    # M_GetSaveGameDir before anything else happens.
    rewrite("m_misc.c",
            r'void M_MakeDirectory\(char \*path\)\n\{[ \t]*\n',
            'void M_MakeDirectory(char *path)\n'
            '{\n'
            '    /* UOOM */\n'
            '    uoom_plat_mkdir(path);\n'
            '    if (1) return;\n')

    # M_FileExists is how d_iwad finds the IWAD. Upstream opens the file with
    # fopen and inspects errno for EISDIR.
    rewrite("m_misc.c",
            r'boolean M_FileExists\(char \*filename\)\n\{[ \t]*\n',
            'boolean M_FileExists(char *filename)\n'
            '{\n'
            '    /* UOOM */\n'
            '    return uoom_plat_exists(filename) ? true : false;\n'
            '#if 0\n')
    rewrite("m_misc.c",
            r'(boolean M_FileExists\(char \*filename\)\n\{[ \t]*\n'
            r'    /\* UOOM \*/\n'
            r'    return uoom_plat_exists\(filename\) \? true : false;\n'
            r'#if 0\n.*?\n)\}\n',
            r'\1#endif\n}\n',
            groups=True)

    # The savegame path: p_saveg.c serialises through a FILE* one byte at a
    # time, so it gets the port's buffered replacement.
    for f in ("p_saveg.c", "p_saveg.h", "g_game.c", "m_menu.c"):
        src = (DG / f).read_text()
        header = '#include "uoom_file.h"    /* UOOM */\n'
        if header not in src:
            src = header + src
        for a, b in (
            (r'\bFILE\b', 'uoom_FILE'),
            (r'\bfopen\b', 'uoom_fopen'),
            (r'\bfread\b', 'uoom_fread'),
            (r'\bfwrite\b', 'uoom_fwrite'),
            (r'\bfclose\b', 'uoom_fclose'),
            (r'\bftell\b', 'uoom_ftell'),
            (r'\bremove\(', 'uoom_plat_remove('),
            (r'\brename\(', 'uoom_plat_rename('),
        ):
            src = re.sub(a, b, src)
        if 'uoom_plat_remove' in src or 'uoom_plat_rename' in src:
            src = src.replace(header, header + '#include "uoom_plat.h"    /* UOOM */\n')
        (DG / f).write_text(src)
        print(f"  {f}: stdio -> uoom_FILE")
        global edits
        edits += 1

    # V_ScreenShot writes a PCX through fopen. Nothing on a watch wants it.
    rewrite("v_video.c",
            r'void V_ScreenShot\(char \*format\)\n\{[ \t]*\n',
            'void V_ScreenShot(char *format)\n'
            '{\n'
            '    /* UOOM: no screenshot writer. */\n'
            '    (void) format;\n'
            '    if (1) return;\n')


# ------------------------------------------------------------------ 0007 safety
#
# Two latent bugs that only bite once the renderer limits are lowered, plus one
# allocation moved out of the zone.

def patch_safety():
    # R_FindPlane checks MAXVISPLANES before bumping lastvisplane. R_CheckPlane
    # does NOT -- vanilla never added the check and doomgeneric inherited that.
    # Overflowing through this path writes 664 bytes past the array, straight
    # over lastvisplane / floorplane / ceilingplane. With UOOM_MAXVISPLANES
    # lowered from 128 that goes from theoretical to reachable, so a clean
    # I_Error is worth the branch. Chocolate Doom fixed it the same way.
    rewrite("r_plane.c",
            r'    // make a new visplane\n'
            r'    lastvisplane->height = pl->height;',
            '    // make a new visplane\n'
            '    if (lastvisplane - visplanes == MAXVISPLANES)\n'
            '        I_Error ("R_CheckPlane: no more visplanes");   /* UOOM */\n'
            '\n'
            '    lastvisplane->height = pl->height;')

    # solidsegs is sized 32 in vanilla, below the theoretical maximum for a
    # 320-wide screen. Chocolate raised it to SCREENWIDTH/2+1 after Lee
    # Killough showed the bound is a function of screen size. 1.3KB for the
    # difference between correct clipping and an overflow.
    rewrite("r_bsp.c", r'#define MAXSEGS\s+32',
            '#define MAXSEGS         (SCREENWIDTH/2+1)   /* UOOM: was 32 */')

    # I_VideoBuffer out of the zone and into .bss. Same total RAM, but it stops
    # a 64KB PU_STATIC block from fragmenting the arena at startup, which is
    # worth real kilobytes off the measured zone floor.
    rewrite("i_video.c",
            r'\tI_VideoBuffer = \(byte\*\)Z_Malloc \(SCREENWIDTH \* SCREENHEIGHT, PU_STATIC, NULL\);[^\n]*\n',
            '\tI_VideoBuffer = (byte *) uoom_screen_buffer();  /* UOOM: static, not zone */\n')
    rewrite("i_video.c", r'\tZ_Free \(I_VideoBuffer\);\n',
            '\t/* UOOM: I_VideoBuffer is static now, nothing to free. */\n')


# ------------------------------------------------------------------ 0009 RAM diet
#
# Stage 1 of the structure diet described in docs/03-memory-budget.md. Only
# changes that are provably safe: a field read in exactly one place and
# trivially derivable, and a field whose every use is a comparison.
#
# line_t is the single largest level structure -- 68 bytes on 32-bit ARM times
# a few thousand linedefs. This takes 20 bytes off it.

def patch_ram():
    # bbox: four fixed_t, i.e. 16 bytes per linedef, that are nothing but
    # min/max of v1 and v2. Written once at load, read in exactly one place
    # (PIT_CheckLine's early rejection). Verified by grepping every .c/.h --
    # r_bsp.c's bbox use is node_t's, a different struct.
    rewrite("r_defs.h",
            r'    // Neat\. Another bounding box, for the extent\n'
            r'    //  of the LineDef\.\n'
            r'    fixed_t\tbbox\[4\];\n',
            '    /* UOOM: bbox[4] removed -- 16 bytes per linedef holding nothing\n'
            '     * but min/max of v1 and v2, read in one place. Computed there\n'
            '     * instead. See docs/03-memory-budget.md. */\n')

    # slopetype is an enum stored in an int; every use is a comparison or a
    # switch (p_maputl.c:112, p_map.c:623,629). One byte is plenty.
    rewrite("r_defs.h",
            r'    // To aid move clipping\.\n'
            r'    slopetype_t\tslopetype;\n',
            '    // To aid move clipping.\n'
            '    unsigned char slopetype;    /* UOOM: was slopetype_t (int) */\n')

    rewrite("p_setup.c",
            r'\n\tif \(v1->x < v2->x\)\n'
            r'\t\{\n'
            r'\t    ld->bbox\[BOXLEFT\] = v1->x;\n'
            r'\t    ld->bbox\[BOXRIGHT\] = v2->x;\n'
            r'\t\}\n'
            r'\telse\n'
            r'\t\{\n'
            r'\t    ld->bbox\[BOXLEFT\] = v2->x;\n'
            r'\t    ld->bbox\[BOXRIGHT\] = v1->x;\n'
            r'\t\}\n'
            r'\n'
            r'\tif \(v1->y < v2->y\)\n'
            r'\t\{\n'
            r'\t    ld->bbox\[BOXBOTTOM\] = v1->y;\n'
            r'\t    ld->bbox\[BOXTOP\] = v2->y;\n'
            r'\t\}\n'
            r'\telse\n'
            r'\t\{\n'
            r'\t    ld->bbox\[BOXBOTTOM\] = v2->y;\n'
            r'\t    ld->bbox\[BOXTOP\] = v1->y;\n'
            r'\t\}\n',
            '\n\t/* UOOM: line bbox is derived at the one place that reads it. */\n')

    rewrite("p_map.c",
            r'    if \(tmbbox\[BOXRIGHT\] <= ld->bbox\[BOXLEFT\]\n'
            r'\t\|\| tmbbox\[BOXLEFT\] >= ld->bbox\[BOXRIGHT\]\n'
            r'\t\|\| tmbbox\[BOXTOP\] <= ld->bbox\[BOXBOTTOM\]\n'
            r'\t\|\| tmbbox\[BOXBOTTOM\] >= ld->bbox\[BOXTOP\] \)\n'
            r'\treturn true;\n',
            '    /* UOOM: was ld->bbox[4], a cached min/max of v1 and v2 costing\n'
            '     * 16 bytes on every linedef in the level. This is the only\n'
            '     * reader, and it is per-line inside a blockmap cell, not an\n'
            '     * inner loop. */\n'
            '    {\n'
            '\tconst fixed_t x1 = ld->v1->x, x2 = ld->v2->x;\n'
            '\tconst fixed_t y1 = ld->v1->y, y2 = ld->v2->y;\n'
            '\tconst fixed_t lLeft   = (x1 < x2) ? x1 : x2;\n'
            '\tconst fixed_t lRight  = (x1 < x2) ? x2 : x1;\n'
            '\tconst fixed_t lBottom = (y1 < y2) ? y1 : y2;\n'
            '\tconst fixed_t lTop    = (y1 < y2) ? y2 : y1;\n'
            '\n'
            '\tif (tmbbox[BOXRIGHT] <= lLeft\n'
            '\t    || tmbbox[BOXLEFT] >= lRight\n'
            '\t    || tmbbox[BOXTOP] <= lBottom\n'
            '\t    || tmbbox[BOXBOTTOM] >= lTop )\n'
            '\t    return true;\n'
            '    }\n')

    # Level-geometry accounting, opt-in. The zone floor is set by these arrays,
    # so being able to print them per map is how the diet gets measured.
    for name, count, typ in (
        ("vertexes", "numvertexes", "vertex_t"),
        ("segs", "numsegs", "seg_t"),
        ("subsectors", "numsubsectors", "subsector_t"),
        ("sectors", "numsectors", "sector_t"),
        ("nodes", "numnodes", "node_t"),
        ("lines", "numlines", "line_t"),
        ("sides", "numsides", "side_t"),
    ):
        call = "%s = Z_Malloc (%s*sizeof(%s),PU_LEVEL,0);" % (name, count, typ)
        log = ('%s\n'
               '#if UOOM_LOG_MAP_ALLOC\n'
               '    printf("MAP %-11s n=%%6d elem=%%3d total=%%7d\\n",\n'
               '           %s, (int) sizeof(%s), (int) (%s*sizeof(%s)));\n'
               '#endif\n') % (call, name, count, typ, count, typ)
        rewrite("p_setup.c", re.escape(call), log)

    rewrite("p_setup.c", r'#include "p_local\.h"\n',
            '#include "p_local.h"\n\n#include "uoom_config.h"       /* UOOM */\n')


# ----------------------------------------------------------------- 0010 RAM diet 2
#
# node_t.bbox: eight fixed_t per node, 32 of its 52 bytes. But the WAD stores
# these as 16-bit shorts and P_LoadNodes shifts them up by FRACBITS, so keeping
# the shorts and shifting on read is *provably lossless* -- the low 16 bits were
# never anything but zero.
#
# Two sites touch them: the write in P_LoadNodes and R_CheckBBox, which is the
# only reader. Four shifts per visited node against 16 bytes per node in the
# level is not a close call.

def patch_ram2():
    rewrite("r_defs.h",
            r'    // Bounding box for each child\.\n'
            r'    fixed_t\tbbox\[2\]\[4\];\n',
            '    // Bounding box for each child.\n'
            '    /* UOOM: the WAD holds these as 16-bit shorts and P_LoadNodes\n'
            '     * used to shift them up by FRACBITS. Kept narrow and expanded\n'
            '     * in R_CheckBBox, the only reader -- 16 bytes per node. */\n'
            '    short\tbbox[2][4];\n')

    rewrite("p_setup.c",
            r'\t\tno->bbox\[j\]\[k\] = SHORT\(mn->bbox\[j\]\[k\]\)<<FRACBITS;',
            '\t\tno->bbox[j][k] = SHORT(mn->bbox[j][k]);   /* UOOM: no <<FRACBITS */')

    rewrite("r_bsp.c",
            r'boolean R_CheckBBox \(fixed_t\*\tbspcoord\)\n\{[ \t]*\n',
            'boolean R_CheckBBox (const short*\tbspcoord16)\n'
            '{\n'
            '    /* UOOM: expand the node\'s 16-bit bbox once, here, instead of\n'
            '     * storing it expanded in every node_t. */\n'
            '    const fixed_t\tbspcoord[4] = {\n'
            '\t(fixed_t) bspcoord16[0] << FRACBITS,\n'
            '\t(fixed_t) bspcoord16[1] << FRACBITS,\n'
            '\t(fixed_t) bspcoord16[2] << FRACBITS,\n'
            '\t(fixed_t) bspcoord16[3] << FRACBITS\n'
            '    };\n'
            '\n')


# ------------------------------------------------------------- 0011 FixedDiv
#
# m_fixed.c does `((int64_t) a << 16) / b` -- a 64-by-32 division, per column
# of the frame. The UNA linker script discards libgcc.a outright, so the
# helper the compiler wants (__aeabi_ldivmod) does not exist and the link
# fails. Point it at a 64/32 routine instead, which is what the operation
# actually is and is faster than a general 64/64 helper would be.

def patch_fixeddiv():
    rewrite("m_fixed.c", r'#include "m_fixed\.h"\n',
            '#include "m_fixed.h"\n\n#include "uoom_libc.h"        /* UOOM */\n')
    rewrite("m_fixed.c",
            r'\tint64_t result;\n'
            r'\n?\s*result = \(\(int64_t\) a << 16\) / b;\n',
            '\tint64_t result;\n'
            '\n'
            '\t/* UOOM: was `((int64_t) a << 16) / b`, which needs\n'
            '\t * __aeabi_ldivmod -- discarded along with libgcc.a on this\n'
            '\t * platform. Same arithmetic, two hardware 32-bit divides. */\n'
            '\tresult = uoom_div64_32((int64_t) a << 16, b);\n')


# --------------------------------------------------------------- 0012 no stdio 2
#
# What is left after patch 0006, found the only way that finds it: by linking
# for ARM and reading the undefined symbols. The UNA linker script discards
# libc.a, libm.a and libgcc.a and binds 336 symbols to the kernel's newlib, so
# every reference outside that list is a link error -- and most of the ones DOOM
# still had were in code that patch 0002 had already made unreachable but that
# the linker keeps anyway, because -ffunction-sections cannot split a function.
#
# Three groups:
#   * diagnostics through stderr/stdout  -> printf, which the kernel provides
#   * file I/O left in m_misc            -> stubbed
#   * float formatting and fabs          -> deleted, which also removes the
#                                          three libgcc soft-float helpers

def patch_nostdio2():
    # --- diagnostics: fprintf(stderr, ...) -> printf(...) ------------------
    #
    # This also removes every reference to `_impure_ptr`, which the stderr and
    # stdout macros pull in.
    for f, n in (("p_map.c", 1), ("p_setup.c", 1), ("p_spec.c", 4),
                 ("p_doors.c", 1), ("p_saveg.c", 2), ("m_menu.c", 1)):
        rewrite(f, r'fprintf\(\s*stderr,', 'printf(', count=n)

    rewrite("am_map.c", r'DEH_fprintf\(stderr,', 'printf(')

    # i_scale.c only flushes progress dots.
    rewrite("i_scale.c", r'fflush\(stdout\);', '/* UOOM: no stdout */;', count=3)

    # Z_FileDumpHeap writes a heap dump to a FILE*. Debug-only, and the only
    # remaining user of fprintf.
    rewrite("z_zone.c",
            r'void Z_FileDumpHeap \(FILE\* f\)\n\{[ \t]*\n',
            'void Z_FileDumpHeap (FILE* f)\n'
            '{\n'
            '    /* UOOM: no FILE* on this platform. */\n'
            '    (void) f;\n'
            '    if (1) return;\n')

    # --- i_system: the unreachable error-box path -------------------------
    #
    # I_Error hands off to uoom_fatal on its first statement (patch 0002) and
    # never comes back, but the rest of the function is still linked.
    rewrite("i_system.c",
            r'        fprintf\(stderr, "Warning: recursive call to I_Error detected\.\\n"\);',
            '        printf("Warning: recursive call to I_Error detected.\\n");')
    rewrite("i_system.c",
            r'    vfprintf\(stderr, error, argptr\);\n'
            r'    fprintf\(stderr, "\\n\\n"\);\n',
            '    /* UOOM: unreachable -- uoom_fatal already took the message. */\n'
            '    (void) argptr;\n')
    rewrite("i_system.c", r'    fflush\(stderr\);', '    /* UOOM */;')
    rewrite("i_system.c",
            r'    return system\(ZENITY_BINARY " --help >/dev/null 2>&1"\) == 0;',
            '    return 0;   /* UOOM: no system() */')
    rewrite("i_system.c", r'    result = system\(errorboxpath\);',
            '    result = -1;   /* UOOM: no system() */')

    # --- m_misc: the file helpers nothing reachable uses ------------------
    rewrite("m_misc.c",
            r'long M_FileLength\(FILE \*handle\)\n\{[ \t]*\n',
            'long M_FileLength(FILE *handle)\n'
            '{\n'
            '    /* UOOM: no FILE*. The WAD reader uses uoom_plat_filesize. */\n'
            '    (void) handle;\n'
            '    if (1) return 0;\n')
    rewrite("m_misc.c",
            r'boolean M_WriteFile\(char \*name, void \*source, int length\)\n\{[ \t]*\n',
            'boolean M_WriteFile(char *name, void *source, int length)\n'
            '{\n'
            '    /* UOOM: demos and dehacked only. */\n'
            '    (void) name; (void) source; (void) length;\n'
            '    if (1) return false;\n')
    rewrite("m_misc.c",
            r'int M_ReadFile\(char \*name, byte \*\*buffer\)\n\{[ \t]*\n',
            'int M_ReadFile(char *name, byte **buffer)\n'
            '{\n'
            '    /* UOOM: demos and dehacked only. */\n'
            '    (void) name; (void) buffer;\n'
            '    if (1) return -1;\n')
    rewrite("m_misc.c",
            r'char \*M_TempFile\(char \*s\)\n\{[ \t]*\n',
            'char *M_TempFile(char *s)\n'
            '{\n'
            '    /* UOOM: there is no /tmp. */\n'
            '    (void) s;\n'
            '    if (1) return 0;\n')

    # M_StrToInt parses with four sscanf calls; strtol is in the kernel's libc
    # and sscanf is not.
    rewrite("m_misc.c",
            r'    return sscanf\(str, " 0x%x", result\) == 1\n'
            r'        \|\| sscanf\(str, " 0X%x", result\) == 1\n'
            r'        \|\| sscanf\(str, " 0%o", result\) == 1\n'
            r'        \|\| sscanf\(str, " %d", result\) == 1;',
            '    /* UOOM: strtol is provided by the kernel libc; sscanf is not.\n'
            '     * Base 0 already handles the 0x / 0 / decimal forms sscanf was\n'
            '     * being used to try in turn. */\n'
            '    {\n'
            '        char *end = 0;\n'
            '        long v;\n'
            '\n'
            '        while (*str == \' \') { ++str; }\n'
            '        v = strtol(str, &end, 0);\n'
            '        if (end == str) { return 0; }\n'
            '        *result = (int) v;\n'
            '        return 1;\n'
            '    }')

    # --- m_config: the only float parsing and the only sscanf left --------
    rewrite("m_config.c",
            r'        sscanf\(strparm\+2, "%x", &parm\);',
            '        parm = (int) strtol(strparm + 2, 0, 16);   /* UOOM */')
    rewrite("m_config.c",
            r'        sscanf\(strparm, "%i", &parm\);',
            '        parm = (int) strtol(strparm, 0, 0);        /* UOOM */')
    rewrite("m_config.c",
            r'            \* \(float \*\) def->location = \(float\) atof\(value\);',
            '            /* UOOM: atof pulls in the double parser and with it\n'
            '             * __aeabi_d2f, and libgcc is discarded on this\n'
            '             * platform. strtod is provided; the narrowing is the\n'
            '             * same one atof did. */\n'
            '            * (float *) def->location = (float) strtod(value, 0);')

    # --- the last two float sites ----------------------------------------
    #
    # Between them these are the whole reason __aeabi_f2d / __aeabi_dcmplt are
    # referenced at all.
    rewrite("g_game.c",
            r'        float fps;\n',
            '        /* UOOM: was a float FPS report, which needs __aeabi_f2d\n'
            '         * and a float-capable printf. Fixed point instead. */\n'
            '        int fps_x10;\n')
    rewrite("g_game.c",
            r'        fps = \(\(float\) gametic \* TICRATE\) / realtics;\n',
            '        fps_x10 = (int) (((long) gametic * TICRATE * 10) / realtics);\n')
    rewrite("g_game.c",
            r'\tI_Error \("timed %i gametics in %i realtics \(%f fps\)",\n'
            r'                 gametic, realtics, fps\);',
            '\tI_Error ("timed %i gametics in %i realtics (%i.%i fps)",\n'
            '                 gametic, realtics, fps_x10 / 10, fps_x10 % 10);')

    rewrite("v_video.c",
            r'void V_DrawMouseSpeedBox\(int speed\)\n\{[ \t]*\n',
            'void V_DrawMouseSpeedBox(int speed)\n'
            '{\n'
            '    /* UOOM: reachable only with -testcontrols, and its fabs() is\n'
            '     * the last soft-float reference in the binary. */\n'
            '    (void) speed;\n'
            '    if (1) return;\n')


# ------------------------------------------------------------------ 0013 native
#
# Opt-in (--native). DOOMGENERIC_RESX/RESY do *not* set DOOM's render
# resolution -- SCREENWIDTH/SCREENHEIGHT are compile-time constants in
# i_video.h, and RESX/RESY only size doomgeneric's own output buffer, which
# this port does not use. Rendering natively at 240x240 therefore means editing
# these two lines. Every width-dependent renderer array shrinks with them:
# visplane_t carries top[SCREENWIDTH] and bottom[SCREENWIDTH], and MAXOPENINGS
# is SCREENWIDTH*64.
#
# The cost is that the status bar is a 320-pixel-wide graphic and a fair amount
# of menu art assumes a 320-pixel canvas; both get clipped on the right.

def patch_native():
    rewrite("i_video.h", r'#define SCREENWIDTH  320',
            '#define SCREENWIDTH  240        /* UOOM: native panel width */')
    rewrite("i_video.h", r'#define SCREENHEIGHT 200',
            '#define SCREENHEIGHT 240        /* UOOM: native panel height */')


def main():
    global DG
    args = [a for a in sys.argv[1:] if a != "--native"]
    native = "--native" in sys.argv
    if len(args) != 1:
        sys.exit("usage: apply-uoom-patches.py [--native] "
                 "<third_party/doomgeneric>")
    DG = Path(args[0]) / "doomgeneric"
    if not (DG / "d_main.c").exists():
        sys.exit(f"not a doomgeneric checkout: {DG}")

    for name, fn in (
        ("0001 video hook", patch_video),
        ("0002 system / zone", patch_system),
        ("0003 renderer limits", patch_limits),
        ("0004 input drain fix", patch_input),
        ("0005 no screen wipes", patch_wipes),
        ("0006 no stdio", patch_stdio),
        ("0007 latent-bug guards", patch_safety),
        ("0009 RAM diet, stage 1", patch_ram),
        ("0010 RAM diet, stage 2", patch_ram2),
        ("0011 FixedDiv without libgcc", patch_fixeddiv),
        ("0012 the last of the stdio", patch_nostdio2),
    ):
        print(name)
        fn()

    if native:
        print("0013 native 240x240 (opt-in)")
        patch_native()

    print(f"\n{edits} edits applied")


if __name__ == "__main__":
    main()
