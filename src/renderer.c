#include "renderer.h"
#include "entities.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Grade interna 3D dinamica com limites maximos estaticos */
#define MAX_R_WIDTH  420
#define MAX_R_HEIGHT 210

/* Alturas relativas do olho da camera em relacao ao chao e ao teto (sala mais alta) */
#define EYE_FLOOR_H 0.50f
#define EYE_CEIL_H  1.18f

static int r_width = 320;
static int r_height = 160;
static int current_quality_mode = ASCII_QUALITY_AUTO;

static AsciiCell screen_grid[MAX_R_HEIGHT][MAX_R_WIDTH];
static float z_buffer[MAX_R_WIDTH];
static Font ui_font;
static int font_loaded = 0;

/* Estado da camera e animacao de caminhada sincronizados entre paredes, arma e sprites */
static float cam_pitch = 0.0f;
static int cam_third_person = 0;

static Color character_color(int material) {
    CharacterColor c = player_get_material_color(material);
    return (Color){c.r, c.g, c.b, c.a};
}

static float cam_view_x = 0.0f;
static float cam_view_y = 0.0f;
static float cam_view_angle = 0.0f;
static int cam_view_ready = 0;
static float prev_bob_phase = 0.0f;
static float walk_amp = 0.0f;

static void renderer_draw_enemy_projectiles(const Player *player);

#define ENEMY_ART_WIDTH 9
#define ENEMY_ART_HEIGHT 12
#define ENEMY_ART_MAX_HEIGHT 13

typedef struct {
    char label;
    float width_ratio;
    float height_scale;
    const char *rows[ENEMY_ART_MAX_HEIGHT];
    AsciiColor palette[5];
} EnemyDesign;

/* Art tokens map to body, secondary, shadow, glow and detail palette entries. */
static const EnemyDesign ENEMY_DESIGNS[] = {
    {'Z', 0.60f, 0.92f, {
        "....^....", "...###...", "..#+++#..", "..#*@*#..",
        "...###...", "..#####..", ".###+###.", ".###+###.",
        "...#=#...", "..##=##..", "..#...#..", ".##...##."
    }, {{120,150,120}, {155,180,145}, {45,65,52}, {220,235,170}, {200,205,185}}},
    {'D', 0.72f, 1.05f, {
        "...^^^...", "..#####..", "..#+++#..", "..#*@*#..",
        ".#######.", "#########", "###+#####", "###+#####",
        "..##=##..", ".###=###.", ".##...##.", "##.....##"
    }, {{110,130,160}, {155,175,205}, {35,50,75}, {210,230,255}, {180,195,220}}},
    {'I', 0.62f, 0.98f, {
        "....^....", "...###...", "..##+##..", "..#*@*#..",
        "..#####..", ".#######.", "..##+##..", "...##=##.",
        "...#.#...", "..##.##..", "..#...#..", ".##...##."
    }, {{160,150,100}, {195,185,125}, {65,58,35}, {245,225,120}, {215,205,165}}},
    {'M', 0.62f, 1.10f, {
        "..DBBBD..", "..BEBEB..", "..BCCCB..", ".SBBBBBS.",
        ".BBBDBBB.", "SB.BDB.BS", "B..BBB..B", "C..DBD..C",
        "..BBDBB..", "..BB.BB..", ".SBB.BBS.", "..BB.BB..",
        ".CCC.CCC."
    }, {{140,85,45}, {100,60,30}, {65,35,25}, {235,30,30}, {235,230,215}}},
    {'Y', 1.12f, 0.76f, {
        ".........", ".........", "..####...", ".#######.",
        ".##+++##.", ".#*@#*@#.", "#########", "##===####",
        "#########", ".##...##.", ".........", "........."
    }, {{220,120,140}, {240,155,165}, {90,35,48}, {255,215,205}, {245,185,175}}},
    {'C', 1.02f, 0.94f, {
        ".........", "...###...", "..#####..", ".#######.",
        "##@@@@@##", "##@***@##", "##@@@@@##", ".#######.",
        "..##!##..", "...!!!...", ".........", "........."
    }, {{210,40,40}, {170,28,30}, {65,10,20}, {220,235,90}, {245,210,160}}},
    {'B', 1.08f, 1.20f, {
        "^.......^", "##^...^##", ".#######.", "..#+++#..",
        "..#*@*#..", ".#######.", "#########", "###+#####",
        "..##=##..", "..##=##..", ".##...##.", "##.....##"
    }, {{140,30,30}, {180,65,58}, {48,8,12}, {255,125,70}, {225,150,115}}},
    {'K', 0.48f, 0.68f, {
        "....+....", "...###...", "...#@#...", "...###...",
        "....#....", "..#####..", "..##+##..", "....#....",
        "...##=##.", "..#...#..", "..#...#..", "........."
    }, {{150,155,165}, {215,225,235}, {52,60,70}, {250,255,255}, {190,210,220}}},
    {'T', 1.14f, 1.02f, {
        "..#####..", ".#######.", "#########", "#.####.##",
        "##@###@##", ".########", "#########", "###++++##",
        "=##=###=.", "=##=###=.", "..##..#..", ".##...##."
    }, {{80,110,50}, {110,140,68}, {34,48,25}, {180,195,80}, {130,150,85}}},
    {'A', 0.68f, 1.00f, {
        "....^^...", "...####..", "...#++#..", "..##*@##.",
        "..######.", "..#######", "..##+##..", "..##=##..",
        "..#..##..", ".##...#..", "##.......", "..#......"
    }, {{100,40,130}, {155,70,185}, {38,12,52}, {235,170,255}, {200,100,235}}},
    {'Q', 1.12f, 1.28f, {
        "^..###..^", "##^###^##", ".###+###.", "..#####..",
        ".##@*@##.", "#########", "###+#####", "##=###=##",
        "##=###=##", ".##===##.", "##.....##", "##.....##"
    }, {{40,10,15}, {90,20,28}, {14,5,8}, {210,170,60}, {255,220,110}}}
};

static const EnemyDesign ENEMY_DESIGN_FALLBACK = {
    '?', 0.62f, 1.0f, {
        "....^....", "...###...", "..#+++#..", "..#*@*#..",
        "...###...", "..#####..", ".###+###.", ".###+###.",
        "...#=#...", "..##=##..", "..#...#..", ".##...##."
    }, {{170,150,170}, {205,185,205}, {65,55,70}, {255,210,230}, {225,195,220}}
};

static const EnemyDesign *enemy_design_for_label(char label) {
    int design_count = (int)(sizeof(ENEMY_DESIGNS) / sizeof(ENEMY_DESIGNS[0]));
    for (int i = 0; i < design_count; i++) {
        if (ENEMY_DESIGNS[i].label == label) return &ENEMY_DESIGNS[i];
    }
    return &ENEMY_DESIGN_FALLBACK;
}

static int enemy_art_color_index(char glyph) {
    switch (glyph) {
        case '#': return 0;
        case '+': return 1;
        case '=': return 2;
        case '@':
        case '*': return 3;
        case '^':
        case '!':
        case '|': return 4;
        default: return -1;
    }
}

static int imp_art_color_index(char token) {
    switch (token) {
        case 'B': return 0;
        case 'D': return 1;
        case 'E': return 3;
        case 'S':
        case 'C': return 4;
        default: return -1;
    }
}

static char imp_art_glyph(char token) {
    switch (token) {
        case 'B': return '#';
        case 'D': return '%';
        case 'E': return 'O';
        case 'S': return '^';
        case 'C': return 'v';
        default: return token;
    }
}

#define REDESIGN_ART_MAX_WIDTH 17
#define REDESIGN_ART_MAX_HEIGHT 20
#define REDESIGN_IMP_HEIGHT_SCALE 1.10f

typedef struct {
    char glyph;
    AsciiColor color;
    unsigned char brighten;
} EnemyArtStyle;

typedef struct {
    char label;
    int width;
    int height;
    float width_ratio;
    float world_height;
    char effect_token;
    AsciiColor effect_color;
    const EnemyArtStyle *styles;
    const char *rows[REDESIGN_ART_MAX_HEIGHT];
} EnemyVisualDesign;

#define ENEMY_STYLE(token, glyph, red, green, blue, body) \
    [token] = {glyph, {red, green, blue}, body}

static const EnemyArtStyle LEGACY_Z_STYLE[128] = {
    ENEMY_STYLE('S', '#', 120, 150, 120, 1),
    ENEMY_STYLE('D', '%', 85, 110, 85, 1),
    ENEMY_STYLE('E', 'o', 200, 40, 40, 0),
    ENEMY_STYLE('R', '=', 70, 80, 60, 0)
};

static const EnemyArtStyle LEGACY_D_STYLE[128] = {
    ENEMY_STYLE('S', '#', 110, 130, 160, 1),
    ENEMY_STYLE('D', '%', 80, 95, 120, 1),
    ENEMY_STYLE('E', 'o', 200, 40, 40, 0),
    ENEMY_STYLE('R', '=', 50, 60, 90, 0)
};

static const EnemyArtStyle LEGACY_I_STYLE[128] = {
    ENEMY_STYLE('S', '#', 160, 150, 100, 1),
    ENEMY_STYLE('D', '%', 120, 110, 70, 1),
    ENEMY_STYLE('E', 'o', 200, 40, 40, 0),
    ENEMY_STYLE('R', '=', 95, 85, 55, 0)
};

static const EnemyArtStyle PINKY_STYLE[128] = {
    ENEMY_STYLE('P', '#', 225, 125, 145, 1),
    ENEMY_STYLE('R', '%', 170, 85, 105, 1),
    ENEMY_STYLE('G', 'o', 230, 190, 40, 0),
    ENEMY_STYLE('M', '=', 120, 20, 30, 0),
    ENEMY_STYLE('T', 'v', 240, 235, 225, 0),
    ENEMY_STYLE('H', '^', 235, 230, 205, 0),
    ENEMY_STYLE('C', 'v', 240, 235, 225, 0)
};

static const EnemyArtStyle CACODEMON_STYLE[128] = {
    ENEMY_STYLE('R', '#', 210, 40, 40, 1),
    ENEMY_STYLE('D', '%', 150, 25, 30, 1),
    ENEMY_STYLE('E', 'O', 235, 225, 90, 0),
    ENEMY_STYLE('P', '@', 25, 90, 35, 0),
    ENEMY_STYLE('H', '^', 235, 230, 205, 0),
    ENEMY_STYLE('M', '=', 60, 10, 15, 0),
    ENEMY_STYLE('T', 'v', 240, 235, 225, 0)
};

static const EnemyArtStyle BARON_STYLE[128] = {
    ENEMY_STYLE('P', '#', 215, 120, 130, 1),
    ENEMY_STYLE('D', '%', 160, 80, 95, 1),
    ENEMY_STYLE('F', '@', 225, 220, 200, 0),
    ENEMY_STYLE('E', 'O', 235, 40, 30, 0),
    ENEMY_STYLE('H', '^', 190, 185, 165, 0),
    ENEMY_STYLE('L', '#', 120, 75, 40, 0),
    ENEMY_STYLE('K', '%', 85, 50, 25, 0),
    ENEMY_STYLE('N', 'v', 240, 140, 40, 0),
    ENEMY_STYLE('X', '=', 35, 28, 25, 0)
};

static const EnemyArtStyle KUSHAN_STYLE[128] = {
    ENEMY_STYLE('M', '#', 150, 155, 165, 1),
    ENEMY_STYLE('D', '%', 100, 105, 115, 1),
    ENEMY_STYLE('H', '^', 210, 215, 225, 0),
    ENEMY_STYLE('F', 'o', 150, 100, 70, 0),
    ENEMY_STYLE('W', ')', 140, 95, 50, 0),
    ENEMY_STYLE('R', '=', 170, 40, 40, 0)
};

static const EnemyArtStyle TROLL_STYLE[128] = {
    ENEMY_STYLE('G', '#', 80, 110, 50, 1),
    ENEMY_STYLE('D', '%', 55, 80, 35, 1),
    ENEMY_STYLE('E', 'o', 230, 200, 60, 0),
    ENEMY_STYLE('N', '*', 210, 120, 120, 0),
    ENEMY_STYLE('T', 'v', 235, 230, 210, 0),
    ENEMY_STYLE('W', '|', 110, 75, 40, 0)
};

static const EnemyArtStyle APOSTLE_STYLE[128] = {
    ENEMY_STYLE('V', '#', 100, 40, 130, 1),
    ENEMY_STYLE('D', '%', 60, 20, 85, 1),
    ENEMY_STYLE('E', 'O', 200, 230, 70, 0),
    ENEMY_STYLE('T', 'v', 235, 230, 210, 0),
    ENEMY_STYLE('C', 'v', 225, 215, 190, 0)
};

static const EnemyArtStyle ZODD_STYLE[128] = {
    ENEMY_STYLE('B', '#', 35, 25, 30, 1),
    ENEMY_STYLE('D', '%', 80, 15, 25, 1),
    ENEMY_STYLE('E', 'O', 230, 170, 50, 0),
    ENEMY_STYLE('H', '^', 225, 215, 190, 0),
    ENEMY_STYLE('W', '*', 90, 20, 35, 0),
    ENEMY_STYLE('T', 'v', 240, 235, 225, 0),
    ENEMY_STYLE('C', 'v', 225, 215, 190, 0),
    ENEMY_STYLE('X', '=', 50, 45, 45, 0)
};

/* Grades validadas: cada linha corresponde a largura declarada. */
static const EnemyVisualDesign ENEMY_VISUAL_DESIGNS[] = {
    {'Z', 9, 12, 0.60f, 0.95f, '\0', {0,0,0}, LEGACY_Z_STYLE, {
        "..DSSSD..", "..SESES..", "..SSDSS..", "...SSS...",
        ".RRRRRRR.", "SRRRDRRRS", "S.RRRRR.S", "S.RRDRR.S",
        "..RRRRR..", "..RR.RR..", "..SS.SS..", "..DD.DD.."
    }},
    {'D', 9, 12, 0.72f, 1.05f, '\0', {0,0,0}, LEGACY_D_STYLE, {
        "..DSSSD..", "..SESES..", "..SSDSS..", "...SSS...",
        ".RRRRRRR.", "SRRRDRRRS", "S.RRRRR.S", "S.RRDRR.S",
        "..RRRRR..", "..RR.RR..", "..SS.SS..", "..DD.DD.."
    }},
    {'I', 9, 12, 0.62f, 1.00f, '\0', {0,0,0}, LEGACY_I_STYLE, {
        "..DSSSD..", "..SESES..", "..SSDSS..", "...SSS...",
        ".RRRRRRR.", "SRRRDRRRS", "S.RRRRR.S", "S.RRDRR.S",
        "..RRRRR..", "..RR.RR..", "..SS.SS..", "..DD.DD.."
    }},
    {'Y', 13, 10, 1.25f, 0.85f, '\0', {0,0,0}, PINKY_STYLE, {
        "...H.....H...", "..RPPPPPPPR..", "..PGPPPPPGP..",
        "..PMTMTMTMP..", ".PPRPPPPPRPP.", "PPPPRPPPRPPPP",
        "RPP.RPPPR.PPR", "CP..RPPPR..PC", "..PPP...PPP..",
        ".CCCC...CCCC."
    }},
    {'C', 13, 11, 1.15f, 0.95f, 'M', {255,160,40}, CACODEMON_STYLE, {
        "...H.....H...", "...DRRRRRD...", "..DRRRRRRRD..",
        ".DRRREEERRRD.", ".RRREEPEERRR.", ".DRRREEERRRD.",
        "RRRRRRRRRRRRR", "RDMTMTMTMTMDR", "RRDMMMMMMMDRR",
        ".DRDDDDDDDRD.", "..DDDDDDDDD.."
    }},
    {'B', 13, 17, 1.20f, 1.30f, '\0', {0,0,0}, BARON_STYLE, {
        "H...........H", "HH..FFFFF..HH", ".H..FEFEF..H.",
        "...FFFFFFF...", "..PPPPPPPPP..", ".PPDPPPPPDPP.",
        "PPPDPPPPPDPPP", "PPDPPPPPPPDPP", "PP.DPPPPPD.PP",
        "NN.PPDPDPP.NN", "NN..PPPPP..NN", "....DPPPD....",
        "...LLL.LLL...", "...LKL.LKL...", "....LL.LL....",
        "...LL...LL...", "..XXX...XXX.."
    }},
    {'K', 9, 11, 0.66f, 0.85f, 'W', {255,230,120}, KUSHAN_STYLE, {
        "....H....", "..DHHHD..", "..DFFFD..", "..DMMMD..",
        ".MMMMMMM.", "WMMMDMMM.", "W.MRRRM..", "W.MMDMM..",
        "W..MMM...", "...M.M...", "..DD.DD.."
    }},
    {'T', 15, 16, 1.20f, 1.40f, '\0', {0,0,0}, TROLL_STYLE, {
        "....DGGGGGD.WW.", "...DGEGGGEGD.WW", "...GGGNNNGGG.WW",
        "...DGTGGGTGD..W", "..GGGGGGGGGGG.W", ".GGDGGGGGGGDGGW",
        "GGGGDGGGGGDGGGW", "GGDG.GGGGG.GGGW", "GG.G.DGGGD.G.GW",
        "DD....GGGGG..DD", "....DGGGGGD....", "....GGDGDGG....",
        "....GG...GG....", "...GGG...GGG...", "...GGG...GGG...",
        "..DDDD...DDDD.."
    }},
    {'A', 11, 14, 0.95f, 1.10f, '\0', {0,0,0}, APOSTLE_STYLE, {
        "..DVVD.....", ".DEVEVD....", ".VVVTTVV...", "..DVVVVVD..",
        ".VVVDVVVVV.", "VVV.VVVV.VV", "VV..DVVV..V",
        "C...VVVD..V", "....DVVV..V", "....VVDV..V",
        "....VV.VV.V", "...VV..VV.C", "...VV..VV..",
        "..CCC.CCC.."
    }},
    {'Q', 17, 20, 1.45f, 1.70f, 'E', {255,60,40}, ZODD_STYLE, {
        "H...............H", "HH....BBBBB....HH", ".H...DBEBEBD...H.",
        "..W..DBTTTBD..W..", ".WW..DBBBBBD..WW.", "WWW.DBBBBBBBD.WWW",
        "WWWWDBBDBDBBDWWWW", "WWWWBBBBBBBBBWWWW",
        ".WWWBBDBBBDBBWWW.", ".WWBBBBBBBBBBBWW.",
        "..WBBBBDBDBBBBW..", "...BBBBBBBBBBB...",
        "..CBBB.BBB.BBBC..", "..CBB..BDB..BBC..",
        ".....BBBBBBB.....", ".....BBDBDBB.....",
        "....BBB...BBB....", "....BBB...BBB....",
        "....DDD...DDD....", "...XXXX...XXXX..."
    }}
};

#undef ENEMY_STYLE

static const EnemyVisualDesign *enemy_visual_design_for_label(char label) {
    int design_count = (int)(sizeof(ENEMY_VISUAL_DESIGNS) / sizeof(ENEMY_VISUAL_DESIGNS[0]));
    for (int i = 0; i < design_count; i++) {
        if (ENEMY_VISUAL_DESIGNS[i].label == label) return &ENEMY_VISUAL_DESIGNS[i];
    }
    return NULL;
}

#define ENEMY_SPRITE_PIPELINE_ENABLED 1
#define SPRITE_UPSCALE 4
#define SPRITE_SOURCE_MAX_WIDTH 17
#define SPRITE_SOURCE_MAX_HEIGHT 20
#define SPRITE_UPSCALED_MAX_WIDTH (SPRITE_SOURCE_MAX_WIDTH * SPRITE_UPSCALE + 2)
#define SPRITE_UPSCALED_MAX_HEIGHT (SPRITE_SOURCE_MAX_HEIGHT * SPRITE_UPSCALE + 2)
#define SPRITE_MIP_HALF_MAX_WIDTH ((SPRITE_UPSCALED_MAX_WIDTH + 1) / 2)
#define SPRITE_MIP_HALF_MAX_HEIGHT ((SPRITE_UPSCALED_MAX_HEIGHT + 1) / 2)
#define SPRITE_MIP_QUARTER_MAX_WIDTH ((SPRITE_UPSCALED_MAX_WIDTH + 3) / 4)
#define SPRITE_MIP_QUARTER_MAX_HEIGHT ((SPRITE_UPSCALED_MAX_HEIGHT + 3) / 4)

#if SPRITE_UPSCALE != 2 && SPRITE_UPSCALE != 4
#error SPRITE_UPSCALE must be 2 or 4
#endif

enum {
    ENEMY_VISUAL_DESIGN_COUNT = sizeof(ENEMY_VISUAL_DESIGNS) / sizeof(ENEMY_VISUAL_DESIGNS[0]),
    ENEMY_IMP_CACHE_INDEX = ENEMY_VISUAL_DESIGN_COUNT,
    ENEMY_FALLBACK_CACHE_INDEX = ENEMY_VISUAL_DESIGN_COUNT + 1,
    ENEMY_SPRITE_CACHE_COUNT = ENEMY_VISUAL_DESIGN_COUNT + 2
};

typedef struct {
    char glyph;
    char token;
    AsciiColor color;
} EnemyRenderCell;

typedef struct {
    int source_width;
    int source_height;
    int full_width;
    int full_height;
    int half_width;
    int half_height;
    int quarter_width;
    int quarter_height;
    EnemyRenderCell full[SPRITE_UPSCALED_MAX_HEIGHT][SPRITE_UPSCALED_MAX_WIDTH];
    EnemyRenderCell half[SPRITE_MIP_HALF_MAX_HEIGHT][SPRITE_MIP_HALF_MAX_WIDTH];
    EnemyRenderCell quarter[SPRITE_MIP_QUARTER_MAX_HEIGHT][SPRITE_MIP_QUARTER_MAX_WIDTH];
} EnemySpriteCache;

_Static_assert(sizeof(EnemyRenderCell) == 5, "Unexpected enemy render cell size");

static EnemySpriteCache enemy_sprite_caches[ENEMY_SPRITE_CACHE_COUNT];
static EnemyRenderCell enemy_sprite_source[SPRITE_SOURCE_MAX_HEIGHT][SPRITE_SOURCE_MAX_WIDTH];
static EnemyRenderCell enemy_sprite_scale2[SPRITE_SOURCE_MAX_HEIGHT * 2][SPRITE_SOURCE_MAX_WIDTH * 2];
static int enemy_sprite_pipeline_ready = 0;

/* Rampa 100% densa de alta cobertura (sem pontos vazios furando superficies) */
static const char *DENSE_RAMP = "=-+*xX#%8&BWM@";
static const int DENSE_RAMP_LEN = 14;

static AsciiColor make_color(unsigned char r, unsigned char g, unsigned char b) {
    AsciiColor c = {r, g, b};
    return c;
}

static unsigned char clamp_u8(float v) {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (unsigned char)v;
}

static float clamp_f01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static AsciiColor scale_rgb(float r, float g, float b, float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.35f) factor = 1.35f;
    return make_color(clamp_u8(r * factor), clamp_u8(g * factor), clamp_u8(b * factor));
}

static EnemyRenderCell enemy_source_cell(char label, const EnemyVisualDesign *visual,
                                         const EnemyDesign *source, int x, int y) {
    EnemyRenderCell cell = {0};
    char token = visual ? visual->rows[y][x] : source->rows[y][x];
    if (token == '.') return cell;

    if (visual) {
        EnemyArtStyle style = visual->styles[(unsigned char)token];
        if (style.glyph) {
            cell.glyph = style.glyph;
            cell.token = token;
            cell.color = style.color;
        }
    } else if (label == 'M') {
        int color_index = imp_art_color_index(token);
        if (color_index >= 0) {
            cell.glyph = imp_art_glyph(token);
            cell.token = token;
            cell.color = source->palette[color_index];
        }
    } else {
        int color_index = enemy_art_color_index(token);
        if (color_index >= 0) {
            cell.glyph = token;
            cell.token = token;
            cell.color = source->palette[color_index];
        }
    }
    return cell;
}

static EnemyRenderCell enemy_pixel_at(const EnemyRenderCell *pixels, int width,
                                      int height, int stride, int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        EnemyRenderCell empty = {0};
        return empty;
    }
    return pixels[y * stride + x];
}

static int enemy_pixels_equal(EnemyRenderCell a, EnemyRenderCell b) {
    if (!a.glyph || !b.glyph) return !a.glyph && !b.glyph;
    return a.glyph == b.glyph && a.token == b.token &&
           a.color.r == b.color.r && a.color.g == b.color.g &&
           a.color.b == b.color.b;
}

static void enemy_scale2x(const EnemyRenderCell *source, int width, int height,
                          int source_stride, EnemyRenderCell *dest, int dest_stride) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            EnemyRenderCell e = enemy_pixel_at(source, width, height, source_stride, x, y);
            EnemyRenderCell b = enemy_pixel_at(source, width, height, source_stride, x, y - 1);
            EnemyRenderCell d = enemy_pixel_at(source, width, height, source_stride, x - 1, y);
            EnemyRenderCell f = enemy_pixel_at(source, width, height, source_stride, x + 1, y);
            EnemyRenderCell h = enemy_pixel_at(source, width, height, source_stride, x, y + 1);
            EnemyRenderCell e0 = e, e1 = e, e2 = e, e3 = e;

            if (!enemy_pixels_equal(b, h) && !enemy_pixels_equal(d, f)) {
                if (enemy_pixels_equal(d, b)) e0 = d;
                if (enemy_pixels_equal(b, f)) e1 = f;
                if (enemy_pixels_equal(d, h)) e2 = d;
                if (enemy_pixels_equal(h, f)) e3 = h;
            }

            int dx = x * 2;
            int dy = y * 2;
            dest[dy * dest_stride + dx] = e0;
            dest[dy * dest_stride + dx + 1] = e1;
            dest[(dy + 1) * dest_stride + dx] = e2;
            dest[(dy + 1) * dest_stride + dx + 1] = e3;
        }
    }
}

static EnemyRenderCell enemy_box_filter(const EnemyRenderCell *source, int source_width,
                                         int source_height, int source_stride,
                                         int x0, int y0, int x1, int y1) {
    unsigned int red = 0, green = 0, blue = 0;
    int total = (x1 - x0) * (y1 - y0);
    int covered = 0;
    float center_x = ((float)x0 + (float)x1) * 0.5f;
    float center_y = ((float)y0 + (float)y1) * 0.5f;
    float closest = INFINITY;
    EnemyRenderCell selected = {0};

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            EnemyRenderCell cell = enemy_pixel_at(source, source_width, source_height,
                                                   source_stride, x, y);
            if (!cell.glyph) continue;

            red += cell.color.r;
            green += cell.color.g;
            blue += cell.color.b;
            covered++;

            float dx = ((float)x + 0.5f) - center_x;
            float dy = ((float)y + 0.5f) - center_y;
            float distance = dx * dx + dy * dy;
            if (distance < closest) {
                closest = distance;
                selected = cell;
            }
        }
    }

    if (total <= 0 || covered * 4 < total) {
        EnemyRenderCell empty = {0};
        return empty;
    }
    selected.color = make_color((unsigned char)((red + (unsigned int)(covered / 2)) / (unsigned int)covered),
                                (unsigned char)((green + (unsigned int)(covered / 2)) / (unsigned int)covered),
                                (unsigned char)((blue + (unsigned int)(covered / 2)) / (unsigned int)covered));
    return selected;
}

static void enemy_generate_mip(const EnemyRenderCell *source, int source_width,
                               int source_height, int source_stride, int factor,
                               EnemyRenderCell *dest, int dest_stride,
                               int *dest_width, int *dest_height) {
    *dest_width = (source_width + factor - 1) / factor;
    *dest_height = (source_height + factor - 1) / factor;
    for (int y = 0; y < *dest_height; y++) {
        for (int x = 0; x < *dest_width; x++) {
            int x0 = x * factor;
            int y0 = y * factor;
            int x1 = x0 + factor < source_width ? x0 + factor : source_width;
            int y1 = y0 + factor < source_height ? y0 + factor : source_height;
            dest[y * dest_stride + x] = enemy_box_filter(source, source_width,
                source_height, source_stride, x0, y0, x1, y1);
        }
    }
}

static void enemy_finish_sprite_cache(EnemySpriteCache *cache) {
    for (int y = 1; y < cache->full_height - 1; y++) {
        for (int x = 1; x < cache->full_width - 1; x++) {
            EnemyRenderCell *cell = &cache->full[y][x];
            if (!cell->token) continue;

            int lit = !cache->full[y][x - 1].glyph || !cache->full[y - 1][x].glyph;
            int shadow = !cache->full[y][x + 1].glyph || !cache->full[y + 1][x].glyph;
            float factor = lit ? 1.15f : (shadow ? 0.80f : 1.0f);
            cell->color = scale_rgb(cell->color.r, cell->color.g, cell->color.b, factor);
        }
    }

    for (int y = 0; y < cache->full_height; y++) {
        for (int x = 0; x < cache->full_width; x++) {
            EnemyRenderCell *cell = &cache->full[y][x];
            if (cell->glyph) continue;

            EnemyRenderCell *neighbor = NULL;
            for (int dy = -1; dy <= 1 && !neighbor; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx >= 0 && nx < cache->full_width &&
                        ny >= 0 && ny < cache->full_height &&
                        cache->full[ny][nx].token) {
                        neighbor = &cache->full[ny][nx];
                        break;
                    }
                }
            }
            if (neighbor) {
                cell->glyph = neighbor->glyph;
                cell->color = scale_rgb(neighbor->color.r, neighbor->color.g,
                                        neighbor->color.b, 0.65f);
            }
        }
    }

    enemy_generate_mip(&cache->full[0][0], cache->full_width, cache->full_height,
        SPRITE_UPSCALED_MAX_WIDTH, 2, &cache->half[0][0],
        SPRITE_MIP_HALF_MAX_WIDTH, &cache->half_width, &cache->half_height);
    enemy_generate_mip(&cache->full[0][0], cache->full_width, cache->full_height,
        SPRITE_UPSCALED_MAX_WIDTH, 4, &cache->quarter[0][0],
        SPRITE_MIP_QUARTER_MAX_WIDTH, &cache->quarter_width, &cache->quarter_height);
}

static void enemy_build_sprite_cache(EnemySpriteCache *cache, char label,
                                     int width, int height,
                                     const EnemyVisualDesign *visual,
                                     const EnemyDesign *source) {
    memset(enemy_sprite_source, 0, sizeof(enemy_sprite_source));
    memset(enemy_sprite_scale2, 0, sizeof(enemy_sprite_scale2));
    memset(cache, 0, sizeof(*cache));

    cache->source_width = width;
    cache->source_height = height;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            enemy_sprite_source[y][x] = enemy_source_cell(label, visual, source, x, y);
        }
    }

    int scale2_width = width * 2;
    int scale2_height = height * 2;
    enemy_scale2x(&enemy_sprite_source[0][0], width, height,
                  SPRITE_SOURCE_MAX_WIDTH, &enemy_sprite_scale2[0][0],
                  SPRITE_SOURCE_MAX_WIDTH * 2);

    cache->full_width = width * SPRITE_UPSCALE + 2;
    cache->full_height = height * SPRITE_UPSCALE + 2;
#if SPRITE_UPSCALE == 4
    enemy_scale2x(&enemy_sprite_scale2[0][0], scale2_width, scale2_height,
                  SPRITE_SOURCE_MAX_WIDTH * 2, &cache->full[1][1],
                  SPRITE_UPSCALED_MAX_WIDTH);
#else
    for (int y = 0; y < scale2_height; y++) {
        for (int x = 0; x < scale2_width; x++) {
            cache->full[y + 1][x + 1] = enemy_sprite_scale2[y][x];
        }
    }
#endif
    enemy_finish_sprite_cache(cache);
}

static void enemy_sprite_pipeline_init(void) {
    if (enemy_sprite_pipeline_ready || !ENEMY_SPRITE_PIPELINE_ENABLED) return;

    memset(enemy_sprite_caches, 0, sizeof(enemy_sprite_caches));
    for (int i = 0; i < ENEMY_VISUAL_DESIGN_COUNT; i++) {
        const EnemyVisualDesign *design = &ENEMY_VISUAL_DESIGNS[i];
        enemy_build_sprite_cache(&enemy_sprite_caches[i], design->label,
            design->width, design->height, design, NULL);
    }

    const EnemyDesign *imp = enemy_design_for_label('M');
    enemy_build_sprite_cache(&enemy_sprite_caches[ENEMY_IMP_CACHE_INDEX], 'M',
        ENEMY_ART_WIDTH, ENEMY_ART_MAX_HEIGHT, NULL, imp);
    enemy_build_sprite_cache(&enemy_sprite_caches[ENEMY_FALLBACK_CACHE_INDEX], '?',
        ENEMY_ART_WIDTH, ENEMY_ART_HEIGHT, NULL, &ENEMY_DESIGN_FALLBACK);
    enemy_sprite_pipeline_ready = 1;
}

static const EnemySpriteCache *enemy_sprite_cache_for_label(char label) {
    const EnemyVisualDesign *design = enemy_visual_design_for_label(label);
    if (design) return &enemy_sprite_caches[design - ENEMY_VISUAL_DESIGNS];
    if (label == 'M') return &enemy_sprite_caches[ENEMY_IMP_CACHE_INDEX];
    return &enemy_sprite_caches[ENEMY_FALLBACK_CACHE_INDEX];
}

static const EnemyRenderCell *enemy_select_sprite_mip(const EnemySpriteCache *cache,
                                                       int target_width, int target_height,
                                                       int *width, int *height, int *stride) {
    const EnemyRenderCell *pixels = &cache->full[0][0];
    *width = cache->full_width;
    *height = cache->full_height;
    *stride = SPRITE_UPSCALED_MAX_WIDTH;

    float reduce_x = (float)*width / (float)target_width;
    float reduce_y = (float)*height / (float)target_height;
    float reduction = fmaxf(reduce_x, reduce_y);
    if (reduction >= 4.0f) {
        pixels = &cache->quarter[0][0];
        *width = cache->quarter_width;
        *height = cache->quarter_height;
        *stride = SPRITE_MIP_QUARTER_MAX_WIDTH;
    } else if (reduction >= 2.0f) {
        pixels = &cache->half[0][0];
        *width = cache->half_width;
        *height = cache->half_height;
        *stride = SPRITE_MIP_HALF_MAX_WIDTH;
    }
    return pixels;
}

static EnemyRenderCell enemy_sample_sprite_mip(const EnemyRenderCell *pixels,
                                                int texture_width, int texture_height,
                                                int texture_stride, float u, float v,
                                                int target_width, int target_height) {
    if (texture_width <= target_width && texture_height <= target_height) {
        int x = (int)(u * (float)texture_width);
        int y = (int)(v * (float)texture_height);
        if (x >= texture_width) x = texture_width - 1;
        if (y >= texture_height) y = texture_height - 1;
        return enemy_pixel_at(pixels, texture_width, texture_height, texture_stride, x, y);
    }

    float x0f = u * (float)texture_width;
    float y0f = v * (float)texture_height;
    float x1f = (u + 1.0f / (float)target_width) * (float)texture_width;
    float y1f = (v + 1.0f / (float)target_height) * (float)texture_height;
    int x0 = (int)floorf(x0f);
    int y0 = (int)floorf(y0f);
    int x1 = (int)ceilf(x1f);
    int y1 = (int)ceilf(y1f);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 >= texture_width) x0 = texture_width - 1;
    if (y0 >= texture_height) y0 = texture_height - 1;
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x1 > texture_width) x1 = texture_width;
    if (y1 > texture_height) y1 = texture_height;

    return enemy_box_filter(pixels, texture_width, texture_height, texture_stride,
                            x0, y0, x1, y1);
}

static float bayer4x4(int x, int y) {
    static const int B[4][4] = {
        {  0,  8,  2, 10 },
        { 12,  4, 14,  6 },
        {  3, 11,  1,  9 },
        { 15,  7, 13,  5 }
    };
    return ((float)B[y & 3][x & 3] / 15.0f - 0.5f) * 0.28f;
}

static char sample_dense_ramp(float lum, int x, int y) {
    float v = clamp_f01(lum + bayer4x4(x, y));
    int idx = (int)(v * (float)(DENSE_RAMP_LEN - 1) + 0.5f);
    if (idx < 0) idx = 0;
    if (idx >= DENSE_RAMP_LEN) idx = DENSE_RAMP_LEN - 1;
    return DENSE_RAMP[idx];
}

static void set_cell(int x, int y, char ch, AsciiColor fg, AsciiColor bg) {
    if (x < 0 || x >= r_width || y < 0 || y >= r_height) {
        return;
    }
    screen_grid[y][x].ch = ch;
    screen_grid[y][x].fg = fg;
    screen_grid[y][x].bg = bg;
}

static const char *quality_short_label(int quality_mode) {
    switch (quality_mode) {
        case ASCII_QUALITY_LEVE:  return "LEVE";
        case ASCII_QUALITY_MEDIA: return "MEDIA";
        case ASCII_QUALITY_ALTA:  return "ALTA";
        case ASCII_QUALITY_ULTRA: return "ULTRA";
        default:                  return "AUTO";
    }
}

void renderer_set_ascii_density(int quality_mode, int screen_w, int screen_h) {
    if (quality_mode < ASCII_QUALITY_AUTO || quality_mode > ASCII_QUALITY_ULTRA) {
        quality_mode = ASCII_QUALITY_AUTO;
    }
    current_quality_mode = quality_mode;

    if (screen_w < 320) screen_w = 320;
    if (screen_h < 240) screen_h = 240;

    int target_w = 320;
    int target_h = 160;
    switch (quality_mode) {
        case ASCII_QUALITY_LEVE:  target_w = 180; target_h = 90;  break;
        case ASCII_QUALITY_MEDIA: target_w = 256; target_h = 128; break;
        case ASCII_QUALITY_ALTA:  target_w = 320; target_h = 160; break;
        case ASCII_QUALITY_ULTRA: target_w = 400; target_h = 200; break;
        case ASCII_QUALITY_AUTO:
        default: {
            int view_h = (int)((float)screen_h * 0.855f);
            target_w = (int)((float)screen_w / 4.0f);
            target_h = (int)((float)view_h / 4.2f);
            if (target_w > target_h * 2 + 24) {
                target_w = target_h * 2;
            }
            break;
        }
    }

    /* Trava anti-quebra em janela menor: garante celulas legiveis sem sobreposicao */
    int max_fit_w = screen_w / 3;
    int max_fit_h = ((screen_h * 85) / 100) / 3;
    if (target_w > max_fit_w) target_w = max_fit_w;
    if (target_h > max_fit_h) target_h = max_fit_h;

    if (target_w < 120) target_w = 120;
    if (target_h < 60)  target_h = 60;
    if (target_w > MAX_R_WIDTH)  target_w = MAX_R_WIDTH;
    if (target_h > MAX_R_HEIGHT) target_h = MAX_R_HEIGHT;

    /* Mantem dimensoes pares para alinhar o eixo central de mira e arma ao centro da janela */
    target_w &= ~1;
    target_h &= ~1;

    r_width = target_w;
    r_height = target_h;
}

static float wrap_angle_signed(float angle) {
    while (angle > PI_F) {
        angle -= 2.0f * PI_F;
    }
    while (angle < -PI_F) {
        angle += 2.0f * PI_F;
    }
    return angle;
}

static AsciiColor get_tile_base_color(char tile) {
    switch (tile) {
        case '#': return make_color(228, 72, 56);   /* Tijolo/aco vermelho DOOM */
        case '@': return make_color(72, 158, 245);  /* Painel metal azul tech */
        case '%': return make_color(68, 215, 118);  /* Coluna bio-reator verde */
        default:  return make_color(224, 170, 68);  /* Ambar industrial */
    }
}

void renderer_set_camera_view(float pitch_offset, int third_person) {
    if (pitch_offset < -62.0f) pitch_offset = -62.0f;
    if (pitch_offset > 62.0f) pitch_offset = 62.0f;
    cam_pitch = pitch_offset;
    cam_third_person = third_person ? 1 : 0;
}

void renderer_init(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "ASCII DOOM C - 165 FPS (Raylib)");
    SetWindowMinSize(640, 480);
    SetExitKey(KEY_NULL);
    SetTargetFPS(TARGET_FPS);
    ui_font = GetFontDefault();
    SetTextureFilter(ui_font.texture, TEXTURE_FILTER_POINT);
    font_loaded = 1;
    cam_pitch = 0.0f;
    cam_third_person = 0;
    cam_view_ready = 0;
    prev_bob_phase = 0.0f;
    walk_amp = 0.0f;
    enemy_sprite_pipeline_init();
}

void renderer_close(void) {
    if (IsWindowReady()) {
        if (IsCursorHidden()) {
            EnableCursor();
        }
        CloseWindow();
    }
    font_loaded = 0;
}

static const GameMap *cam_view_map = NULL;

void renderer_clear(void) {
    AsciiColor black = make_color(0, 0, 0);
    cam_view_ready = 0;
    cam_view_map = NULL;
    for (int y = 0; y < r_height; y++) {
        for (int x = 0; x < r_width; x++) {
            screen_grid[y][x].ch = ' ';
            screen_grid[y][x].fg = black;
            screen_grid[y][x].bg = black;
        }
    }
    for (int x = 0; x < r_width; x++) {
        z_buffer[x] = MAX_DEPTH;
    }
}

void renderer_draw_walls(const GameMap *map, const Player *player) {
    if (!map || !player) {
        return;
    }
    cam_view_map = map;

    /* Rastreia cadencia de passos para animar head-bob, arma e 3a pessoa com suavidade */
    float d_bob = fabsf(player->bob_phase - prev_bob_phase);
    prev_bob_phase = player->bob_phase;
    if (d_bob > 0.0005f) {
        walk_amp += 0.18f;
        if (walk_amp > 1.0f) walk_amp = 1.0f;
    } else {
        walk_amp *= 0.86f;
        if (walk_amp < 0.01f) walk_amp = 0.0f;
    }

    cam_view_x = player->x;
    cam_view_y = player->y;
    cam_view_angle = player->angle;

    if (cam_third_person) {
        /* Camera 3a pessoa levemente sobre o ombro direito para manter mira desobstruida */
        float raw_bx = -cosf(player->angle) - sinf(player->angle) * 0.19f;
        float raw_by = -sinf(player->angle) + cosf(player->angle) * 0.19f;
        float norm = sqrtf(raw_bx * raw_bx + raw_by * raw_by);
        float back_x = raw_bx / norm;
        float back_y = raw_by / norm;
        float safe_dist = 0.0f;
        const float target_dist = 1.48f;
        const float step = 0.04f;
        const float margin = 0.16f;

        for (float d = step; d <= target_dist; d += step) {
            float tx = player->x + back_x * d;
            float ty = player->y + back_y * d;
            if (map_is_wall(map, (int)floorf(tx + margin), (int)floorf(ty)) ||
                map_is_wall(map, (int)floorf(tx - margin), (int)floorf(ty)) ||
                map_is_wall(map, (int)floorf(tx), (int)floorf(ty + margin)) ||
                map_is_wall(map, (int)floorf(tx), (int)floorf(ty - margin))) {
                break;
            }
            safe_dist = d;
        }
        cam_view_x = player->x + back_x * safe_dist;
        cam_view_y = player->y + back_y * safe_dist;
    }
    cam_view_ready = 1;

    AsciiColor bg_black = make_color(0, 0, 0);
    float scale_y = (float)r_height / 180.0f;
    float pitch_cells = cam_pitch * scale_y;
    float view_bob = cam_third_person ? 0.0f : (sinf(player->bob_phase * 2.0f) * 1.6f * walk_amp * scale_y);
    float horizon_y = (float)r_height * 0.5f + pitch_cells + view_bob;

    for (int x = 0; x < r_width; x++) {
        float ray_angle = (cam_view_angle - player->fov * 0.5f) +
                          ((float)x / (float)r_width) * player->fov;

        float ray_dir_x = cosf(ray_angle);
        float ray_dir_y = sinf(ray_angle);

        int map_x = (int)floorf(cam_view_x);
        int map_y = (int)floorf(cam_view_y);

        float delta_dist_x = (fabsf(ray_dir_x) < 1e-6f) ? 1e30f : fabsf(1.0f / ray_dir_x);
        float delta_dist_y = (fabsf(ray_dir_y) < 1e-6f) ? 1e30f : fabsf(1.0f / ray_dir_y);

        int step_x, step_y;
        float side_dist_x, side_dist_y;

        if (ray_dir_x < 0.0f) {
            step_x = -1;
            side_dist_x = (cam_view_x - (float)map_x) * delta_dist_x;
        } else {
            step_x = 1;
            side_dist_x = ((float)map_x + 1.0f - cam_view_x) * delta_dist_x;
        }

        if (ray_dir_y < 0.0f) {
            step_y = -1;
            side_dist_y = (cam_view_y - (float)map_y) * delta_dist_y;
        } else {
            step_y = 1;
            side_dist_y = ((float)map_y + 1.0f - cam_view_y) * delta_dist_y;
        }

        int hit = 0;
        int side = 0;
        int steps = 0;

        while (!hit && steps < 64) {
            if (side_dist_x < side_dist_y) {
                side_dist_x += delta_dist_x;
                map_x += step_x;
                side = 0;
            } else {
                side_dist_y += delta_dist_y;
                map_y += step_y;
                side = 1;
            }

            if (map_is_wall(map, map_x, map_y)) {
                hit = 1;
            }
            steps++;
        }

        float raw_dist = MAX_DEPTH;
        if (hit) {
            raw_dist = (side == 0) ? (side_dist_x - delta_dist_x)
                                   : (side_dist_y - delta_dist_y);
        }

        float cos_corr = cosf(ray_angle - cam_view_angle);
        if (cos_corr < 0.25f) cos_corr = 0.25f;
        float inv_cos = 1.0f / cos_corr;

        float perp_dist = raw_dist * cos_corr;
        if (perp_dist < 0.1f) perp_dist = 0.1f;
        if (perp_dist > MAX_DEPTH) perp_dist = MAX_DEPTH;

        z_buffer[x] = perp_dist;

        /* Coordenada horizontal U [0..1) na face da parede */
        float u = (side == 0)
            ? (cam_view_y + raw_dist * ray_dir_y)
            : (cam_view_x + raw_dist * ray_dir_x);
        u -= floorf(u);
        int is_block_edge = (u < 0.025f || u > 0.975f);

        float floor_proj = (EYE_FLOOR_H * (float)r_height) / perp_dist;
        float ceil_proj = (EYE_CEIL_H * (float)r_height) / perp_dist;
        int raw_start = (int)floorf(horizon_y - ceil_proj + 0.5f);
        int raw_end = (int)floorf(horizon_y + floor_proj + 0.5f);
        int line_height = raw_end - raw_start;
        if (line_height < 1) line_height = 1;
        int draw_start = (raw_start < 0) ? 0 : raw_start;
        int draw_end = (raw_end >= r_height) ? (r_height - 1) : raw_end;
        float inv_lh = 1.0f / (float)line_height;

        char tile = hit ? map_get_tile(map, map_x, map_y) : '#';
        AsciiColor base_wall = get_tile_base_color(tile);
        float wall_fog = 1.0f - (perp_dist / 13.5f);
        if (wall_fog < 0.16f) wall_fog = 0.16f;
        if (side == 1) wall_fog *= 0.72f;

        for (int y = 0; y < r_height; y++) {
            if (y < draw_start) {
                /* Teto 3D em perspectiva com vigas e paineis luminosos em micro-ASCII */
                float p_row = horizon_y - ((float)y + 0.5f);
                if (p_row < 0.5f) p_row = 0.5f;
                float row_dist = ((EYE_CEIL_H * (float)r_height) / p_row) * inv_cos;
                if (row_dist > MAX_DEPTH) continue;

                float wx = cam_view_x + ray_dir_x * row_dist;
                float wy = cam_view_y + ray_dir_y * row_dist;
                float cu = wx - floorf(wx);
                float cv = wy - floorf(wy);

                float cfog = 1.0f - (row_dist / 13.0f);
                if (cfog < 0.14f) cfog = 0.14f;

                int is_beam = (cu < 0.08f || cv < 0.08f);
                int is_light = (fabsf(cu - 0.5f) < 0.17f && fabsf(cv - 0.5f) < 0.17f);

                if (is_light) {
                    float lum = 0.92f * cfog;
                    char cch = sample_dense_ramp(lum, x, y);
                    set_cell(x, y, cch, scale_rgb(175.0f, 155.0f, 115.0f, lum + 0.12f), bg_black);
                } else if (is_beam) {
                    float lum = 0.48f * cfog;
                    set_cell(x, y, '=', scale_rgb(76.0f, 86.0f, 108.0f, lum + 0.14f), bg_black);
                } else {
                    float plate_tex = 0.32f + 0.10f * sinf(wx * 6.0f) * cosf(wy * 6.0f);
                    float lum = plate_tex * cfog;
                    char cch = sample_dense_ramp(lum, x, y);
                    set_cell(x, y, cch, scale_rgb(56.0f, 66.0f, 88.0f, lum + 0.16f), bg_black);
                }
            } else if (y <= draw_end) {
                /* Parede 3D com textura procedural UV em micro-ASCII denso */
                float v = clamp_f01(((float)(y - raw_start) + 0.5f) * inv_lh);
                float shade = wall_fog;
                float r_c = (float)base_wall.r;
                float g_c = (float)base_wall.g;
                float b_c = (float)base_wall.b;
                char force_ch = 0;

                if (is_block_edge && perp_dist < 10.0f) {
                    shade *= 0.62f;
                    force_ch = '|';
                } else if (tile == '#') {
                    /* Tijolos industriais alternados com argamassa */
                    float vy = v * 8.0f;
                    int brow = (int)floorf(vy);
                    float ux = u * 3.0f + ((brow & 1) ? 0.5f : 0.0f);
                    int bcol = (int)floorf(ux);
                    float fv = vy - floorf(vy);
                    float fu = ux - floorf(ux);
                    if (fv < 0.12f || fu < 0.08f) {
                        shade *= 0.52f;
                        force_ch = (fv < 0.12f) ? '=' : ':';
                    } else {
                        unsigned h = (unsigned)((brow * 73u) ^ (bcol * 139u) ^ (map_x * 31u + map_y * 17u));
                        float var = 0.84f + 0.24f * ((float)(h & 15u) / 15.0f);
                        shade *= var;
                    }
                } else if (tile == '@') {
                    /* Painel metalico tech com duto luminoso central */
                    int in_border = (u < 0.07f || u > 0.93f || v < 0.08f || v > 0.92f);
                    int in_conduit = (fabsf(u - 0.5f) < 0.06f) || (fabsf(v - 0.5f) < 0.045f);
                    if (in_conduit) {
                        r_c = 135.0f; g_c = 230.0f; b_c = 255.0f;
                        shade = fmaxf(shade * 1.25f, 0.72f);
                        force_ch = '@';
                    } else if (in_border) {
                        shade *= 0.65f;
                        force_ch = '#';
                    } else {
                        shade *= (0.85f + 0.15f * cosf((u - 0.5f) * 8.0f));
                    }
                } else if (tile == '%') {
                    /* Coluna bio-reator verde com aletas verticais */
                    float rib = fabsf(fmodf(u * 4.0f, 1.0f) - 0.5f);
                    if (rib < 0.14f) {
                        shade *= 0.58f;
                        force_ch = '#';
                    } else {
                        float pulse = 0.85f + 0.25f * sinf(v * 18.0f);
                        shade *= pulse;
                    }
                } else {
                    /* Faixas diagonais de perigo industrial */
                    float stripe = fmodf(u * 3.0f + v * 3.0f, 1.0f);
                    if (stripe < 0.40f) {
                        shade *= 0.58f;
                    }
                }

                /* Oclusao de ambiente discreta no topo e na base da parede */
                if (v < 0.06f || v > 0.94f) {
                    shade *= 0.72f;
                }

                char wch = force_ch ? force_ch : sample_dense_ramp(shade, x, y);
                set_cell(x, y, wch, scale_rgb(r_c, g_c, b_c, shade), bg_black);
            } else {
                /* Chao 3D em perspectiva com placas metalicas e rejunte em micro-ASCII */
                float p_row = ((float)y + 0.5f) - horizon_y;
                if (p_row < 0.5f) p_row = 0.5f;
                float row_dist = ((EYE_FLOOR_H * (float)r_height) / p_row) * inv_cos;
                if (row_dist > MAX_DEPTH) continue;

                float wx = cam_view_x + ray_dir_x * row_dist;
                float wy = cam_view_y + ray_dir_y * row_dist;
                /* Anel rosa no chao: borda continua no raio exato do bonus. */
                if (player->ability_state.zone_time > 0) {
                    float dx = wx - player->ability_state.zone_x;
                    float dy = wy - player->ability_state.zone_y;
                    float edge = fabsf(sqrtf(dx*dx + dy*dy) - TETO_ZONE_RADIUS);
                    float thickness = 0.07f + row_dist / r_height * 0.65f;
                    if (edge < thickness) {
                        set_cell(x, y, '#', make_color(255, 80, 170), bg_black);
                        continue;
                    }
                    if (edge < thickness * 2.0f) {
                        set_cell(x, y, '+', make_color(190, 55, 120), bg_black);
                        continue;
                    }
                }
                int floor_mx = (int)floorf(wx);
                int floor_my = (int)floorf(wy);
                char floor_raw = map_get_raw_tile(map, floor_mx, floor_my);
                int trap_unlit = map_is_trap_tile(floor_raw) && !map_is_trap_triggered(floor_mx, floor_my);
                int fire_lit = map_is_fire_active_at(map, floor_mx, floor_my);
                if (trap_unlit || fire_lit) {
                    float fu_m = wx - floorf(wx);
                    float fv_m = wy - floorf(wy);
                    float dx_m = fu_m - 0.5f;
                    float dy_m = fv_m - 0.5f;
                    float rad = sqrtf(dx_m * dx_m + dy_m * dy_m);
                    float adx = fabsf(dx_m);
                    float ady = fabsf(dy_m);
                    int outer_ring = (rad >= 0.29f && rad <= 0.37f);
                    int inner_ring = (rad >= 0.14f && rad <= 0.21f);
                    int core_jet = (rad < 0.09f);
                    int vane = (rad >= 0.09f && rad <= 0.37f) &&
                               (adx < 0.035f || ady < 0.035f || fabsf(adx - ady) < 0.04f);
                    if (outer_ring || inner_ring || core_jet || vane) {
                        float tfog = fmaxf(0.28f, 1.0f - (row_dist / 13.5f));
                        char tch = core_jet ? (fire_lit ? '@' : 'o')
                                 : (inner_ring ? (fire_lit ? '*' : 'O')
                                 : (vane ? '+' : '#'));
                        AsciiColor tcol;
                        if (fire_lit) {
                            tcol = core_jet ? scale_rgb(255.0f, 245.0f, 160.0f, tfog)
                                 : (inner_ring ? scale_rgb(255.0f, 125.0f, 25.0f, tfog)
                                 : (vane ? scale_rgb(245.0f, 80.0f, 20.0f, tfog)
                                         : scale_rgb(175.0f, 55.0f, 25.0f, tfog)));
                        } else {
                            tcol = core_jet ? scale_rgb(225.0f, 105.0f, 35.0f, tfog)
                                 : (inner_ring ? scale_rgb(170.0f, 85.0f, 40.0f, tfog)
                                 : (vane ? scale_rgb(135.0f, 145.0f, 155.0f, tfog)
                                         : scale_rgb(95.0f, 105.0f, 115.0f, tfog)));
                        }
                        set_cell(x, y, tch, tcol, bg_black);
                        continue;
                    }
                } else if (map_is_medkit_tile(floor_raw)) {
                    float fu_m = wx - floorf(wx);
                    float fv_m = wy - floorf(wy);
                    float dx_m = fabsf(fu_m - 0.5f);
                    float dy_m = fabsf(fv_m - 0.5f);
                    if (dx_m < 0.28f && dy_m < 0.28f) {
                        float mf = fmaxf(0.32f, 1.0f - (row_dist / 13.5f));
                        int active_kit = map_is_medkit_active_at(map, floor_mx, floor_my);
                        int is_cross = (dx_m < 0.07f && dy_m < 0.20f) || (dy_m < 0.07f && dx_m < 0.20f);
                        if (active_kit) {
                            set_cell(x, y, is_cross ? '+' : '#',
                                     is_cross ? scale_rgb(235.0f, 55.0f, 55.0f, mf)
                                              : scale_rgb(240.0f, 245.0f, 250.0f, mf),
                                     bg_black);
                        } else {
                            int on_edge = (dx_m > 0.21f || dy_m > 0.21f);
                            set_cell(x, y, is_cross ? '+' : (on_edge ? '#' : '='),
                                     is_cross ? scale_rgb(105.0f, 55.0f, 55.0f, mf)
                                              : scale_rgb(95.0f, 105.0f, 118.0f, mf),
                                     bg_black);
                        }
                        continue;
                    }
                }
                float fu = wx - floorf(wx);
                float fv = wy - floorf(wy);
                int checker = (((int)floorf(wx) + (int)floorf(wy)) & 1);

                float ffog = 1.0f - (row_dist / 13.5f);
                if (ffog < 0.18f) ffog = 0.18f;

                int is_seam = (fu < 0.055f || fv < 0.055f);
                int is_rivet = ((fu > 0.14f && fu < 0.20f) || (fu > 0.80f && fu < 0.86f)) &&
                               ((fv > 0.14f && fv < 0.20f) || (fv > 0.80f && fv < 0.86f));

                if (is_seam) {
                    float lum = 0.78f * ffog;
                    char fch = (fu < 0.055f && fv < 0.055f) ? '+' : '=';
                    set_cell(x, y, fch, scale_rgb(165.0f, 110.0f, 75.0f, lum + 0.10f), bg_black);
                } else if (is_rivet) {
                    float lum = 0.85f * ffog;
                    set_cell(x, y, '#', scale_rgb(185.0f, 145.0f, 95.0f, lum), bg_black);
                } else {
                    float base_tone = checker ? 0.54f : 0.40f;
                    float micro_tex = 0.08f * sinf(wx * 12.0f) * cosf(wy * 12.0f);
                    float lum = clamp_f01((base_tone + micro_tex) * ffog);
                    char fch = sample_dense_ramp(lum, x, y);
                    set_cell(x, y, fch, scale_rgb(138.0f, 112.0f, 92.0f, lum + 0.14f), bg_black);
                }
            }
        }
    }
}

static void renderer_draw_hazards(const GameMap *map, const Player *player) {
    if (!map || !player) {
        return;
    }
    float vx = cam_view_ready ? cam_view_x : player->x;
    float vy = cam_view_ready ? cam_view_y : player->y;
    float vangle = cam_view_ready ? cam_view_angle : player->angle;
    float scale_y = (float)r_height / 180.0f;
    float pitch_cells = cam_pitch * scale_y;
    float view_bob = cam_third_person ? 0.0f : (sinf(player->bob_phase * 2.0f) * 1.6f * walk_amp * scale_y);
    float horizon_y = (float)r_height * 0.5f + pitch_cells + view_bob;
    float time_now = IsWindowReady() ? (float)GetTime() : 0.0f;
    AsciiColor bg_black = make_color(0, 0, 0);

    for (int my = 0; my < map->height; my++) {
        for (int mx = 0; mx < map->width; mx++) {
            int is_fire = map_is_fire_active_at(map, mx, my);
            int is_medkit = !is_fire && map_is_medkit_active_at(map, mx, my);
            if (!is_fire && !is_medkit) {
                continue;
            }
            float fx = (float)mx + 0.5f;
            float fy = (float)my + 0.5f;
            float dx = fx - vx;
            float dy = fy - vy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < 0.18f || dist >= MAX_DEPTH) {
                continue;
            }
            float angle = wrap_angle_signed(atan2f(dy, dx) - vangle);
            if (fabsf(angle) > player->fov * 0.65f) {
                continue;
            }

            int center_x = (int)((0.5f + angle / player->fov) * (float)r_width);
            int bot_y = (int)floorf(horizon_y + (EYE_FLOOR_H * (float)r_height) / dist + 0.5f);
            if (is_medkit) {
                int box_h = (int)floorf((0.28f * (float)r_height) / dist + 0.5f);
                if (box_h < 4) box_h = 4;
                if (box_h > r_height) box_h = r_height;
                int box_w = (int)floorf((float)box_h * 1.35f + 0.5f);
                if (box_w < 5) box_w = 5;
                if (box_w > r_width) box_w = r_width;
                int top_y = bot_y - box_h + 1;
                int left_x = center_x - box_w / 2;
                int right_x = center_x + box_w / 2;
                float mfog = fmaxf(0.32f, 1.0f - (dist / 14.0f));
                AsciiColor c_box = scale_rgb(240.0f, 246.0f, 252.0f, mfog);
                AsciiColor c_cross = scale_rgb(235.0f, 52.0f, 52.0f, mfog);
                AsciiColor c_edge = scale_rgb(130.0f, 150.0f, 170.0f, mfog);
                for (int sx = left_x; sx <= right_x; sx++) {
                    if (sx < 0 || sx >= r_width || dist >= z_buffer[sx]) continue;
                    float nx = (float)(sx - left_x) / (float)(box_w > 1 ? box_w : 1) * 2.0f - 1.0f;
                    for (int sy = top_y; sy <= bot_y; sy++) {
                        if (sy < 0 || sy >= r_height) continue;
                        float ny = (float)(sy - top_y) / (float)(box_h > 1 ? box_h : 1) * 2.0f - 1.0f;
                        int is_border = (fabsf(nx) > 0.80f || fabsf(ny) > 0.78f);
                        int is_cross = (fabsf(nx) < 0.20f && fabsf(ny) < 0.58f) ||
                                       (fabsf(ny) < 0.22f && fabsf(nx) < 0.52f);
                        char mch = is_cross ? '+' : (is_border ? '#' : 'M');
                        AsciiColor mcol = is_cross ? c_cross : (is_border ? c_edge : c_box);
                        set_cell(sx, sy, mch, mcol, bg_black);
                    }
                }
                continue;
            }
            /* Jato de labareda bem alto e ameacador (ate proximo ao teto). */
            int flame_h = (int)floorf((1.20f * (float)r_height) / dist + 0.5f);
            if (flame_h < 7) flame_h = 7;
            if (flame_h > r_height) flame_h = r_height;
            int flame_w = (int)floorf((float)flame_h * 0.58f + 0.5f);
            if (flame_w < 5) flame_w = 5;
            if (flame_w > r_width) flame_w = r_width;

            int top_y = bot_y - flame_h + 1;
            int left_x = center_x - flame_w / 2;
            int right_x = center_x + flame_w / 2;
            float seed = (float)(mx * 17 + my * 31);

            for (int sx = left_x; sx <= right_x; sx++) {
                if (sx < 0 || sx >= r_width || dist >= z_buffer[sx]) {
                    continue;
                }
                float u = (float)(sx - left_x) / (float)(flame_w > 1 ? flame_w : 1);
                float nx = u * 2.0f - 1.0f;
                float tongue = 0.64f + 0.24f * sinf(u * 13.0f + time_now * 11.5f + seed)
                                     + 0.12f * cosf(u * 23.0f - time_now * 16.0f + seed * 0.5f);
                float max_h = (1.0f - powf(fabsf(nx), 1.45f) * 0.85f) * tongue;

                for (int sy = top_y; sy <= bot_y; sy++) {
                    if (sy < 0 || sy >= r_height) {
                        continue;
                    }
                    float h_norm = (float)(bot_y - sy) / (float)(flame_h > 1 ? flame_h : 1);
                    if (h_norm > max_h) {
                        continue;
                    }
                    float flicker = 0.5f + 0.5f * sinf(time_now * 17.0f + nx * 7.5f - h_norm * 10.0f + seed);
                    AsciiColor fcol;
                    char fch;
                    if (h_norm < 0.26f && fabsf(nx) < 0.42f) {
                        fcol = make_color(255, 250, 185);
                        fch = '@';
                    } else if (h_norm < 0.62f) {
                        fcol = make_color(255, (unsigned char)(95 + (int)(flicker * 85.0f)), 15);
                        fch = flicker > 0.48f ? '#' : '*';
                    } else {
                        fcol = make_color((unsigned char)(200 + (int)(flicker * 45.0f)), 32, 10);
                        fch = flicker > 0.55f ? '^' : '!';
                    }
                    set_cell(sx, sy, fch, fcol, bg_black);
                }
            }
        }
    }
}

void renderer_draw_sprites(const Player *player, const SpriteEntity *sprites, int count) {
    if (!player) {
        return;
    }
    if (ENEMY_SPRITE_PIPELINE_ENABLED && !enemy_sprite_pipeline_ready) {
        enemy_sprite_pipeline_init();
    }

    float vx = cam_view_ready ? cam_view_x : player->x;
    float vy = cam_view_ready ? cam_view_y : player->y;
    float vangle = cam_view_ready ? cam_view_angle : player->angle;
    float scale_y = (float)r_height / 180.0f;
    float pitch_cells = cam_pitch * scale_y;
    float view_bob = cam_third_person ? 0.0f : (sinf(player->bob_phase * 2.0f) * 1.6f * walk_amp * scale_y);
    float horizon_y = (float)r_height * 0.5f + pitch_cells + view_bob;

    SpriteEntity draw_list[MAX_SPRITES + 1];
    float dists[MAX_SPRITES + 1];
    int total_count = 0;

    if (sprites && count > 0) {
        int limit = (count > MAX_SPRITES) ? MAX_SPRITES : count;
        for (int i = 0; i < limit; i++) {
            if (!sprites[i].active) {
                continue;
            }
            float dx = sprites[i].x - vx;
            float dy = sprites[i].y - vy;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 0.25f || d >= MAX_DEPTH) {
                continue;
            }
            draw_list[total_count] = sprites[i];
            dists[total_count] = d;
            total_count++;
        }
    }

    if (cam_third_person) {
        float dx = player->x - vx;
        float dy = player->y - vy;
        float d = sqrtf(dx * dx + dy * dy);
        if (d >= 0.34f && d < MAX_DEPTH) {
            SpriteEntity self_sp;
            self_sp.x = player->x;
            self_sp.y = player->y;
            self_sp.active = 1;
            self_sp.shooting = (player->shoot_timer > 0.12f) ? 1 : 0;
            self_sp.label = 'P';
            draw_list[total_count] = self_sp;
            dists[total_count] = d;
            total_count++;
        }
    }

    if (total_count <= 0) {
        renderer_draw_hazards(cam_view_map, player);
        renderer_draw_enemy_projectiles(player);
        return;
    }

    /* Ordena do mais distante para o mais proximo (painter's algorithm) */
    for (int i = 0; i < total_count - 1; i++) {
        for (int j = i + 1; j < total_count; j++) {
            if (dists[i] < dists[j]) {
                SpriteEntity tmp_s = draw_list[i];
                float tmp_d = dists[i];
                draw_list[i] = draw_list[j];
                dists[i] = dists[j];
                draw_list[j] = tmp_s;
                dists[j] = tmp_d;
            }
        }
    }

    AsciiColor bg_black = make_color(0, 0, 0);
    float time_now = IsWindowReady() ? (float)GetTime() : 0.0f;

    for (int i = 0; i < total_count; i++) {
        float dx = draw_list[i].x - vx;
        float dy = draw_list[i].y - vy;
        float dist = dists[i];
        int is_self = (draw_list[i].label == 'P');
        const EnemyVisualDesign *visual_design = (!is_self && draw_list[i].label != 'O')
            ? enemy_visual_design_for_label(draw_list[i].label) : NULL;
        const EnemyDesign *enemy_design = (!is_self && draw_list[i].label != 'O')
            ? enemy_design_for_label(draw_list[i].label) : NULL;
        const EnemySpriteCache *sprite_cache =
            (ENEMY_SPRITE_PIPELINE_ENABLED && !is_self && draw_list[i].label != 'O')
                ? enemy_sprite_cache_for_label(draw_list[i].label) : NULL;

        float sprite_angle = wrap_angle_signed(atan2f(dy, dx) - vangle);
        if (fabsf(sprite_angle) > player->fov * 0.65f) {
            continue;
        }

        /* Passada ativa so para jogador local andando em 3aP; parados/inimigos apenas respiram */
        int is_walking = (is_self && walk_amp > 0.02f);
        float amp = is_walking ? walk_amp : 0.0f;
        float phase = is_walking
            ? player->bob_phase
            : (time_now * 2.4f + draw_list[i].x * 1.9f + draw_list[i].y * 2.3f);
        float breath = is_walking ? 0.0f : (0.5f + 0.5f * sinf(phase));
        float stride = is_walking ? (sinf(phase) * amp) : 0.0f;
        float bounce = is_walking ? (fabsf(cosf(phase)) * amp) : (breath * 0.55f);
        float sway = is_walking ? (cosf(phase) * 0.032f * amp) : 0.0f;

        int center_x = (int)((0.5f + sprite_angle / player->fov) * (float)r_width);
        int bot_y = (int)floorf(horizon_y + (EYE_FLOOR_H * (float)r_height) / dist + 0.5f);
        float design_height_scale = visual_design
            ? (REDESIGN_IMP_HEIGHT_SCALE * visual_design->world_height)
            : (enemy_design ? enemy_design->height_scale : 1.0f);
        int sprite_h = (int)floorf((0.78f * (float)r_height) / dist *
                                   design_height_scale + 0.5f);
        int min_sprite_h = (visual_design || enemy_design) ? 6 : 8;
        if (sprite_h < min_sprite_h) sprite_h = min_sprite_h;
        if (sprite_h > r_height) sprite_h = r_height;

        int sprite_w = visual_design
            ? (int)floorf((float)sprite_h * visual_design->width_ratio + 0.5f)
            : (enemy_design
            ? (int)floorf((float)sprite_h * enemy_design->width_ratio + 0.5f)
            : (sprite_h * 5) / 8);
        int min_sprite_w = (visual_design || enemy_design) ? 4 : 6;
        if (sprite_w < min_sprite_w) sprite_w = min_sprite_w;
        if (sprite_w > r_width) sprite_w = r_width;

        int top_y = bot_y - sprite_h + 1;
        int left_x = center_x - sprite_w / 2;
        int right_x = center_x + sprite_w / 2;
        const EnemyRenderCell *sprite_pixels = NULL;
        int texture_width = 0, texture_height = 0, texture_stride = 0;
        if (sprite_cache) {
            sprite_pixels = enemy_select_sprite_mip(sprite_cache, sprite_w, sprite_h,
                &texture_width, &texture_height, &texture_stride);
        }

        float fog = 1.0f - (dist / 14.5f);
        if (fog < 0.24f) fog = 0.24f;

        for (int sx = left_x; sx <= right_x; sx++) {
            if (sx < 0 || sx >= r_width || dist >= z_buffer[sx]) {
                continue;
            }

            float u = ((float)(sx - left_x) + 0.5f) / (float)(sprite_w > 0 ? sprite_w : 1);
            float sx_s = (u - 0.5f) * 2.0f; /* -1 .. +1 */
            float sx_u = sx_s - sway;
            float ax_u = fabsf(sx_u);

            for (int sy = top_y; sy <= bot_y; sy++) {
                if (sy < 0 || sy >= r_height) {
                    continue;
                }

                float v = ((float)(sy - top_y) + 0.5f) / (float)sprite_h;
                float vu = v + bounce * 0.026f;
                if (is_self && player_appearance_visual(player->visual_id, &player->appearance).custom_sprite) {
                    int mat = player_render_material(player, sx_u, vu, 1);
                    if (mat) {
                        Color color = character_color(mat);
                        set_cell(sx, sy, mat == 3 ? '@' : '#',
                                 scale_rgb(color.r, color.g, color.b, fog), bg_black);
                    }
                    continue;
                }
                if (sprite_pixels) {
                    EnemyRenderCell pixel = enemy_sample_sprite_mip(sprite_pixels,
                        texture_width, texture_height, texture_stride, u, v,
                        sprite_w, sprite_h);
                    if (!pixel.glyph) continue;

                    int source_x = (int)(u * (float)sprite_cache->source_width);
                    int source_y = (int)(v * (float)sprite_cache->source_height);
                    if (source_x >= sprite_cache->source_width) {
                        source_x = sprite_cache->source_width - 1;
                    }
                    if (source_y >= sprite_cache->source_height) {
                        source_y = sprite_cache->source_height - 1;
                    }

                    int shooting = draw_list[i].shooting;
                    int is_imp = draw_list[i].label == 'M';
                    if (is_imp && shooting && source_y == 7 && source_x == 8) {
                        AsciiColor fireball = make_color(255, 160, 40);
                        float sprite_fog = fog < 0.65f ? 0.65f : fog;
                        set_cell(sx, sy, '@',
                                 scale_rgb(fireball.r, fireball.g, fireball.b, sprite_fog), bg_black);
                        continue;
                    }

                    char glyph = pixel.glyph;
                    AsciiColor color = pixel.color;
                    int effect = shooting && visual_design &&
                                 pixel.token == visual_design->effect_token;
                    int muzzle_flash = shooting && !is_imp &&
                        source_y == sprite_cache->source_height / 2 &&
                        source_x >= sprite_cache->source_width - 2;
                    if (effect) {
                        color = visual_design->effect_color;
                    } else if (muzzle_flash) {
                        glyph = '*';
                        color = make_color(255, 245, 80);
                    } else if (shooting && is_imp &&
                               (pixel.token == 'B' || pixel.token == 'D')) {
                        color = scale_rgb(color.r, color.g, color.b, 1.25f);
                    } else if (shooting && visual_design &&
                               visual_design->styles[(unsigned char)pixel.token].brighten) {
                        color = scale_rgb(color.r, color.g, color.b, 1.25f);
                    }

                    float sprite_fog = fog < 0.65f ? 0.65f : fog;
                    set_cell(sx, sy, glyph,
                             scale_rgb(color.r, color.g, color.b, sprite_fog), bg_black);
                    continue;
                }
                if (visual_design) {
                    int art_x = (int)(u * (float)visual_design->width);
                    int art_y = (int)(v * (float)visual_design->height);
                    float enemy_fog = fog < 0.65f ? 0.65f : fog;
                    if (art_x >= visual_design->width) art_x = visual_design->width - 1;
                    if (art_y >= visual_design->height) art_y = visual_design->height - 1;
                    char token = visual_design->rows[art_y][art_x];
                    if (token == '.') continue;

                    EnemyArtStyle style = visual_design->styles[(unsigned char)token];
                    if (!style.glyph) continue;

                    int shooting = draw_list[i].shooting;
                    int effect = shooting && token == visual_design->effect_token;
                    int muzzle_flash = shooting &&
                        art_y == visual_design->height / 2 &&
                        art_x >= visual_design->width - 2;
                    if (effect) {
                        style.color = visual_design->effect_color;
                    } else if (muzzle_flash) {
                        style.glyph = '*';
                        style.color = make_color(255, 245, 80);
                    } else if (shooting && style.brighten) {
                        style.color = scale_rgb(style.color.r, style.color.g,
                                                style.color.b, 1.25f);
                    }

                    set_cell(sx, sy, style.glyph,
                             scale_rgb(style.color.r, style.color.g, style.color.b,
                                       enemy_fog), bg_black);
                    continue;
                }
                if (enemy_design) {
                    int art_x = (int)(u * ENEMY_ART_WIDTH);
                    int is_imp = draw_list[i].label == 'M';
                    int art_height = is_imp ? ENEMY_ART_MAX_HEIGHT : ENEMY_ART_HEIGHT;
                    int art_y = (int)(v * (float)art_height);
                    float enemy_fog = fog < 0.65f ? 0.65f : fog;
                    if (art_x >= ENEMY_ART_WIDTH) art_x = ENEMY_ART_WIDTH - 1;
                    if (art_y >= art_height) art_y = art_height - 1;
                    char token = enemy_design->rows[art_y][art_x];
                    char glyph = is_imp ? imp_art_glyph(token) : token;
                    int color_index = is_imp
                        ? imp_art_color_index(token) : enemy_art_color_index(token);
                    int imp_fireball = is_imp && draw_list[i].shooting &&
                        art_y == 7 && art_x == 8;
                    if (imp_fireball) {
                        AsciiColor fireball = make_color(255, 160, 40);
                        set_cell(sx, sy, '@',
                                 scale_rgb(fireball.r, fireball.g, fireball.b, enemy_fog), bg_black);
                        continue;
                    }
                    int muzzle_flash = !is_imp && draw_list[i].shooting &&
                        art_y == ENEMY_ART_HEIGHT / 2 &&
                        art_x >= ENEMY_ART_WIDTH - 2;
                    if (muzzle_flash) {
                        AsciiColor flash = make_color(255, 245, 80);
                        set_cell(sx, sy, '*',
                                 scale_rgb(flash.r, flash.g, flash.b, enemy_fog), bg_black);
                    } else if (color_index >= 0) {
                        AsciiColor color = enemy_design->palette[color_index];
                        if (is_imp && draw_list[i].shooting &&
                            (token == 'B' || token == 'D')) {
                            color = scale_rgb(color.r, color.g, color.b, 1.25f);
                        }
                        set_cell(sx, sy, glyph,
                                 scale_rgb(color.r, color.g, color.b, enemy_fog), bg_black);
                    }
                    continue;
                }
                int hit_part = 0; /* 1=armadura, 2=visor/reator/tiro, 3=escuro/arma */
                float nx = 0.0f, ny = 0.0f;
                float depth_shade = 1.0f;

                /* Silhueta 3D animada de soldado/inimigo blindado */
                if (vu < 0.21f) {
                    float hx = ax_u / 0.34f;
                    float hy = (vu - 0.105f) / 0.105f;
                    if (hx * hx + hy * hy <= 1.0f) {
                        if (is_self) {
                            /* Nuca/crista do capacete na visao de costas */
                            hit_part = (ax_u < 0.10f && vu > 0.04f && vu < 0.16f) ? 2 : 1;
                        } else {
                            hit_part = (vu >= 0.075f && vu <= 0.135f && ax_u < 0.24f) ? 2 : 1;
                        }
                        nx = (sx_u >= 0.0f ? hx : -hx) * 0.85f;
                        ny = hy * 0.65f;
                    }
                } else if (vu < 0.25f && ax_u < 0.18f) {
                    hit_part = 3;
                    nx = sx_u / 0.18f * 0.7f;
                } else if (vu >= 0.23f && vu < 0.58f) {
                    /* Torso + ombreiras + bracos (passada ao andar ou expansao suave na respiracao) */
                    float chest_expand = (!is_walking && vu < 0.46f) ? (breath * 0.025f) : 0.0f;
                    float torso_hw = ((vu < 0.42f) ? 0.56f : 0.44f) + chest_expand;
                    float arm_bob = is_walking
                        ? ((sx_u < 0.0f) ? (-stride * 0.045f) : (stride * 0.035f))
                        : (breath * 0.012f);
                    float v_arm = vu + arm_bob;

                    if (ax_u <= torso_hw) {
                        hit_part = 1;
                        nx = (sx_u / torso_hw) * 0.78f;
                        ny = (vu - 0.38f) * 1.2f;
                        if (is_self && vu >= 0.27f && vu <= 0.45f && ax_u < 0.32f) {
                            /* Mochila tatica/thruster dorsal mais brilhante ao andar */
                            if (ax_u >= 0.10f && ax_u <= 0.22f && vu >= 0.31f && vu <= 0.41f) {
                                hit_part = 2;
                                depth_shade = 0.88f + 0.24f * amp;
                            } else {
                                hit_part = 3;
                            }
                        } else if (!is_self && fabsf(vu - 0.35f) < 0.055f && ax_u < 0.14f) {
                            hit_part = 2; /* Reator no peito */
                        } else if (vu > 0.50f && vu < 0.54f) {
                            hit_part = 3; /* Cinto */
                        }
                    } else if (v_arm < 0.40f && ax_u <= 0.86f) {
                        /* Ombreiras */
                        float ox = (ax_u - 0.68f) / 0.19f;
                        float oy = (v_arm - 0.31f) / 0.09f;
                        if (ox * ox + oy * oy <= 1.0f) {
                            hit_part = 1;
                            nx = (sx_u >= 0.0f ? 1.0f : -1.0f) * ox * 0.8f;
                            ny = oy * 0.6f;
                        }
                    } else if (v_arm >= 0.36f && ax_u >= 0.58f && ax_u <= 0.82f) {
                        hit_part = (v_arm > 0.48f) ? 3 : 1;
                        nx = (sx_u >= 0.0f ? 1.0f : -1.0f) * 0.65f;
                        depth_shade = (sx_u < 0.0f) ? (1.0f + stride * 0.14f) : (1.0f - stride * 0.14f);
                    }

                    /* Arma na mao direita (3a pessoa) + clarao ao disparar */
                    if (is_self && sx_u > 0.46f && sx_u < 0.88f && v_arm > 0.28f && v_arm < 0.54f) {
                        hit_part = draw_list[i].shooting && v_arm < 0.35f ? 2 : 3;
                    } else if (draw_list[i].shooting && sx_u > 0.30f && sx_u < 0.95f &&
                               v_arm > 0.34f && v_arm < 0.52f) {
                        hit_part = 2;
                    }
                } else if (vu >= 0.56f) {
                    /* Pernas alternadas ao caminhar; parados e inimigos ficam firmes no chao */
                    float leg_s = is_walking ? ((sx_u < 0.0f) ? stride : -stride) : 0.0f;
                    float leg_max_v = is_walking ? (0.99f - fmaxf(0.0f, leg_s) * 0.11f) : 0.995f;
                    if (v <= leg_max_v) {
                        float leg_cx = 0.25f + leg_s * 0.025f;
                        float v_leg = is_walking ? (vu + leg_s * 0.055f) : v;
                        float l_dist = fabsf(ax_u - leg_cx);
                        float l_hw = (v_leg > 0.90f) ? 0.21f : 0.18f;
                        if (l_dist <= l_hw) {
                            hit_part = (v_leg > 0.92f) ? 3 : 1;
                            nx = ((ax_u - leg_cx) / l_hw) * 0.75f * (sx_u >= 0.0f ? 1.0f : -1.0f);
                            depth_shade = clamp_f01(1.0f - leg_s * 0.22f);
                            if (depth_shade < 0.68f) depth_shade = 0.68f;
                        }
                    }
                }

                if (hit_part == 0) continue;

                float nz = sqrtf(fmaxf(0.10f, 1.0f - nx * nx * 0.6f - ny * ny * 0.4f));
                float ndotl = fmaxf(0.24f, (-0.42f * nx - 0.48f * ny + 0.76f * nz)) * depth_shade;
                float lum = ndotl * fog;
                if (hit_part == 2) lum = fmaxf(0.92f * fog, 0.55f);

                float cr, cg, cb;
                if (hit_part == 2) {
                    if (draw_list[i].shooting) {
                        cr = 255.0f; cg = 230.0f; cb = 85.0f;
                    } else if (is_self) {
                        cr = 95.0f; cg = 225.0f; cb = 255.0f;
                    } else if (draw_list[i].label == 'O') {
                        cr = 95.0f; cg = 195.0f; cb = 245.0f;
                    } else {
                        cr = 255.0f; cg = 145.0f; cb = 55.0f;
                    }
                } else if (hit_part == 3) {
                    cr = 105.0f; cg = 120.0f; cb = 145.0f;
                } else {
                    if (is_self) {
                        cr = 165.0f; cg = 185.0f; cb = 212.0f;
                    } else if (draw_list[i].label == 'O') {
                        cr = 95.0f; cg = 195.0f; cb = 245.0f;
                    } else {
                        cr = 225.0f; cg = 88.0f; cb = 195.0f;
                    }
                }

                char sch = (hit_part == 2) ? '@' : sample_dense_ramp(lum, sx, sy);
                set_cell(sx, sy, sch, scale_rgb(cr, cg, cb, lum + 0.12f), bg_black);
            }
        }
    }
    renderer_draw_hazards(cam_view_map, player);
    renderer_draw_enemy_projectiles(player);
}

/* Projecao de efeitos na mesma grade e camera do raycasting. */
static int project_effect(const Player *p, float x, float y, float height,
                          int *sx, int *sy, float *depth) {
    float vx = cam_view_ready ? cam_view_x : p->x;
    float vy = cam_view_ready ? cam_view_y : p->y;
    float angle = cam_view_ready ? cam_view_angle : p->angle;
    float dx=x-vx, dy=y-vy;
    float dist=sqrtf(dx*dx+dy*dy);
    float delta=wrap_angle_signed(atan2f(dy,dx)-angle);
    if (dist < 0.15f || dist > MAX_DEPTH || fabsf(delta)>p->fov*0.6f) return 0;
    *sx=(int)((0.5f+delta/p->fov)*r_width);
    float bob=cam_third_person ? 0 : sinf(p->bob_phase*2)*1.6f*walk_amp*((float)r_height/180);
    *sy=(int)(r_height*0.5f + cam_pitch*((float)r_height/180) + bob + (EYE_FLOOR_H-height)*r_height/dist);
    *depth=dist;
    return *sx>=0 && *sx<r_width && dist<z_buffer[*sx];
}

static void renderer_draw_enemy_projectiles(const Player *player) {
    const EnemyProjectile *projectiles = enemies_projectile_pool();
    AsciiColor black = make_color(0, 0, 0);

    for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
        const EnemyProjectile *projectile = &projectiles[i];
        if (!projectile->active) continue;

        float height = projectile->kind == ENEMY_PROJECTILE_ARROW ? 0.32f : 0.46f;
        int sx, sy;
        float depth;
        if (!project_effect(player, projectile->x, projectile->y, height,
                            &sx, &sy, &depth)) {
            continue;
        }

        if (projectile->kind == ENEMY_PROJECTILE_ARROW) {
            float angle = atan2f(projectile->dy, projectile->dx);
            float view_angle = cam_view_ready ? cam_view_angle : player->angle;
            float horizontal = sinf(wrap_angle_signed(angle - view_angle));
            char arrow = horizontal > 0.18f ? '>' : (horizontal < -0.18f ? '<' : '^');
            int tail_x = arrow == '>' ? -1 : (arrow == '<' ? 1 : 0);
            int tail_y = arrow == '^' ? 1 : 0;
            set_cell(sx, sy, arrow, make_color(255, 245, 115), black);
            for (int trail = 1; trail <= 2; trail++) {
                int px = sx + tail_x * trail;
                int py = sy + tail_y * trail;
                if (px >= 0 && px < r_width && depth < z_buffer[px]) {
                    set_cell(px, py, trail == 1 ? '-' : '.',
                             trail == 1 ? make_color(255, 185, 55) : make_color(220, 115, 30),
                             black);
                }
            }
        } else {
            /* Keep energy and fire orbs readable at medium range. */
            int radius = (int)(r_height * (projectile->kind == ENEMY_PROJECTILE_FIREBALL
                ? 0.10f : 0.09f) / depth);
            if (radius < 3) radius = 3;
            if (radius > 16) radius = 16;

            AsciiColor core = projectile->kind == ENEMY_PROJECTILE_FIREBALL
                ? make_color(255, 180, 45) : make_color(155, 255, 255);
            AsciiColor edge = projectile->kind == ENEMY_PROJECTILE_FIREBALL
                ? make_color(255, 55, 10) : make_color(20, 150, 255);
            for (int y = -radius; y <= radius; y++) {
                for (int x = -radius; x <= radius; x++) {
                    float d = (float)(x * x + y * y) / (float)(radius * radius);
                    int px = sx + x;
                    if (d > 1.0f || px < 0 || px >= r_width || depth >= z_buffer[px]) continue;
                    char glyph = d > 0.55f ? '*' :
                        (projectile->kind == ENEMY_PROJECTILE_FIREBALL ? '@' : 'O');
                    set_cell(px, sy + y, glyph, d > 0.55f ? edge : core, black);
                }
            }
        }
    }
}

void renderer_draw_ability_effects(const Player *p) {
    const AbilityState *s=&p->ability_state;
    AsciiColor black=make_color(0,0,0);
    float time=IsWindowReady() ? (float)GetTime() : 0;
    if (s->zone_time>0) {
        for (int i=0;i<16;i++) {
            float a=i*2.4f;
            float radius=0.5f+(i%4)*0.42f;
            float height=0.15f+fmodf(time*0.45f+i*0.17f,0.8f);
            int sx,sy; float depth;
            if (!project_effect(p,s->zone_x+cosf(a)*radius,s->zone_y+sinf(a)*radius,height,&sx,&sy,&depth)) continue;
            static const char *heart[]={"@ @","@@@"," @ "};
            for (int r=0;r<3;r++) for (int c=0;c<3;c++) {
                int px=sx+c-1;
                if (heart[r][c]!=' ' && px>=0 && px<r_width && depth<z_buffer[px])
                    set_cell(px,sy+r,heart[r][c],make_color(255,115,180),black);
            }
        }
    }
    if (s->fireball_active) {
        int sx,sy; float depth;
        if (project_effect(p,s->fireball_x,s->fireball_y,0.48f,&sx,&sy,&depth)) {
            int radius=(int)(r_height*0.11f/depth);
            if (radius<2) radius=2;
            if (radius>30) radius=30;
            for (int y=-radius;y<=radius;y++) for (int x=-radius;x<=radius;x++) {
                float d=(float)(x*x+y*y)/(radius*radius);
                int px=sx+x;
                if (d>1 || px<0 || px>=r_width || depth>=z_buffer[px]) continue;
                set_cell(px,sy+y,d>0.65f?'*':'@',d>0.65f ? make_color(155,65,215) : make_color(30,15,44),black);
            }
        }
    }
}
void renderer_draw_weapon(const Player *player) {
    if (!player || cam_third_person) {
        return;
    }

    if (player->ability_state.berserk_time > 0) {
        float swing = player->shoot_timer > 0 ? sinf(player->shoot_timer / 0.60f * PI_F) : 0;
        for (int y=r_height/3;y<r_height;y++) {
            float t=(float)(y-r_height/3)/(r_height*2/3);
            int center=(int)(r_width*(0.68f-t*0.18f-swing*0.28f));
            int width=(int)(r_width*(0.016f+t*0.045f));
            if (t>0.80f) width=(int)(r_width*0.09f);
            if (t>0.85f) width=(int)(r_width*0.015f);
            for (int x=-width;x<=width;x++) {
                AsciiColor color=t>0.85f?make_color(65,45,42):abs(x)>width-2?make_color(220,225,235):make_color(105,115,132);
                set_cell(center+x,y,t>0.85f?'#':'=',color,make_color(0,0,0));
            }
        }
        return;
    }
    int w_cols = (r_width * 23) / 100;
    int w_rows = (r_height * 27) / 100;
    if (w_cols < 36) w_cols = 36;
    if (w_cols & 1) w_cols++;
    if (w_rows < 22) w_rows = 22;

    int firing = (player->shoot_timer > 0.12f);
    float bob_sx = (float)r_width / 360.0f;
    float bob_sy = (float)r_height / 180.0f;
    int bob_x = (int)roundf(sinf(player->bob_phase) * 7.5f * walk_amp * bob_sx);
    int bob_y = (int)roundf((1.0f - cosf(player->bob_phase * 2.0f)) * 3.8f * walk_amp * bob_sy) -
                (firing ? (int)fmaxf(3.0f, 6.0f * bob_sy) : 0);

    int base_x = (r_width - w_cols) / 2 + bob_x;
    int base_y = r_height - w_rows + bob_y;

    AsciiColor bg_black = make_color(0, 0, 0);

    /* Claras de disparo volumetrico duplo nos dois canos (simetrico em torno do centro) */
    if (firing) {
        int flash_off = (int)roundf(0.085f * (float)w_cols);
        int mcx_l = base_x + (w_cols / 2) - 1 - flash_off;
        int mcx_r = base_x + (w_cols / 2) + flash_off;
        int mcy = base_y + 2;
        int rad_x = (w_cols * 18) / 100;
        int rad_y = (w_rows * 21) / 100;
        if (rad_x < 6) rad_x = 6;
        if (rad_y < 4) rad_y = 4;
        for (int b = 0; b < 2; b++) {
            int mcx = (b == 0) ? mcx_l : mcx_r;
            for (int dy = -rad_y; dy <= rad_y; dy++) {
                for (int dx = -rad_x; dx <= rad_x; dx++) {
                    float ex = (float)dx / (float)rad_x;
                    float ey = (float)dy / (float)rad_y;
                    float d = sqrtf(ex * ex + ey * ey);
                    float rays = 0.78f + 0.25f * sinf(atan2f(ey, ex) * 7.0f);
                    if (d < rays * 0.92f) {
                        float f_lum = clamp_f01(1.15f - d);
                        char fch = sample_dense_ramp(f_lum, mcx + dx, mcy + dy);
                        AsciiColor fcol = (d < 0.42f)
                            ? scale_rgb(255.0f, 245.0f, 150.0f, 1.0f)
                            : scale_rgb(255.0f, 130.0f, 38.0f, f_lum + 0.2f);
                        set_cell(mcx + dx, mcy + dy, fch, fcol, bg_black);
                    }
                }
            }
        }
    }

    /* Desenho 3D do fuzil duplo de plasma em primeira pessoa na base da tela */
    for (int r = 0; r < w_rows; r++) {
        float v = ((float)r + 0.5f) / (float)w_rows;
        for (int c = 0; c < w_cols; c++) {
            float u = ((float)c + 0.5f) / (float)w_cols;
            float ax = fabsf(u - 0.5f) * 2.0f;

            /* 1=metal/cano, 2=chassi escuro/rebaixo, 3=trilho claro, 4=nucleo/mira */
            int part = 0;
            float nx = 0.0f;

            if (v >= 0.02f && v < 0.36f) {
                /* Dois canos cilindricos paralelos + poste de mira central */
                float d_left = fabsf(u - 0.415f);
                float d_right = fabsf(u - 0.585f);
                float b_rad = 0.064f + v * 0.03f;
                if (d_left <= b_rad || d_right <= b_rad) {
                    float d_min = (d_left < d_right) ? (u - 0.415f) : (u - 0.585f);
                    nx = clamp_f01(fabsf(d_min) / b_rad) * (d_min >= 0.0f ? 1.0f : -1.0f);
                    part = (v < 0.07f || (v > 0.14f && v < 0.18f)) ? 3 : 1;
                } else if (fabsf(u - 0.50f) < 0.026f && v >= 0.11f) {
                    part = (v < 0.16f) ? 4 : 2;
                    nx = (u - 0.50f) / 0.026f;
                }
            } else if (v >= 0.36f && v < 0.62f) {
                /* Guarda-mao blindado com dissipadores laterais e bobina central */
                float hw = 0.23f + (v - 0.36f) * 0.26f;
                if (ax <= hw) {
                    nx = (u >= 0.50f ? 1.0f : -1.0f) * (ax / hw);
                    if (ax < 0.16f && v >= 0.39f && v <= 0.59f &&
                        fmodf(v * 24.0f, 1.0f) < 0.55f) {
                        part = 4;
                    } else if (ax > hw * 0.55f && ((r % 3) == 0)) {
                        part = 2;
                    } else if (ax < 0.24f) {
                        part = 3;
                    } else {
                        part = 1;
                    }
                }
            } else if (v >= 0.62f) {
                /* Receptor pesado + miras traseiras + manopla e antebraco */
                float hw = 0.30f + (v - 0.62f) * 0.42f;
                if (hw > 0.58f) hw = 0.58f;
                if (ax <= hw) {
                    nx = (u >= 0.50f ? 1.0f : -1.0f) * (ax / hw);
                    if (v < 0.70f && ax > 0.18f && ax < 0.29f) {
                        part = 3; /* Abas de mira traseira */
                    } else if (v >= 0.68f && v <= 0.77f && ax < 0.20f) {
                        part = 4; /* Barra indicadora de carga */
                    } else if (ax > hw * 0.84f || v > 0.88f) {
                        part = 2; /* Chanfros e punho blindado simetricos */
                    } else {
                        part = 1;
                    }
                }
            }

            if (part == 0) continue;

            float cyl = sqrtf(fmaxf(0.14f, 1.0f - nx * nx * 0.85f));
            char wch;
            AsciiColor wcol;
            if (part == 4) {
                float glow = firing ? 1.25f : 0.96f;
                wch = '@';
                wcol = scale_rgb(95.0f, 225.0f, 255.0f, glow);
                if (firing) {
                    wcol = scale_rgb(255.0f, 195.0f, 65.0f, glow);
                }
            } else if (part == 2) {
                wch = (r % 2 == 0) ? '=' : '+';
                wcol = scale_rgb(76.0f, 88.0f, 108.0f, 0.82f);
            } else if (part == 3) {
                float lum = 0.74f + 0.26f * cyl;
                wch = sample_dense_ramp(lum, c, r);
                wcol = scale_rgb(225.0f, 232.0f, 245.0f, lum);
            } else {
                float lum = 0.36f + 0.56f * cyl;
                wch = sample_dense_ramp(lum, c, r);
                wcol = scale_rgb(142.0f, 156.0f, 180.0f, lum + 0.16f);
            }

            set_cell(base_x + c, base_y + r, wch, wcol, bg_black);
        }
    }
}

void renderer_draw_minimap(const GameMap *map, const Player *player,
                           const SpriteEntity *sprites, int count) {
    if (!map || !player) {
        return;
    }

    AsciiColor bg_black = make_color(0, 0, 0);
    AsciiColor mm_floor = make_color(38, 44, 58);
    AsciiColor border = make_color(120, 134, 162);

    int ox = 2;
    int oy = 2;
    int scale = (r_height < 130) ? 1 : 2;
    int mw = map->width * scale;
    int mh = map->height * scale;

    /* Moldura do minimapa em ASCII puro */
    for (int y = 0; y <= mh + 1; y++) {
        for (int x = 0; x <= mw + 1; x++) {
            int is_edge = (x == 0 || y == 0 || x == mw + 1 || y == mh + 1);
            if (is_edge) {
                char bch = ((x == 0 || x == mw + 1) && (y == 0 || y == mh + 1)) ? '+' :
                           ((y == 0 || y == mh + 1) ? '-' : '|');
                set_cell(ox + x, oy + y, bch, border, bg_black);
            }
        }
    }

    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            char tile = map_get_tile(map, x, y);
            int is_w = map_is_wall(map, x, y);
            char gch = '=';
            AsciiColor col = mm_floor;
            if (is_w) {
                gch = tile;
                col = get_tile_base_color(tile);
            } else if (map_is_fire_active_minimap(map, x, y)) {
                gch = '^';
                col = make_color(255, 160, 20);
            } else if (map_is_medkit_active_minimap(map, x, y)) {
                gch = '+';
                col = make_color(185, 255, 205);
            }

            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    set_cell(ox + 1 + x * scale + sx, oy + 1 + y * scale + sy, gch, col, bg_black);
                }
            }
        }
    }

    if (sprites && count > 0) {
        for (int i = 0; i < count; i++) {
            if (!sprites[i].active) continue;
            int sx = (int)sprites[i].x;
            int sy = (int)sprites[i].y;
            if (sx >= 0 && sx < map->width && sy >= 0 && sy < map->height) {
                for (int mmy = 0; mmy < scale; mmy++) {
                    for (int mmx = 0; mmx < scale; mmx++) {
                        set_cell(ox + 1 + sx * scale + mmx, oy + 1 + sy * scale + mmy, 'E',
                                 make_color(255, 95, 225), bg_black);
                    }
                }
            }
        }
    }

    /* Direcao da mira no minimapa */
    int look_x = (int)(player->x + cosf(player->angle) * 1.15f);
    int look_y = (int)(player->y + sinf(player->angle) * 1.15f);
    if (look_x >= 0 && look_x < map->width && look_y >= 0 && look_y < map->height &&
        !map_is_wall(map, look_x, look_y)) {
        set_cell(ox + 1 + look_x * scale, oy + 1 + look_y * scale, '*',
                 make_color(255, 195, 65), bg_black);
    }

    int px = (int)player->x;
    int py = (int)player->y;
    if (px >= 0 && px < map->width && py >= 0 && py < map->height) {
        AsciiColor pcol = make_color(255, 240, 75);
        for (int sy = 0; sy < scale; sy++) {
            for (int sx = 0; sx < scale; sx++) {
                set_cell(ox + 1 + px * scale + sx, oy + 1 + py * scale + sy, 'P', pcol, bg_black);
            }
        }
    }
}

void renderer_draw_hud(const Player *player) {
    if (!player || font_loaded) {
        return;
    }

    int cx = r_width / 2;
    int cy = r_height / 2;
    int firing = (player->shoot_timer > 0.12f);

    AsciiColor bg_black = make_color(0, 0, 0);
    AsciiColor cross_col = firing ? make_color(255, 95, 65) : make_color(115, 255, 138);

    set_cell(cx, cy, firing ? '@' : '+', cross_col, bg_black);
    set_cell(cx - 4, cy, '-', cross_col, bg_black);
    set_cell(cx - 3, cy, '-', cross_col, bg_black);
    set_cell(cx + 3, cy, '-', cross_col, bg_black);
    set_cell(cx + 4, cy, '-', cross_col, bg_black);
    set_cell(cx, cy - 3, '|', cross_col, bg_black);
    set_cell(cx, cy - 2, '|', cross_col, bg_black);
    set_cell(cx, cy + 2, '|', cross_col, bg_black);
    set_cell(cx, cy + 3, '|', cross_col, bg_black);
}

static int fit_hud_font(const char *txt, int want_sz, int max_w) {
    int sz = want_sz;
    while (sz > 10 && MeasureText(txt, sz) > max_w) {
        sz--;
    }
    return sz;
}

static void draw_hud_mugshot(int bx, int by, int bw, int bh, const Player *player) {
    if (player && player_appearance_visual(player->visual_id, &player->appearance).custom_sprite) {
        DrawRectangle(bx, by, bw, bh, (Color){10, 12, 17, 255});
        DrawRectangleLines(bx, by, bw, bh, (Color){235, 55, 100, 255});
        CharacterVisual visual = player_appearance_visual(player->visual_id, &player->appearance);
        int rows = visual.portrait_rows;
        int cols = visual.portrait_cols;
        float crop_x = visual.portrait_crop_x;
        float crop_y = visual.portrait_crop_y;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                int mat = player_render_material(player,
                    (((float)c + 0.5f) / cols * 2.0f - 1.0f) * crop_x,
                    ((float)r + 0.5f) / rows * crop_y, 0);
                if (mat) DrawTextCodepoint(GetFontDefault(), mat == 3 ? '@' : '#',
                    (Vector2){bx + 3 + c * (float)(bw - 6) / cols, by + 3 + r * (float)(bh - 6) / rows},
                    (float)(bh - 6) / rows * 1.5f, character_color(mat));
            }
        }
        return;
    }
    int hp = player ? player->health : 100;
    int firing = (player && player->shoot_timer > 0.12f);
    double t = GetTime();

    /* Giro sutil da cabeca/olhar a cada ~3s + piscada natural ocasional */
    float look_wave = sinf((float)t * 1.05f);
    float head_turn = 0.0f;
    if (!firing) {
        if (look_wave > 0.60f) head_turn = 0.065f;
        else if (look_wave < -0.60f) head_turn = -0.065f;
    }
    int is_blinking = (!firing && fmodf((float)t, 4.2f) > 4.06f);

    Color frame_col = (hp <= 30) ? (Color){225, 60, 60, 255}
                    : firing      ? (Color){255, 195, 70, 255}
                                  : (Color){95, 112, 138, 255};
    DrawRectangle(bx, by, bw, bh, (Color){10, 12, 17, 255});
    DrawRectangleLines(bx, by, bw, bh, frame_col);

    int mh = (bh - 6) / 4;
    if (mh < 12) mh = 12;
    if (mh > 20) mh = 20;
    int mw = (mh * 6) / 5;
    float cw = (float)(bw - 6) / (float)mw;
    float ch = (float)(bh - 6) / (float)mh;
    float gsize = ch * 1.55f;
    if (gsize < 5.0f) gsize = 5.0f;

    float muzzle_flash = firing ? 0.35f : 0.0f;

    for (int r = 0; r < mh; r++) {
        float fy = (((float)r + 0.5f) / (float)mh) * 2.0f - 1.0f;
        for (int c = 0; c < mw; c++) {
            float fx = (((float)c + 0.5f) / (float)mw) * 2.0f - 1.0f;
            float ax = fabsf(fx);
            float fx_t = fx - head_turn;
            float ax_t = fabsf(fx_t);

            /* Silhueta da cabeca/capacete + gola */
            float rad = sqrtf(fx * fx * 1.10f + fy * fy * 0.92f);
            int in_ear = (ax >= 0.72f && ax <= 0.96f && fy > -0.35f && fy < 0.38f);
            int in_neck = (ax < 0.42f && fy >= 0.66f);
            if (rad > 0.90f && !in_ear && !in_neck) {
                continue;
            }

            char glyph = '#';
            float cr = 120.0f, cg = 132.0f, cb = 148.0f;
            float nz = sqrtf(fmaxf(0.15f, 1.0f - fx * fx * 0.65f - fy * fy * 0.45f));
            float light = clamp_f01(0.45f + 0.52f * nz - fx * 0.14f - fy * 0.12f + muzzle_flash * 0.25f);

            int in_helmet = (fy < -0.36f || ax > 0.56f || in_ear || (fy > 0.68f && ax > 0.24f));
            int in_visor_rim = (ax <= 0.56f && fy >= -0.40f && fy <= -0.26f &&
                                fy > -0.38f + ax * 0.12f);

            if (in_helmet) {
                /* Capacete blindado metalico / oliva tatico */
                cr = 92.0f; cg = 114.0f; cb = 104.0f;
                if (ax < 0.12f && fy < -0.45f) {
                    cr = 155.0f; cg = 175.0f; cb = 165.0f;
                    light = fmaxf(light, 0.85f);
                } else if (in_ear) {
                    cr = 215.0f; cg = 138.0f; cb = 52.0f;
                    light = 0.78f;
                }
                glyph = sample_dense_ramp(light, c, r);
            } else if (in_visor_rim) {
                /* Aba discreta do capacete */
                cr = 58.0f; cg = 68.0f; cb = 78.0f;
                glyph = '-';
            } else if (fy >= 0.68f) {
                /* Peitoral/gola */
                cr = 85.0f; cg = 96.0f; cb = 112.0f;
                glyph = sample_dense_ramp(light, c, r);
            } else {
                /* Rosto do personagem com sombra natural sob a aba e olhos compactos */
                cr = 228.0f + muzzle_flash * 22.0f;
                cg = 174.0f + muzzle_flash * 18.0f;
                cb = 138.0f - (hp <= 50 ? 20.0f : 0.0f);

                int in_brow = (fy < -0.12f && ax_t < 0.48f);

                /* Olhos compactos (1 linha por orbita) + iris discreta sem brilho exagerado */
                float eye_y = -0.055f;
                float eye_row_tol = fmaxf(0.052f, 0.92f / (float)mh);
                float eye_w = 0.105f;
                float eye_l = -0.25f;
                float eye_r = 0.25f;
                int on_eye_row = (fabsf(fy - eye_y) < eye_row_tol);
                int in_eye = on_eye_row &&
                             (fabsf(fx_t - eye_l) < eye_w || fabsf(fx_t - eye_r) < eye_w);
                float look_shift = (head_turn > 0.02f) ? 0.042f : ((head_turn < -0.02f) ? -0.042f : 0.0f);
                float pup_l = (head_turn == 0.0f) ? -0.215f : (eye_l + look_shift);
                float pup_r = (head_turn == 0.0f) ?  0.215f : (eye_r + look_shift);
                float pup_col_tol = fmaxf(0.040f, 0.90f / (float)mw);
                int in_pupil = on_eye_row && !is_blinking &&
                               (fabsf(fx_t - pup_l) < pup_col_tol ||
                                fabsf(fx_t - pup_r) < pup_col_tol);

                /* Boca expressiva */
                float mouth_y = firing ? 0.39f : 0.41f;
                float mouth_h = firing ? 0.12f : (hp <= 35 ? 0.070f : 0.052f);
                int in_mouth = (ax_t < (firing ? 0.26f : 0.22f)) &&
                               (fabsf(fy - mouth_y) < mouth_h);

                /* Nariz */
                int in_nose = (ax_t < 0.075f && fy > 0.01f && fy < 0.24f);

                /* Machucado/cicatriz conforme vida cai */
                int has_bruise = (hp <= 65 && fx_t > 0.14f && fx_t < 0.44f &&
                                  fy > 0.06f && fy < 0.34f);
                int crit_blood = (hp <= 30 && ((fx_t < -0.18f && fy > -0.20f && fy < 0.22f) ||
                                               (ax_t < 0.16f && fy > 0.46f && fy < 0.64f)));

                if (in_pupil) {
                    if (firing) {
                        cr = 255.0f; cg = 205.0f; cb = 75.0f; glyph = '*'; light = 0.95f;
                    } else if (hp <= 35) {
                        cr = 185.0f; cg = 55.0f; cb = 55.0f; glyph = '*'; light = 0.85f;
                    } else {
                        cr = 42.0f; cg = 34.0f; cb = 30.0f; glyph = '*'; light = 0.90f;
                    }
                } else if (in_eye) {
                    if (is_blinking) {
                        cr = 168.0f; cg = 124.0f; cb = 96.0f; glyph = '-'; light = 0.72f;
                    } else {
                        cr = 165.0f; cg = 160.0f; cb = 155.0f; glyph = '-'; light = 0.85f;
                    }
                } else if (in_brow) {
                    cr = 158.0f; cg = 112.0f; cb = 86.0f;
                    light *= 0.68f;
                    glyph = sample_dense_ramp(light, c, r);
                } else if (in_mouth) {
                    if (firing) {
                        cr = 240.0f; cg = 236.0f; cb = 222.0f;
                        glyph = (fy < mouth_y) ? '#' : '=';
                        light = 0.92f;
                    } else {
                        cr = 128.0f; cg = 58.0f; cb = 52.0f;
                        glyph = '=';
                        light = 0.72f;
                    }
                } else if (crit_blood || has_bruise) {
                    cr = 205.0f; cg = 42.0f; cb = 48.0f;
                    glyph = sample_dense_ramp(0.65f, c, r);
                    light = 0.85f;
                } else if (in_nose) {
                    cr = 182.0f; cg = 128.0f; cb = 100.0f;
                    glyph = '+';
                } else {
                    glyph = sample_dense_ramp(light, c, r);
                }
            }

            AsciiColor fc = scale_rgb(cr, cg, cb, light + 0.14f);
            DrawTextCodepoint(
                ui_font,
                (int)glyph,
                (Vector2){(float)(bx + 3) + (float)c * cw, (float)(by + 3) + (float)r * ch},
                gsize,
                (Color){fc.r, fc.g, fc.b, 255}
            );
        }
    }
}

static void draw_hud_ascii_meter(int x, int y, int w, int h, float ratio, Color col) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    const int SEGS = 14;
    int filled = (int)roundf(ratio * (float)SEGS);
    float seg_w = (float)w / (float)SEGS;
    float gsize = (float)h * 1.15f;
    if (gsize < 10.0f) gsize = 10.0f;

    DrawRectangle(x - 2, y - 1, w + 4, h + 3, (Color){12, 15, 22, 255});
    for (int i = 0; i < SEGS; i++) {
        int ch = (i < filled) ? '#' : '-';
        Color c = (i < filled) ? col : (Color){52, 60, 76, 255};
        DrawTextCodepoint(ui_font, ch, (Vector2){(float)x + (float)i * seg_w, (float)y}, gsize, c);
    }
}

void renderer_draw_scene_and_hud(const Player *player, int is_multiplayer,
                                 int remote_connected, int mouse_locked, int fps_cap) {
    if (!font_loaded) {
        return;
    }

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int hud_h = (int)((float)sh * 0.145f);
    if (hud_h < 82) {
        hud_h = 82;
    }
    int view_h = sh - hud_h;
    if (view_h < r_height) {
        view_h = r_height;
    }

    float cell_w = (float)sw / (float)r_width;
    float cell_h = (float)view_h / (float)r_height;
    float glyph_size = cell_h * 1.85f;
    if (glyph_size < 4.0f) {
        glyph_size = 4.0f;
    }

    ClearBackground((Color){5, 7, 11, 255});

    /* Viewport 3D 100% em Micro-ASCII puro (ZERO DrawRectangle por celula) */
    for (int y = 0; y < r_height; y++) {
        float py = (float)y * cell_h;
        for (int x = 0; x < r_width; x++) {
            AsciiCell cell = screen_grid[y][x];
            if (cell.ch != ' ' && cell.ch != '\0') {
                float px = (float)x * cell_w;
                DrawTextCodepoint(
                    ui_font,
                    (int)cell.ch,
                    (Vector2){px, py},
                    glyph_size,
                    (Color){cell.fg.r, cell.fg.g, cell.fg.b, 255}
                );
            }
        }
    }

    /* Mira em ASCII puro centralizada ao pixel sobre o eixo do viewport */
    if (player) {
        int firing = (player->shoot_timer > 0.12f);
        Color cross_col = firing ? (Color){255, 95, 65, 255} : (Color){115, 255, 138, 255};
        int ch_sz = (int)(glyph_size * 1.15f);
        if (ch_sz < 12) ch_sz = 12;
        if (ch_sz > 28) ch_sz = 28;
        int cx_px = sw / 2;
        int cy_px = view_h / 2;

        const char *c_str = firing ? "@" : "+";
        int cw_c = MeasureText(c_str, ch_sz);
        DrawText(c_str, cx_px - cw_c / 2, cy_px - ch_sz / 2, ch_sz, cross_col);

        const char *h_str = "--";
        int hw_c = MeasureText(h_str, ch_sz);
        int h_gap = (int)((float)ch_sz * 0.72f);
        if (h_gap < 10) h_gap = 10;
        DrawText(h_str, cx_px - h_gap - hw_c, cy_px - ch_sz / 2, ch_sz, cross_col);
        DrawText(h_str, cx_px + h_gap, cy_px - ch_sz / 2, ch_sz, cross_col);

        const char *v_str = "|";
        int vw_c = MeasureText(v_str, ch_sz);
        int v_gap = (int)((float)ch_sz * 0.55f);
        if (v_gap < 8) v_gap = 8;
        int vx = cx_px - vw_c / 2;
        DrawText(v_str, vx, cy_px - v_gap - ch_sz, ch_sz, cross_col);
        DrawText(v_str, vx, cy_px + v_gap, ch_sz, cross_col);
    }

    /* Painel inferior de HUD tatico estilo DOOM (Vida | Pontos | Carinha | Municao | Status) */
    int hud_y = view_h;
    DrawRectangle(0, hud_y, sw, hud_h, (Color){16, 19, 27, 255});
    DrawRectangle(0, hud_y, sw, 3, (Color){196, 48, 42, 255});
    DrawRectangle(0, hud_y + hud_h - 3, sw, 3, (Color){32, 38, 52, 255});

    if (player) {
        int lbl_font = (int)((float)hud_h * 0.165f);
        if (lbl_font < 11) lbl_font = 11;
        int val_font = (int)((float)hud_h * 0.30f);
        if (val_font < 17) val_font = 17;
        int stat_font = (int)((float)hud_h * 0.165f);
        if (stat_font < 10) stat_font = 10;

        /* 5 compartimentos proporcionais com carinha centralizada */
        int face_h = hud_h - 12;
        int face_w = (int)((float)face_h * 1.08f);
        int face_x = (sw - face_w) / 2;
        int face_y = hud_y + 6;

        int left_end = face_x - 8;
        int right_start = face_x + face_w + 8;

        int s1_x = 10;
        int s1_w = (int)((float)(left_end - 18) * 0.54f);
        int s2_x = s1_x + s1_w + 8;
        int s2_w = left_end - s2_x;

        int right_total = sw - right_start - 10;
        int s4_x = right_start;
        int s4_w = (int)((float)right_total * 0.48f);
        int s5_x = s4_x + s4_w + 8;
        int s5_w = sw - s5_x - 10;

        int card_y = hud_y + 7;
        int card_h = hud_h - 14;
        Color card_bg = (Color){24, 28, 38, 255};
        Color card_bd = (Color){54, 64, 84, 255};
        Color lbl_col = (Color){145, 160, 185, 255};

        DrawRectangle(s1_x, card_y, s1_w, card_h, card_bg);
        DrawRectangleLines(s1_x, card_y, s1_w, card_h, card_bd);
        DrawRectangle(s2_x, card_y, s2_w, card_h, card_bg);
        DrawRectangleLines(s2_x, card_y, s2_w, card_h, card_bd);
        DrawRectangle(s4_x, card_y, s4_w, card_h, card_bg);
        DrawRectangleLines(s4_x, card_y, s4_w, card_h, card_bd);
        DrawRectangle(s5_x, card_y, s5_w, card_h, card_bg);
        DrawRectangleLines(s5_x, card_y, s5_w, card_h, card_bd);

        int bar_h = (int)((float)card_h * 0.24f);
        if (bar_h < 12) bar_h = 12;
        if (bar_h > 20) bar_h = 20;
        int bar_y = card_y + card_h - bar_h - 6;

        /* Bloco 1: VIDA (rotulo a esquerda, valor a direita, barra livre abaixo) */
        Color hp_col = (player->health > 60) ? (Color){75, 228, 120, 255}
                     : (player->health > 30) ? (Color){248, 200, 58, 255}
                                             : (Color){240, 68, 68, 255};
        const char *hp_val = TextFormat("%d", player->health);
        int hp_lf = fit_hud_font("VIDA", lbl_font, (s1_w - 20) / 2);
        int hp_vf = fit_hud_font(hp_val, val_font, s1_w - MeasureText("VIDA", hp_lf) - 24);
        DrawText("VIDA", s1_x + 10, card_y + 8, hp_lf, lbl_col);
        DrawText(hp_val, s1_x + s1_w - MeasureText(hp_val, hp_vf) - 10, card_y + 6, hp_vf, hp_col);
        draw_hud_ascii_meter(s1_x + 10, bar_y, s1_w - 20, bar_h,
                             (float)player->health / (float)(player->max_health > 0 ? player->max_health : 100), hp_col);

        /* Bloco 2: PONTOS + indicador da densidade ASCII */
        const char *sc_val = TextFormat("%04d", player->score);
        int sc_lf = fit_hud_font("PONTOS", lbl_font, (s2_w - 20) / 2);
        int sc_vf = fit_hud_font(sc_val, val_font, s2_w - MeasureText("PONTOS", sc_lf) - 24);
        DrawText("PONTOS", s2_x + 10, card_y + 8, sc_lf, lbl_col);
        DrawText(sc_val, s2_x + s2_w - MeasureText(sc_val, sc_vf) - 10, card_y + 6,
                 sc_vf, (Color){115, 220, 255, 255});
        const char *dens_txt = TextFormat("ASCII: %s [F2]", quality_short_label(current_quality_mode));
        int df_sz = fit_hud_font(dens_txt, stat_font, s2_w - 20);
        DrawText(dens_txt, s2_x + 10, card_y + card_h - df_sz - 6, df_sz,
                 (Color){115, 165, 200, 255});

        /* Bloco 3 (Centro): Carinha animada em Micro-ASCII */
        draw_hud_mugshot(face_x, face_y, face_w, face_h, player);

        /* Bloco 4: MUNICAO (rotulo a esquerda, valor a direita, barra livre abaixo) */
        Color ammo_col = (player->ammo > 5) ? (Color){252, 208, 72, 255}
                                            : (Color){245, 72, 72, 255};
        int melee = player->ability_state.berserk_time > 0;
        const char *am_val = melee ? "1.5m" : TextFormat("%02d/30", player->ammo);
        int am_lf = fit_hud_font("MUN", lbl_font, (s4_w - 20) / 2);
        const char *am_lbl = melee ? "ESPADA" :
            (MeasureText("MUNICAO", am_lf) + MeasureText(am_val, val_font) + 28 <= s4_w) ? "MUNICAO" : "MUN";
        int am_vf = fit_hud_font(am_val, val_font, s4_w - MeasureText(am_lbl, am_lf) - 24);
        DrawText(am_lbl, s4_x + 10, card_y + 8, am_lf, lbl_col);
        DrawText(am_val, s4_x + s4_w - MeasureText(am_val, am_vf) - 10, card_y + 6, am_vf, ammo_col);
        draw_hud_ascii_meter(s4_x + 10, bar_y, s4_w - 20, bar_h,
                             melee ? 1.0f - fminf(1, player->shoot_timer / 0.60f) : (float)player->ammo / 30.0f, ammo_col);

        /* Bloco 5: STATUS / CAMERA / MIRA / FPS */
        int row_gap = (card_h - 12) / 3;
        const char *mode_str = is_multiplayer
            ? (remote_connected ? "MP: ONLINE" : "MP: AGUARDANDO")
            : "SINGLEPLAYER";
        Color mode_col = (is_multiplayer && !remote_connected)
            ? (Color){250, 185, 65, 255}
            : (Color){115, 235, 145, 255};
        int f5_0 = fit_hud_font(mode_str, stat_font, s5_w - 18);
        DrawText(mode_str, s5_x + 9, card_y + 6, f5_0, mode_col);

        const char *cam_str = cam_third_person ? "CAM: 3aP [V]" : "CAM: 1aP [V]";
        Color cam_col = cam_third_person ? (Color){255, 190, 80, 255} : (Color){135, 215, 255, 255};
        int f5_1 = fit_hud_font(cam_str, stat_font, s5_w - 18);
        DrawText(cam_str, s5_x + 9, card_y + 6 + row_gap, f5_1, cam_col);

        if (fps_cap >= 0) {
            const char *cap_str = (fps_cap > 0) ? TextFormat("%d", fps_cap) : "MAX";
            const char *fps_txt = TextFormat("FPS: %d/%s", GetFPS(), cap_str);
            int f5_2 = fit_hud_font(fps_txt, stat_font, s5_w - 18);
            DrawText(fps_txt, s5_x + 9, card_y + card_h - f5_2 - 6, f5_2,
                     (Color){230, 235, 245, 255});
        } else {
            const char *lock_str = mouse_locked ? "MIRA ATIVA [TAB]" : "SOLTO [TAB]";
            Color lock_col = mouse_locked ? (Color){130, 165, 145, 255} : (Color){245, 185, 85, 255};
            int f5_2 = fit_hud_font(lock_str, stat_font, s5_w - 18);
            DrawText(lock_str, s5_x + 9, card_y + card_h - f5_2 - 6, f5_2, lock_col);
        }
    }
    if (player) {
        const AbilityState *a=&player->ability_state;
        const char *status="SEM HABILIDADE";
        Color accent={190,205,220,255};
        if (player->ability == ABILITY_TETO) {
            accent=(Color){255,115,180,255};
            status=player_attack_damage(player,100)>100 ? TextFormat("DANO +25%% | VEL +35%% | %.1fs",a->zone_time) :
                   a->zone_time>0 ? TextFormat("CORACOES %.1fs | RECARGA %.0fs",a->zone_time,ceilf(a->cooldown)) :
                   a->cooldown>0 ? TextFormat("CORACOES: RECARGA %.0fs",ceilf(a->cooldown)) : "CORACOES: PRONTO (AO ELIMINAR)";
        } else if (player->ability == ABILITY_SHADOW) {
            accent=(Color){185,125,245,255};
            status=a->cooldown>0 ? TextFormat("FOGO NEGRO: %.0fs",ceilf(a->cooldown)) : "[E] FOGO NEGRO: PRONTO";
        } else if (player->ability == ABILITY_GUERRILLA) {
            accent=(Color){175,225,115,255};
            status=a->guerrilla_time>=GUERRILLA_CHARGE_TIME ? "GUERRILHEIRO: PROXIMO TIRO x2" :
                TextFormat("GUERRILHEIRO: %.1fs SEM DANO",GUERRILLA_CHARGE_TIME-a->guerrilla_time);
        } else if (player->ability == ABILITY_BERSERK) {
            accent=(Color){255,95,80,255};
            status=a->berserk_time>0 ? TextFormat("BERSERK %.1fs | ESPADA | DANO RECEBIDO x2",a->berserk_time) :
                   a->berserk_used ? "BERSERK: JA UTILIZADO" : a->kills>=BERSERK_KILLS ? "[E] BERSERK: PRONTO" :
                   TextFormat("BERSERK: %d/5 KILLS",a->kills);
        }
        int f=GetScreenWidth()<1000?12:16;
        int width=MeasureText(status,f)+24;
        DrawRectangle(GetScreenWidth()-width-12,12,width,f+14,(Color){12,14,22,225});
        DrawText(status,GetScreenWidth()-width,19,f,accent);
        const char *kills=TextFormat("KILLS: %d",a->kills);
        DrawText(kills,GetScreenWidth()-MeasureText(kills,f)-24,f+34,f,WHITE);
        if (a->berserk_time>0) DrawRectangleLinesEx((Rectangle){2,2,(float)GetScreenWidth()-4,(float)GetScreenHeight()-4},4,(Color){190,20,30,220});
    }
}

void renderer_present_window(const Player *player, int is_multiplayer,
                             int remote_connected, int mouse_locked, int fps_cap) {
    if (!font_loaded) {
        return;
    }

    BeginDrawing();
    renderer_draw_scene_and_hud(player, is_multiplayer, remote_connected,
                                mouse_locked, fps_cap);
    EndDrawing();
}

void renderer_present_stdout(const Player *player, int is_multiplayer,
                             int remote_connected) {
    /* Amostra 80x26 da grade ativa r_width x r_height para validacao em terminal/CI */
    for (int row = 0; row < 26; row++) {
        int y = (row * r_height) / 26;
        char line[81];
        for (int col = 0; col < 80; col++) {
            int x = (col * r_width) / 80;
            line[col] = screen_grid[y][x].ch;
        }
        line[80] = '\0';
        puts(line);
    }

    if (player) {
        printf("================================================================================\n");
        printf(" VIDA: %3d%% | MUNICAO: %2d | PONTOS: %3d | POS: (%.1f, %.1f) | MODO: %s\n",
               player->health,
               player->ammo,
               player->score,
               player->x,
               player->y,
               is_multiplayer ? (remote_connected ? "MP-ONLINE" : "MP-WAIT") : "SINGLEPLAYER");
        printf("================================================================================\n");
    }

    fflush(stdout);
}
