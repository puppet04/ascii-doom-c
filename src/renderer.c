#include "renderer.h"
#include "embedded_assets.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Grade interna 3D dinamica com limites maximos estaticos */
#define MAX_R_WIDTH  640
#define MAX_R_HEIGHT 360

/* Parametros do atlas mipmapped (16x6 celulas de 64x64) e ring-buffers GPU anti-stall */
#define GPU_TEX_W       640
#define GPU_TEX_H       384
#define GPU_UI_W        256
#define GPU_UI_H        256
#define GPU_MUG_SIZE    64
#define GPU_SCENE_SLOTS 3
#define GPU_UI_SLOTS    6
#define ATLAS_COLS      16
#define ATLAS_ROWS      6
#define ATLAS_CELL      64
#define ATLAS_GLYPHS    96

/* Alturas relativas do olho da camera em relacao ao chao e ao teto */
#define EYE_FLOOR_H 0.50f
#define EYE_CEIL_H  1.18f

static int r_width = 320;
static int r_height = 160;
static int current_quality_mode = ASCII_QUALITY_AUTO;
static int current_visual_style = ASCII_STYLE_CLASSIC;
static float auto_perf_scale = 1.0f;

static AsciiCell screen_grid[MAX_R_HEIGHT][MAX_R_WIDTH];
static float z_buffer[MAX_R_WIDTH];
static float cell_depth[MAX_R_HEIGHT][MAX_R_WIDTH];
static Font ui_font;
static int font_loaded = 0;

/* Estado de renderizacao GPU com shaders especializados e ring-buffer de texturas */
static Shader gpu_shader_micro;
static Shader gpu_shader_classic;
static Texture2D gpu_atlas_tex;
static Texture2D gpu_scene_tex[GPU_SCENE_SLOTS];
static Texture2D gpu_ui_tex[GPU_UI_SLOTS];
static Texture2D gpu_mug_tex;
static int gpu_scene_slot = 0;
static int gpu_ui_slot = 0;
static unsigned int gpu_upload_buf[GPU_TEX_W * GPU_TEX_H];
static int loc_m_tex_size = -1, loc_m_glyph_atlas = -1;
static int loc_c_tex_size = -1, loc_c_grid_dims = -1, loc_c_glyph_atlas = -1;
static int gpu_ready = 0;

/* Estado da camera, horizonte em cache por frame e animacao de caminhada */
static float cam_pitch = 0.0f;
static int cam_third_person = 0;
static float cam_view_x = 0.0f;
static float cam_view_y = 0.0f;
static float cam_view_angle = 0.0f;
static int cam_view_ready = 0;
static float frame_horizon_y = 80.0f;
static float frame_time_now = 0.0f;
static float prev_bob_phase = 0.0f;
static float walk_amp = 0.0f;
static const GameMap *cam_view_map = NULL;

/* Cache do floor-casting do frame atual para uso via RendererApi (ex.: aneis no chao) */
static float floor_rel_inv_cos[MAX_R_WIDTH];
static float floor_base_dist[MAX_R_HEIGHT];
static float floor_col_step_x[MAX_R_WIDTH];
static float floor_col_step_y[MAX_R_WIDTH];
static int floor_col_draw_end[MAX_R_WIDTH];
static int floor_cast_begin = 0;
static int floor_cast_ready = 0;

/* Hooks registrados por outros modulos (ex.: map.c para armadilhas/kits/saida 'E') */
static RendererFloorHookFn g_floor_hook = NULL;
static RendererWallStyleHookFn g_wall_hook = NULL;
static RendererMinimapHookFn g_minimap_hook = NULL;

/* Rampa 100% ASCII para profundidade e sombreamento dinamico nos dois estilos */
static const char *DENSE_RAMP = "=-+*xX#%8&BWM@";
static const int DENSE_RAMP_LEN = 14;

/*
 * Shaders GLSL 330 especializados por estilo (menor pressao de registradores em iGPUs antigas):
 * - ASCII_FS_MICRO_CODE  : Estilo 2 (Micro-ASCII puro 1.0x, zero loop/branch, zero fundo pintado)
 * - ASCII_FS_CLASSIC_CODE: Estilo 1 (Sobreposto 1.72x com vizinhanca 2x2)
 */
static const char *ASCII_FS_MICRO_CODE =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D glyphAtlas;\n"
    "uniform vec2 texSize;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec2 cellCoord = fragTexCoord * texSize;\n"
    "    vec2 cellUV = (floor(cellCoord) + 0.5) / texSize;\n"
    "    vec4 cell = texture(texture0, cellUV);\n"
    "    float gIdx = floor(cell.a * 255.0 + 0.5);\n"
    "    if (gIdx < 0.5) discard;\n"
    "    float col = mod(gIdx, 16.0);\n"
    "    float row = floor(gIdx / 16.0);\n"
    "    const vec2 margin = vec2(0.24, 0.09);\n"
    "    const vec2 cellScale = vec2(1.0 / 16.0, 1.0 / 6.0);\n"
    "    vec2 uvs = mix(margin, 1.0 - margin, clamp(fract(cellCoord), 0.0, 1.0));\n"
    "    vec2 atlasUV = (vec2(col, row) + uvs) * cellScale;\n"
    "    vec2 span = (1.0 - 2.0 * margin) * cellScale;\n"
    "    float a = textureGrad(glyphAtlas, atlasUV, dFdx(cellCoord) * span, dFdy(cellCoord) * span).a;\n"
    "    float mask = smoothstep(0.03, 0.34, a);\n"
    "    if (mask < 0.02) discard;\n"
    "    finalColor = vec4(cell.rgb * fragColor.rgb, mask * fragColor.a);\n"
    "}\n";

static const char *ASCII_FS_CLASSIC_CODE =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D glyphAtlas;\n"
    "uniform vec2 texSize;\n"
    "uniform vec2 gridDims;\n"
    "out vec4 finalColor;\n"
    "float sampleGlyph(float gIdx, vec2 localUV, vec2 margin, float scaleInv, vec2 gx, vec2 gy) {\n"
    "    float col = mod(gIdx, 16.0);\n"
    "    float row = floor(gIdx / 16.0);\n"
    "    vec2 uvs = mix(margin, 1.0 - margin, clamp(localUV, 0.0, 1.0));\n"
    "    const vec2 cellScale = vec2(1.0 / 16.0, 1.0 / 6.0);\n"
    "    vec2 atlasUV = (vec2(col, row) + uvs) * cellScale;\n"
    "    vec2 span = (1.0 - 2.0 * margin) * cellScale * scaleInv;\n"
    "    float a = textureGrad(glyphAtlas, atlasUV, gx * span, gy * span).a;\n"
    "    return smoothstep(0.14, 0.52, a);\n"
    "}\n"
    "void main() {\n"
    "    vec2 cellCoord = fragTexCoord * texSize;\n"
    "    vec2 cellIdx = floor(cellCoord);\n"
    "    vec2 gx = dFdx(cellCoord);\n"
    "    vec2 gy = dFdy(cellCoord);\n"
    "    const float OVERLAP = 1.72;\n"
    "    vec3 outRgb = vec3(0.0);\n"
    "    float outA = 0.0;\n"
    "    for (int dy = -1; dy <= 0; dy++) {\n"
    "        for (int dx = -1; dx <= 0; dx++) {\n"
    "            vec2 idx = cellIdx + vec2(float(dx), float(dy));\n"
    "            if (idx.x < 0.0 || idx.y < 0.0 || idx.x >= gridDims.x || idx.y >= gridDims.y) continue;\n"
    "            vec2 rel = (cellCoord - idx) / OVERLAP;\n"
    "            if (rel.x < 0.0 || rel.x > 1.0 || rel.y < 0.0 || rel.y > 1.0) continue;\n"
    "            vec4 c = texture(texture0, (idx + 0.5) / texSize);\n"
    "            float g = floor(c.a * 255.0 + 0.5);\n"
    "            if (g >= 0.5) {\n"
    "                float m = sampleGlyph(g, rel, vec2(0.16, 0.08), 1.0 / OVERLAP, gx, gy);\n"
    "                outRgb = mix(outRgb, c.rgb, m);\n"
    "                outA = max(outA, m);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    if (outA < 0.05) discard;\n"
    "    finalColor = vec4(outRgb * fragColor.rgb, outA * fragColor.a);\n"
    "}\n";

static Color character_color(int material) {
    CharacterColor c = player_get_material_color(material);
    return (Color){c.r, c.g, c.b, c.a};
}

static inline AsciiColor make_color(unsigned char r, unsigned char g, unsigned char b) {
    AsciiColor c = {r, g, b};
    return c;
}

static inline unsigned char clamp_u8(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (unsigned char)v;
}

static inline float clamp_f01(float v) {
    if (v <= 0.0f) return 0.0f;
    if (v >= 1.0f) return 1.0f;
    return v;
}

static inline AsciiColor scale_rgb(float r, float g, float b, float factor) {
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.35f) factor = 1.35f;
    return make_color(clamp_u8(r * factor), clamp_u8(g * factor), clamp_u8(b * factor));
}

/* Floor rapido em inteiro sem chamar floorf() no loop interno por celula */
static inline int fast_floor_i(float v) {
    int i = (int)v;
    return (v < (float)i) ? (i - 1) : i;
}

static inline float fast_tri_wave(float v) {
    float f = v - (float)fast_floor_i(v);
    return (f < 0.5f) ? (4.0f * f - 1.0f) : (3.0f - 4.0f * f);
}

/* Tabela Bayer 4x4 pre-escalada em [-0.14, +0.14] para dithering ASCII sem divisoes em runtime */
static inline float bayer4x4(int x, int y) {
    static const float BAYER_OFF[4][4] = {
        { -0.140000f,  0.009333f, -0.102667f,  0.046667f },
        {  0.084000f, -0.065333f,  0.121333f, -0.028000f },
        { -0.084000f,  0.065333f, -0.121333f,  0.028000f },
        {  0.140000f, -0.009333f,  0.102667f, -0.046667f }
    };
    return BAYER_OFF[y & 3][x & 3];
}

static inline char sample_dense_ramp(float lum, int x, int y) {
    float v = clamp_f01(lum + bayer4x4(x, y));
    int idx = (int)(v * (float)(DENSE_RAMP_LEN - 1) + 0.5f);
    if (idx < 0) idx = 0;
    if (idx >= DENSE_RAMP_LEN) idx = DENSE_RAMP_LEN - 1;
    return DENSE_RAMP[idx];
}

void renderer_set_visual_style(int style_mode) {
    current_visual_style = (style_mode == ASCII_STYLE_SOLID)
        ? ASCII_STYLE_SOLID
        : ASCII_STYLE_CLASSIC;
}

int renderer_get_visual_style(void) {
    return current_visual_style;
}

static void set_cell(int x, int y, char ch, AsciiColor fg, AsciiColor bg) {
    if (x < 0 || x >= r_width || y < 0 || y >= r_height) {
        return;
    }
    screen_grid[y][x].ch = ch;
    screen_grid[y][x].fg = fg;
    screen_grid[y][x].bg = bg;
}

static void set_cell_if_closer(int x, int y, float depth, char ch, AsciiColor fg, AsciiColor bg) {
    if (x < 0 || x >= r_width || y < 0 || y >= r_height) {
        return;
    }
    if (depth >= z_buffer[x] || depth >= cell_depth[y][x]) {
        return;
    }
    cell_depth[y][x] = depth;
    screen_grid[y][x].ch = ch;
    screen_grid[y][x].fg = fg;
    screen_grid[y][x].bg = bg;
}

static const char *quality_short_label(int quality_mode) {
    switch (quality_mode) {
        case ASCII_QUALITY_LEVE:  return "BAIXO";
        case ASCII_QUALITY_MEDIA: return "MEDIO";
        case ASCII_QUALITY_ALTA:  return "ALTO";
        case ASCII_QUALITY_ULTRA: return "ULTRA";
        default:                  return "AUTO";
    }
}

void renderer_set_floor_hook(RendererFloorHookFn hook) {
    g_floor_hook = hook;
}

void renderer_set_wall_hook(RendererWallStyleHookFn hook) {
    g_wall_hook = hook;
}

void renderer_set_minimap_hook(RendererMinimapHookFn hook) {
    g_minimap_hook = hook;
}

void renderer_update_auto_perf(float frame_dt) {
    if (current_quality_mode != ASCII_QUALITY_AUTO) {
        auto_perf_scale = 1.0f;
        return;
    }
    if (frame_dt <= 0.0005f || frame_dt > 0.25f) {
        return;
    }

    static float ema_dt = 0.010f;
    static float degrade_accum = 0.0f;
    static float recover_accum = 0.0f;
    ema_dt = ema_dt * 0.90f + frame_dt * 0.10f;

    /* Em PCs lentos (< 56 FPS sustentados por 0.8s), reduz gradualmente a malha */
    if (ema_dt > (1.0f / 56.0f)) {
        recover_accum = 0.0f;
        degrade_accum += frame_dt;
        if (degrade_accum >= 0.80f && auto_perf_scale > 0.52f) {
            if (auto_perf_scale > 0.86f) auto_perf_scale = 0.86f;
            else if (auto_perf_scale > 0.74f) auto_perf_scale = 0.74f;
            else if (auto_perf_scale > 0.62f) auto_perf_scale = 0.62f;
            else auto_perf_scale = 0.52f;
            degrade_accum = 0.0f;
        }
    } else if (ema_dt < (1.0f / 85.0f) && auto_perf_scale < 1.0f) {
        degrade_accum = 0.0f;
        recover_accum += frame_dt;
        if (recover_accum >= 2.0f) {
            if (auto_perf_scale < 0.62f) auto_perf_scale = 0.62f;
            else if (auto_perf_scale < 0.74f) auto_perf_scale = 0.74f;
            else if (auto_perf_scale < 0.86f) auto_perf_scale = 0.86f;
            else auto_perf_scale = 1.0f;
            recover_accum = 0.0f;
        }
    } else {
        degrade_accum = 0.0f;
        recover_accum = 0.0f;
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
    int solid = (current_visual_style == ASCII_STYLE_SOLID);

    if (solid) {
        switch (quality_mode) {
            case ASCII_QUALITY_LEVE:  target_w = 220; target_h = 110; break;
            case ASCII_QUALITY_MEDIA: target_w = 300; target_h = 150; break;
            case ASCII_QUALITY_ALTA:  target_w = 400; target_h = 200; break;
            case ASCII_QUALITY_ULTRA: target_w = 540; target_h = 270; break;
            case ASCII_QUALITY_AUTO:
            default: {
                int view_h = (int)((float)screen_h * 0.855f);
                target_w = (int)(((float)screen_w / 3.0f) * auto_perf_scale);
                target_h = (int)(((float)view_h / 3.2f) * auto_perf_scale);
                if (target_w > 480) target_w = 480;
                if (target_h > 220) target_h = 220;
                if (target_w > target_h * 2 + 44) {
                    target_w = target_h * 2 + 44;
                }
                break;
            }
        }
    } else {
        switch (quality_mode) {
            case ASCII_QUALITY_LEVE:  target_w = 180; target_h = 90;  break;
            case ASCII_QUALITY_MEDIA: target_w = 256; target_h = 128; break;
            case ASCII_QUALITY_ALTA:  target_w = 320; target_h = 160; break;
            case ASCII_QUALITY_ULTRA: target_w = 400; target_h = 200; break;
            case ASCII_QUALITY_AUTO:
            default: {
                int view_h = (int)((float)screen_h * 0.855f);
                target_w = (int)(((float)screen_w / 4.0f) * auto_perf_scale);
                target_h = (int)(((float)view_h / 4.2f) * auto_perf_scale);
                if (target_w > 400) target_w = 400;
                if (target_h > 200) target_h = 200;
                if (target_w > target_h * 2 + 24) {
                    target_w = target_h * 2;
                }
                break;
            }
        }
    }

    int div = solid ? 2 : 3;
    int max_fit_w = screen_w / div;
    int max_fit_h = ((screen_h * 85) / 100) / div;
    if (target_w > max_fit_w) target_w = max_fit_w;
    if (target_h > max_fit_h) target_h = max_fit_h;

    int min_w = 120;
    int min_h = 60;
    if (target_w < min_w) target_w = min_w;
    if (target_h < min_h) target_h = min_h;
    if (target_w > MAX_R_WIDTH)  target_w = MAX_R_WIDTH;
    if (target_h > MAX_R_HEIGHT) target_h = MAX_R_HEIGHT;

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

/* Constroi o atlas 1024x384 (96 glifos 64x64 em 50px inteiros), gera mipmaps na GPU,
 * cria ring-buffers anti-stall e compila os dois shaders especializados GLSL 330. */
static void gpu_init_resources(void) {
    gpu_ready = 0;

    Image atlas_img = GenImageColor(ATLAS_COLS * ATLAS_CELL, ATLAS_ROWS * ATLAS_CELL, BLANK);
    const int font_px = 50; /* Multiplo inteiro 5x da fonte padrao Raylib (10px) */
    const int oy = (ATLAS_CELL - font_px) / 2;
    for (int i = 1; i < ATLAS_GLYPHS; i++) {
        char s[2] = {(char)(32 + i), '\0'};
        int col = i % ATLAS_COLS;
        int row = i / ATLAS_COLS;
        int gw = MeasureText(s, font_px);
        int ox = (ATLAS_CELL - gw) / 2;
        if (ox < 0) ox = 0;
        ImageDrawText(&atlas_img, s, col * ATLAS_CELL + ox, row * ATLAS_CELL + oy, font_px, WHITE);
    }
    gpu_atlas_tex = LoadTextureFromImage(atlas_img);
    UnloadImage(atlas_img);
    GenTextureMipmaps(&gpu_atlas_tex);
    SetTextureFilter(gpu_atlas_tex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(gpu_atlas_tex, TEXTURE_WRAP_CLAMP);

    Image scene_img = GenImageColor(GPU_TEX_W, GPU_TEX_H, BLANK);
    int all_tex_ok = IsTextureValid(gpu_atlas_tex);
    for (int i = 0; i < GPU_SCENE_SLOTS; i++) {
        gpu_scene_tex[i] = LoadTextureFromImage(scene_img);
        SetTextureFilter(gpu_scene_tex[i], TEXTURE_FILTER_POINT);
        SetTextureWrap(gpu_scene_tex[i], TEXTURE_WRAP_CLAMP);
        if (!IsTextureValid(gpu_scene_tex[i])) all_tex_ok = 0;
    }
    UnloadImage(scene_img);

    Image ui_img = GenImageColor(GPU_UI_W, GPU_UI_H, BLANK);
    for (int i = 0; i < GPU_UI_SLOTS; i++) {
        gpu_ui_tex[i] = LoadTextureFromImage(ui_img);
        SetTextureFilter(gpu_ui_tex[i], TEXTURE_FILTER_POINT);
        SetTextureWrap(gpu_ui_tex[i], TEXTURE_WRAP_CLAMP);
        if (!IsTextureValid(gpu_ui_tex[i])) all_tex_ok = 0;
    }
    UnloadImage(ui_img);

    Image mug_img = GenImageColor(GPU_MUG_SIZE, GPU_MUG_SIZE, BLANK);
    gpu_mug_tex = LoadTextureFromImage(mug_img);
    SetTextureFilter(gpu_mug_tex, TEXTURE_FILTER_POINT);
    SetTextureWrap(gpu_mug_tex, TEXTURE_WRAP_CLAMP);
    if (!IsTextureValid(gpu_mug_tex)) all_tex_ok = 0;
    UnloadImage(mug_img);

    gpu_shader_micro = LoadShaderFromMemory(NULL, ASCII_FS_MICRO_CODE);
    gpu_shader_classic = LoadShaderFromMemory(NULL, ASCII_FS_CLASSIC_CODE);

    if (all_tex_ok && IsShaderValid(gpu_shader_micro) && IsShaderValid(gpu_shader_classic)) {
        loc_m_tex_size = GetShaderLocation(gpu_shader_micro, "texSize");
        loc_m_glyph_atlas = GetShaderLocation(gpu_shader_micro, "glyphAtlas");
        loc_c_tex_size = GetShaderLocation(gpu_shader_classic, "texSize");
        loc_c_grid_dims = GetShaderLocation(gpu_shader_classic, "gridDims");
        loc_c_glyph_atlas = GetShaderLocation(gpu_shader_classic, "glyphAtlas");
        if (loc_m_tex_size >= 0 && loc_m_glyph_atlas >= 0 &&
            loc_c_tex_size >= 0 && loc_c_grid_dims >= 0 && loc_c_glyph_atlas >= 0) {
            gpu_ready = 1;
        }
    }
}

static void gpu_shutdown_resources(void) {
    if (IsShaderValid(gpu_shader_micro)) {
        UnloadShader(gpu_shader_micro);
    }
    if (IsShaderValid(gpu_shader_classic)) {
        UnloadShader(gpu_shader_classic);
    }
    if (IsTextureValid(gpu_atlas_tex)) {
        UnloadTexture(gpu_atlas_tex);
    }
    for (int i = 0; i < GPU_SCENE_SLOTS; i++) {
        if (IsTextureValid(gpu_scene_tex[i])) {
            UnloadTexture(gpu_scene_tex[i]);
        }
    }
    for (int i = 0; i < GPU_UI_SLOTS; i++) {
        if (IsTextureValid(gpu_ui_tex[i])) {
            UnloadTexture(gpu_ui_tex[i]);
        }
    }
    if (IsTextureValid(gpu_mug_tex)) {
        UnloadTexture(gpu_mug_tex);
    }
    gpu_ready = 0;
}

void renderer_init(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "SECTOR 404 - 165 FPS");
    const char *icon_path = FileExists("assets/icon.png") ? "assets/icon.png"
                          : (FileExists("../assets/icon.png") ? "../assets/icon.png" : NULL);
    if (icon_path) {
        Image win_icon = LoadImage(icon_path);
        if (win_icon.data != NULL) {
            SetWindowIcon(win_icon);
            UnloadImage(win_icon);
        }
    } else {
        int icon_len = 0;
        const unsigned char *icon_data = embedded_icon_png_data(&icon_len);
        if (icon_data && icon_len > 0) {
            Image win_icon = LoadImageFromMemory(".png", icon_data, icon_len);
            if (win_icon.data != NULL) {
                SetWindowIcon(win_icon);
                UnloadImage(win_icon);
            }
        }
    }
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
    gpu_init_resources();
}

void renderer_close(void) {
    if (IsWindowReady()) {
        gpu_shutdown_resources();
        if (IsCursorHidden()) {
            EnableCursor();
        }
        CloseWindow();
    }
    font_loaded = 0;
}

static void pack_ascii_to_u32(const AsciiCell *cells, int width, int height,
                              int row_stride, int solid, unsigned int *dst32) {
    static unsigned char solid_boost_lut[256];
    static int solid_lut_ready = 0;
    if (!solid_lut_ready) {
        solid_boost_lut[0] = 0;
        for (int i = 1; i < 256; i++) {
            float norm = (float)i / 255.0f;
            solid_boost_lut[i] = clamp_u8(powf(norm, 0.80f) * 255.0f * 1.16f);
        }
        solid_lut_ready = 1;
    }

    if (solid) {
        for (int y = 0; y < height; y++) {
            const AsciiCell *src_row = &cells[y * row_stride];
            unsigned int *out_row = &dst32[y * width];
            for (int x = 0; x < width; x++) {
                unsigned char uc = (unsigned char)src_row[x].ch;
                if (uc > 32 && uc <= 127) {
                    AsciiColor sfg = src_row[x].fg;
                    out_row[x] = ((unsigned int)solid_boost_lut[sfg.r]) |
                                 ((unsigned int)solid_boost_lut[sfg.g] << 8) |
                                 ((unsigned int)solid_boost_lut[sfg.b] << 16) |
                                 (((unsigned int)(uc - 32u)) << 24);
                } else {
                    out_row[x] = 0u;
                }
            }
        }
    } else {
        for (int y = 0; y < height; y++) {
            const AsciiCell *src_row = &cells[y * row_stride];
            unsigned int *out_row = &dst32[y * width];
            for (int x = 0; x < width; x++) {
                unsigned char uc = (unsigned char)src_row[x].ch;
                if (uc > 32 && uc <= 127) {
                    AsciiColor sfg = src_row[x].fg;
                    out_row[x] = ((unsigned int)sfg.r) |
                                 ((unsigned int)sfg.g << 8) |
                                 ((unsigned int)sfg.b << 16) |
                                 (((unsigned int)(uc - 32u)) << 24);
                } else {
                    out_row[x] = 0u;
                }
            }
        }
    }
}

static void draw_gpu_grid_tex(Texture2D tex, int tex_w, int tex_h,
                              int width, int height, int solid,
                              float dst_x, float dst_y, float dst_w, float dst_h) {
    float tex_size[2] = {(float)tex_w, (float)tex_h};
    if (solid) {
        BeginShaderMode(gpu_shader_micro);
        SetShaderValue(gpu_shader_micro, loc_m_tex_size, tex_size, SHADER_UNIFORM_VEC2);
        SetShaderValueTexture(gpu_shader_micro, loc_m_glyph_atlas, gpu_atlas_tex);
        DrawTexturePro(tex,
                       (Rectangle){0.0f, 0.0f, (float)width, (float)height},
                       (Rectangle){dst_x, dst_y, dst_w, dst_h},
                       (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
        EndShaderMode();
    } else {
        float grid_dims[2] = {(float)width, (float)height};
        BeginShaderMode(gpu_shader_classic);
        SetShaderValue(gpu_shader_classic, loc_c_tex_size, tex_size, SHADER_UNIFORM_VEC2);
        SetShaderValue(gpu_shader_classic, loc_c_grid_dims, grid_dims, SHADER_UNIFORM_VEC2);
        SetShaderValueTexture(gpu_shader_classic, loc_c_glyph_atlas, gpu_atlas_tex);
        DrawTexturePro(tex,
                       (Rectangle){0.0f, 0.0f, (float)width, (float)height},
                       (Rectangle){dst_x, dst_y, dst_w, dst_h},
                       (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
        EndShaderMode();
    }
}

/* Envia qualquer grade AsciiCell 2D para a GPU em 1 draw call com ring-buffer anti-stall. */
void renderer_draw_ascii_buffer(const AsciiCell *cells, int width, int height,
                                int row_stride, float dst_x, float dst_y,
                                float dst_w, float dst_h) {
    if (!font_loaded || !cells || width <= 0 || height <= 0 ||
        row_stride < width || dst_w <= 0.0f || dst_h <= 0.0f) {
        return;
    }

    int solid = (current_visual_style == ASCII_STYLE_SOLID);

    if (gpu_ready && width <= GPU_TEX_W && height <= GPU_TEX_H) {
        pack_ascii_to_u32(cells, width, height, row_stride, solid, gpu_upload_buf);

        Texture2D target_tex;
        int tex_w, tex_h;
        if (width <= GPU_UI_W && height <= GPU_UI_H) {
            target_tex = gpu_ui_tex[gpu_ui_slot];
            gpu_ui_slot = (gpu_ui_slot + 1) % GPU_UI_SLOTS;
            tex_w = GPU_UI_W;
            tex_h = GPU_UI_H;
        } else {
            target_tex = gpu_scene_tex[gpu_scene_slot];
            gpu_scene_slot = (gpu_scene_slot + 1) % GPU_SCENE_SLOTS;
            tex_w = GPU_TEX_W;
            tex_h = GPU_TEX_H;
        }

        UpdateTextureRec(target_tex,
                         (Rectangle){0.0f, 0.0f, (float)width, (float)height},
                         gpu_upload_buf);
        draw_gpu_grid_tex(target_tex, tex_w, tex_h, width, height, solid,
                          dst_x, dst_y, dst_w, dst_h);
        return;
    }

    /* Fallback CPU automatico se o shader GPU nao inicializar */
    float cell_w = dst_w / (float)width;
    float cell_h = dst_h / (float)height;
    float glyph_size = solid ? cell_h : (cell_h * 1.85f);
    if (glyph_size < 4.0f) glyph_size = 4.0f;

    for (int y = 0; y < height; y++) {
        float py = dst_y + (float)y * cell_h;
        const AsciiCell *row = &cells[y * row_stride];
        for (int x = 0; x < width; x++) {
            AsciiCell c = row[x];
            if (c.ch != ' ' && c.ch != '\0') {
                float px = dst_x + (float)x * cell_w;
                DrawTextCodepoint(ui_font, (int)c.ch,
                                  (Vector2){px, py},
                                  glyph_size,
                                  (Color){c.fg.r, c.fg.g, c.fg.b, 255});
            }
        }
    }
}

void renderer_clear(void) {
    static AsciiCell empty_row[MAX_R_WIDTH];
    static float max_depth_row[MAX_R_WIDTH];
    static int clear_rows_ready = 0;
    if (!clear_rows_ready) {
        AsciiColor black = make_color(0, 0, 0);
        for (int i = 0; i < MAX_R_WIDTH; i++) {
            empty_row[i].ch = ' ';
            empty_row[i].fg = black;
            empty_row[i].bg = black;
            max_depth_row[i] = MAX_DEPTH;
        }
        clear_rows_ready = 1;
    }

    cam_view_ready = 0;
    cam_view_map = NULL;
    floor_cast_ready = 0;
    const size_t row_cells_bytes = (size_t)r_width * sizeof(AsciiCell);
    const size_t row_depth_bytes = (size_t)r_width * sizeof(float);
    for (int y = 0; y < r_height; y++) {
        memcpy(screen_grid[y], empty_row, row_cells_bytes);
        memcpy(cell_depth[y], max_depth_row, row_depth_bytes);
    }
    memcpy(z_buffer, max_depth_row, row_depth_bytes);
}

/*
 * Renderiza o cenario 3D completo na grade ASCII (paredes, teto e chao) usando
 * arquitetura em 2 passes otimizada para cache L1:
 *   - Passe 1 (por coluna x): lanca r_width raios via DDA (Digital Differential Analyzer),
 *     corrige distorcao olho-de-peixe (perp_dist = raw_dist * cos), preenche z_buffer[x]
 *     para oclusao posterior dos sprites e rasteriza a faixa vertical da parede atingida.
 *   - Passe 2 (por linha y): desenha teto e chao sequencialmente em ordem de linha,
 *     reaproveitando col_ray_dx/dy/inv_cos e consultando g_floor_hook (armadilhas/kits).
 */
void renderer_draw_walls(const GameMap *map, const Player *player) {
    if (!map || !player) {
        return;
    }
    cam_view_map = map;

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
            if (map_is_visible_wall(map, (int)floorf(tx + margin), (int)floorf(ty)) ||
                map_is_visible_wall(map, (int)floorf(tx - margin), (int)floorf(ty)) ||
                map_is_visible_wall(map, (int)floorf(tx), (int)floorf(ty + margin)) ||
                map_is_visible_wall(map, (int)floorf(tx), (int)floorf(ty - margin))) {
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
    frame_horizon_y = horizon_y;
    frame_time_now = IsWindowReady() ? (float)GetTime() : 0.0f;

    /* LUT relativa dos raios da camera: zero sinf/cosf dentro do laco de colunas */
    static float rel_cos_lut[MAX_R_WIDTH];
    static float rel_sin_lut[MAX_R_WIDTH];
    static int ray_lut_w = -1;
    static float ray_lut_fov = -1.0f;
    if (ray_lut_w != r_width || fabsf(ray_lut_fov - player->fov) > 1e-6f) {
        const float half = player->fov * 0.5f;
        const float step_a = player->fov / (float)r_width;
        for (int x = 0; x < r_width; x++) {
            float rel_a = -half + (float)x * step_a;
            rel_cos_lut[x] = cosf(rel_a);
            rel_sin_lut[x] = sinf(rel_a);
            float cc = (rel_cos_lut[x] < 0.25f) ? 0.25f : rel_cos_lut[x];
            floor_rel_inv_cos[x] = 1.0f / cc;
        }
        ray_lut_w = r_width;
        ray_lut_fov = player->fov;
    }
    const float cam_cos = cosf(cam_view_angle);
    const float cam_sin = sinf(cam_view_angle);

    /* Arrays pre-calculados por coluna (Passe 1) para cast de teto/chao em ordem de linha (Passe 2) */
    static float base_ceil_dist[MAX_R_HEIGHT];
    static int col_draw_start[MAX_R_WIDTH];

    for (int y = 0; y < r_height; y++) {
        float cy = (float)y + 0.5f;
        float pc = horizon_y - cy;
        if (pc < 0.5f) pc = 0.5f;
        base_ceil_dist[y] = (EYE_CEIL_H * (float)r_height) / pc;
        float pf = cy - horizon_y;
        if (pf < 0.5f) pf = 0.5f;
        floor_base_dist[y] = (EYE_FLOOR_H * (float)r_height) / pf;
    }

    const float inv_13_5 = 1.0f / 13.5f;
    const float inv_13_0 = 1.0f / 13.0f;

    /* PASSE 1: Raycasting DDA por coluna e rasterizacao especializada das paredes */
    for (int x = 0; x < r_width; x++) {
        float r_c_lut = rel_cos_lut[x];
        float r_s_lut = rel_sin_lut[x];
        float ray_dir_x = cam_cos * r_c_lut - cam_sin * r_s_lut;
        float ray_dir_y = cam_sin * r_c_lut + cam_cos * r_s_lut;

        int map_x = fast_floor_i(cam_view_x);
        int map_y = fast_floor_i(cam_view_y);

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

            if (map_is_visible_wall(map, map_x, map_y)) {
                hit = 1;
            }
            steps++;
        }

        float raw_dist = MAX_DEPTH;
        if (hit) {
            raw_dist = (side == 0) ? (side_dist_x - delta_dist_x)
                                   : (side_dist_y - delta_dist_y);
        }

        float cos_corr = (r_c_lut < 0.25f) ? 0.25f : r_c_lut;
        float inv_cos = floor_rel_inv_cos[x];

        float perp_dist = raw_dist * cos_corr;
        if (perp_dist < 0.1f) perp_dist = 0.1f;
        if (perp_dist > MAX_DEPTH) perp_dist = MAX_DEPTH;

        z_buffer[x] = perp_dist;

        float u = (side == 0)
            ? (cam_view_y + raw_dist * ray_dir_y)
            : (cam_view_x + raw_dist * ray_dir_x);
        u -= (float)fast_floor_i(u);
        int is_block_edge = (u < 0.025f || u > 0.975f);

        float floor_proj = (EYE_FLOOR_H * (float)r_height) / perp_dist;
        float ceil_proj = (EYE_CEIL_H * (float)r_height) / perp_dist;
        int raw_start = fast_floor_i(horizon_y - ceil_proj + 0.5f);
        int raw_end = fast_floor_i(horizon_y + floor_proj + 0.5f);
        int line_height = raw_end - raw_start;
        if (line_height < 1) line_height = 1;
        int draw_start = (raw_start < 0) ? 0 : raw_start;
        int draw_end = (raw_end >= r_height) ? (r_height - 1) : raw_end;
        float inv_lh = 1.0f / (float)line_height;

        floor_col_step_x[x] = ray_dir_x * inv_cos;
        floor_col_step_y[x] = ray_dir_y * inv_cos;
        col_draw_start[x] = draw_start;
        floor_col_draw_end[x] = draw_end;

        char tile = hit ? map_get_tile(map, map_x, map_y) : '#';
        AsciiColor base_wall = get_tile_base_color(tile);
        float fog_dist = perp_dist;
        int shade_side = side;
        if (hit && g_wall_hook) {
            g_wall_hook(map, map_x, map_y, tile, &base_wall, &fog_dist, &is_block_edge, &shade_side);
        }

        float wall_fog = 1.0f - fog_dist * inv_13_5;
        if (wall_fog < 0.16f) wall_fog = 0.16f;
        if (shade_side == 1) wall_fog *= 0.72f;

        const float r_c = (float)base_wall.r;
        const float g_c = (float)base_wall.g;
        const float b_c = (float)base_wall.b;
        float v_step = ((float)(draw_start - raw_start) + 0.5f) * inv_lh;

        /* Iça o switch do material para fora do laco vertical da parede + incremento de v */
        if (is_block_edge && fog_dist < 10.0f) {
            float v = v_step;
            for (int y = draw_start; y <= draw_end; y++, v += inv_lh) {
                float vc = clamp_f01(v);
                float tex_mul = 0.62f * ((vc < 0.06f || vc > 0.94f) ? 0.72f : 1.0f);
                float shade = wall_fog * tex_mul;
                screen_grid[y][x].ch = '|';
                screen_grid[y][x].fg = scale_rgb(r_c, g_c, b_c, shade);
                screen_grid[y][x].bg = bg_black;
                cell_depth[y][x] = perp_dist;
            }
        } else if (tile == '#') {
            const float ux_base = u * 3.0f;
            const unsigned map_h = (unsigned)(map_x * 31u + map_y * 17u);
            float v = v_step;
            for (int y = draw_start; y <= draw_end; y++, v += inv_lh) {
                float vc = clamp_f01(v);
                float vy = vc * 8.0f;
                int brow = (int)vy;
                float ux = ux_base + ((brow & 1) ? 0.5f : 0.0f);
                int bcol = (int)ux;
                float fv = vy - (float)brow;
                float fu = ux - (float)bcol;
                float tex_mul = (vc < 0.06f || vc > 0.94f) ? 0.72f : 1.0f;
                char wch;
                if (fv < 0.12f || fu < 0.08f) {
                    tex_mul *= 0.52f;
                    wch = (fv < 0.12f) ? '=' : ':';
                } else {
                    unsigned h = (unsigned)((brow * 73u) ^ (bcol * 139u) ^ map_h);
                    tex_mul *= 0.84f + 0.24f * ((float)(h & 15u) / 15.0f);
                    wch = 0;
                }
                float shade = wall_fog * tex_mul;
                screen_grid[y][x].ch = wch ? wch : sample_dense_ramp(shade, x, y);
                screen_grid[y][x].fg = scale_rgb(r_c, g_c, b_c, shade);
                screen_grid[y][x].bg = bg_black;
                cell_depth[y][x] = perp_dist;
            }
        } else if (tile == '@') {
            float at_conduit_mod = 0.85f + 0.15f * cosf((u - 0.5f) * 8.0f);
            int at_in_u_border = (u < 0.07f || u > 0.93f);
            int at_in_u_conduit = (fabsf(u - 0.5f) < 0.06f);
            const float conduit_glow = fmaxf(wall_fog * 1.25f, 0.72f);

            float v = v_step;
            for (int y = draw_start; y <= draw_end; y++, v += inv_lh) {
                float vc = clamp_f01(v);
                if (at_in_u_conduit || fabsf(vc - 0.5f) < 0.045f) {
                    screen_grid[y][x].ch = '@';
                    screen_grid[y][x].fg = scale_rgb(135.0f, 230.0f, 255.0f, conduit_glow);
                } else if (at_in_u_border || vc < 0.08f || vc > 0.92f) {
                    float tex_mul = 0.65f * ((vc < 0.06f || vc > 0.94f) ? 0.72f : 1.0f);
                    float shade = wall_fog * tex_mul;
                    screen_grid[y][x].ch = '#';
                    screen_grid[y][x].fg = scale_rgb(r_c, g_c, b_c, shade);
                } else {
                    float tex_mul = at_conduit_mod * ((vc < 0.06f || vc > 0.94f) ? 0.72f : 1.0f);
                    float shade = wall_fog * tex_mul;
                    screen_grid[y][x].ch = sample_dense_ramp(shade, x, y);
                    screen_grid[y][x].fg = scale_rgb(r_c, g_c, b_c, shade);
                }
                screen_grid[y][x].bg = bg_black;
                cell_depth[y][x] = perp_dist;
            }
        } else if (tile == '%') {
            float rib_mod = u * 4.0f;
            rib_mod -= (float)(int)rib_mod;
            int bio_in_rib = (fabsf(rib_mod - 0.5f) < 0.14f);

            float v = v_step;
            for (int y = draw_start; y <= draw_end; y++, v += inv_lh) {
                float vc = clamp_f01(v);
                float tex_mul = (vc < 0.06f || vc > 0.94f) ? 0.72f : 1.0f;
                char wch;
                if (bio_in_rib) {
                    tex_mul *= 0.58f;
                    wch = '#';
                } else {
                    tex_mul *= 0.85f + 0.25f * fast_tri_wave(vc * 2.86f);
                    wch = 0;
                }
                float shade = wall_fog * tex_mul;
                screen_grid[y][x].ch = wch ? wch : sample_dense_ramp(shade, x, y);
                screen_grid[y][x].fg = scale_rgb(r_c, g_c, b_c, shade);
                screen_grid[y][x].bg = bg_black;
                cell_depth[y][x] = perp_dist;
            }
        } else {
            const float u3 = u * 3.0f;
            float v = v_step;
            for (int y = draw_start; y <= draw_end; y++, v += inv_lh) {
                float vc = clamp_f01(v);
                float st = u3 + vc * 3.0f;
                float stripe = st - (float)(int)st;
                float tex_mul = (stripe < 0.40f ? 0.58f : 1.0f) *
                                ((vc < 0.06f || vc > 0.94f) ? 0.72f : 1.0f);
                float shade = wall_fog * tex_mul;
                screen_grid[y][x].ch = sample_dense_ramp(shade, x, y);
                screen_grid[y][x].fg = scale_rgb(r_c, g_c, b_c, shade);
                screen_grid[y][x].bg = bg_black;
                cell_depth[y][x] = perp_dist;
            }
        }
    }

    /* PASSE 2A: Floor-casting de Teto em ordem de linha (acesso sequencial em L1) */
    int ceil_lim = (int)ceilf(horizon_y);
    if (ceil_lim < 0) ceil_lim = 0;
    if (ceil_lim > r_height) ceil_lim = r_height;

    for (int y = 0; y < ceil_lim; y++) {
        float bcd = base_ceil_dist[y];
        if (bcd > MAX_DEPTH) continue;

        AsciiCell *row = screen_grid[y];
        for (int x = 0; x < r_width; x++) {
            if (y >= col_draw_start[x]) continue;

            float row_dist = bcd * floor_rel_inv_cos[x];
            if (row_dist > MAX_DEPTH) continue;

            float wx = cam_view_x + bcd * floor_col_step_x[x];
            float wy = cam_view_y + bcd * floor_col_step_y[x];
            float cu = wx - (float)fast_floor_i(wx);
            float cv = wy - (float)fast_floor_i(wy);

            int is_beam = (cu < 0.08f || cv < 0.08f);
            int is_light = (fabsf(cu - 0.5f) < 0.17f && fabsf(cv - 0.5f) < 0.17f);

            float cfog = 1.0f - row_dist * inv_13_0;
            if (cfog < 0.14f) cfog = 0.14f;

            row[x].bg = bg_black;
            if (is_light) {
                float lum = 0.92f * cfog;
                row[x].ch = sample_dense_ramp(lum, x, y);
                row[x].fg = scale_rgb(175.0f, 155.0f, 115.0f, lum + 0.12f);
            } else if (is_beam) {
                float lum = 0.48f * cfog;
                row[x].ch = '=';
                row[x].fg = scale_rgb(76.0f, 86.0f, 108.0f, lum + 0.14f);
            } else {
                float plate_tex = 0.32f + 0.10f * fast_tri_wave(wx * 0.95f) * fast_tri_wave(wy * 0.95f);
                float lum = plate_tex * cfog;
                row[x].ch = sample_dense_ramp(lum, x, y);
                row[x].fg = scale_rgb(56.0f, 66.0f, 88.0f, lum + 0.16f);
            }
        }
    }

    /* PASSE 2B: Floor-casting de Chao em ordem de linha (acesso sequencial em L1) */
    int floor_begin = fast_floor_i(horizon_y);
    if (floor_begin < 0) floor_begin = 0;
    if (floor_begin > r_height) floor_begin = r_height;
    floor_cast_begin = floor_begin;
    floor_cast_ready = 1;

    for (int y = floor_begin; y < r_height; y++) {
        float bfd = floor_base_dist[y];
        if (bfd > MAX_DEPTH) continue;

        AsciiCell *row = screen_grid[y];
        for (int x = 0; x < r_width; x++) {
            if (y <= floor_col_draw_end[x]) continue;

            float row_dist = bfd * floor_rel_inv_cos[x];
            if (row_dist > MAX_DEPTH) continue;

            float wx = cam_view_x + bfd * floor_col_step_x[x];
            float wy = cam_view_y + bfd * floor_col_step_y[x];

            int floor_mx = fast_floor_i(wx);
            int floor_my = fast_floor_i(wy);
            float fu = wx - (float)floor_mx;
            float fv = wy - (float)floor_my;

            /* Fast-path: pula chamada indireta do hook em 98% do piso comum ('.' ou ' ') */
            if (g_floor_hook && floor_mx >= 0 && floor_mx < map->width &&
                floor_my >= 0 && floor_my < map->height) {
                char ft = map->tiles[floor_my][floor_mx];
                if (ft != '.' && ft != ' ') {
                    char dch = 0;
                    AsciiColor dcol = bg_black;
                    if (g_floor_hook(map, floor_mx, floor_my, fu, fv, row_dist, &dch, &dcol)) {
                        row[x].ch = dch;
                        row[x].fg = dcol;
                        row[x].bg = bg_black;
                        continue;
                    }
                }
            }

            int checker = ((floor_mx + floor_my) & 1);
            int is_seam = (fu < 0.055f || fv < 0.055f);
            int is_rivet = ((fu > 0.14f && fu < 0.20f) || (fu > 0.80f && fu < 0.86f)) &&
                           ((fv > 0.14f && fv < 0.20f) || (fv > 0.80f && fv < 0.86f));

            float ffog = 1.0f - row_dist * inv_13_5;
            if (ffog < 0.18f) ffog = 0.18f;

            row[x].bg = bg_black;
            if (is_seam) {
                float lum = 0.78f * ffog;
                row[x].ch = (fu < 0.055f && fv < 0.055f) ? '+' : '=';
                row[x].fg = scale_rgb(165.0f, 110.0f, 75.0f, lum + 0.10f);
            } else if (is_rivet) {
                float lum = 0.85f * ffog;
                row[x].ch = '#';
                row[x].fg = scale_rgb(185.0f, 145.0f, 95.0f, lum);
            } else {
                float base_tone = checker ? 0.54f : 0.40f;
                float micro_tex = 0.08f * fast_tri_wave(wx * 1.91f) * fast_tri_wave(wy * 1.91f);
                float lum = clamp_f01((base_tone + micro_tex) * ffog);
                row[x].ch = sample_dense_ramp(lum, x, y);
                row[x].fg = scale_rgb(138.0f, 112.0f, 92.0f, lum + 0.14f);
            }
        }
    }
}

void renderer_draw_sprites(const Player *player, const SpriteEntity *sprites, int count) {
    if (!player) {
        return;
    }

    float vx = cam_view_ready ? cam_view_x : player->x;
    float vy = cam_view_ready ? cam_view_y : player->y;
    float vangle = cam_view_ready ? cam_view_angle : player->angle;
    float horizon_y = cam_view_ready
        ? frame_horizon_y
        : ((float)r_height * 0.5f + cam_pitch * ((float)r_height / 180.0f) +
           (cam_third_person ? 0.0f : (sinf(player->bob_phase * 2.0f) * 1.6f * walk_amp * ((float)r_height / 180.0f))));

    SpriteEntity draw_list[MAX_SPRITES + 1];
    float dists[MAX_SPRITES + 1];
    int total_count = 0;
    const float max_d2 = MAX_DEPTH * MAX_DEPTH;

    if (sprites && count > 0) {
        int limit = (count > MAX_SPRITES) ? MAX_SPRITES : count;
        for (int i = 0; i < limit; i++) {
            if (!sprites[i].active) {
                continue;
            }
            float dx = sprites[i].x - vx;
            float dy = sprites[i].y - vy;
            float d2 = dx * dx + dy * dy;
            if (d2 < 0.0625f || d2 >= max_d2) {
                continue;
            }
            draw_list[total_count] = sprites[i];
            dists[total_count] = sqrtf(d2);
            total_count++;
        }
    }

    if (cam_third_person) {
        float dx = player->x - vx;
        float dy = player->y - vy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= 0.1156f && d2 < max_d2) {
            SpriteEntity self_sp;
            memset(&self_sp, 0, sizeof(self_sp));
            self_sp.x = player->x;
            self_sp.y = player->y;
            self_sp.active = 1;
            self_sp.shooting = (player->shoot_timer > 0.12f) ? 1 : 0;
            self_sp.label = 'P';
            self_sp.visual_id = player->visual_id;
            self_sp.appearance = player->appearance;
            draw_list[total_count] = self_sp;
            dists[total_count] = sqrtf(d2);
            total_count++;
        }
    }

    if (total_count <= 0) {
        return;
    }

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
    float time_now = cam_view_ready ? frame_time_now : (IsWindowReady() ? (float)GetTime() : 0.0f);

    for (int i = 0; i < total_count; i++) {
        float dx = draw_list[i].x - vx;
        float dy = draw_list[i].y - vy;
        float dist = dists[i];
        int is_self = (draw_list[i].label == 'P');

        float sprite_angle = wrap_angle_signed(atan2f(dy, dx) - vangle);
        if (fabsf(sprite_angle) > player->fov * 0.65f) {
            continue;
        }

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
        int sprite_h = (int)floorf((0.78f * (float)r_height) / dist + 0.5f);
        if (sprite_h < 8) sprite_h = 8;
        if (sprite_h > r_height) sprite_h = r_height;

        int sprite_w = (sprite_h * 5) / 8;
        if (sprite_w < 6) sprite_w = 6;
        if (sprite_w > r_width) sprite_w = r_width;

        int top_y = bot_y - sprite_h + 1;
        int left_x = center_x - sprite_w / 2;
        int right_x = center_x + sprite_w / 2;

        int sx_lo = (left_x < 0) ? 0 : left_x;
        int sx_hi = (right_x >= r_width) ? (r_width - 1) : right_x;
        int sy_lo = (top_y < 0) ? 0 : top_y;
        int sy_hi = (bot_y >= r_height) ? (r_height - 1) : bot_y;
        if (sx_lo > sx_hi || sy_lo > sy_hi) {
            continue;
        }

        int use_custom_skin = 0;
        int skin_vid = PRESET_FUZILEIRO;
        const CharacterAppearance *skin_app = NULL;
        int skin_back_view = 0;
        if (is_self) {
            skin_vid = player->visual_id;
            skin_app = &player->appearance;
            skin_back_view = 1;
            use_custom_skin = player_appearance_visual(skin_vid, skin_app).custom_sprite;
        } else if (draw_list[i].label == 'O') {
            skin_vid = draw_list[i].visual_id;
            skin_app = &draw_list[i].appearance;
            skin_back_view = 0;
            use_custom_skin = player_appearance_visual(skin_vid, skin_app).custom_sprite;
        }

        float fog = 1.0f - (dist / 14.5f);
        if (fog < 0.24f) fog = 0.24f;
        const float inv_sw = 1.0f / (float)(sprite_w > 0 ? sprite_w : 1);
        const float inv_sh = 1.0f / (float)sprite_h;
        const float v_step = ((float)(sy_lo - top_y) + 0.5f) * inv_sh;

        for (int sx = sx_lo; sx <= sx_hi; sx++) {
            if (dist >= z_buffer[sx]) {
                continue;
            }

            float u = ((float)(sx - left_x) + 0.5f) * inv_sw;
            float sx_s = (u - 0.5f) * 2.0f;
            float sx_u = sx_s - sway;
            float ax_u = fabsf(sx_u);
            float v = v_step;

            for (int sy = sy_lo; sy <= sy_hi; sy++, v += inv_sh) {
                if (dist >= cell_depth[sy][sx]) {
                    continue;
                }
                float vu = v + bounce * 0.026f;

                if (use_custom_skin) {
                    if (draw_list[i].shooting && sx_u > 0.38f && sx_u < 0.92f &&
                        vu > 0.34f && vu < 0.50f) {
                        cell_depth[sy][sx] = dist;
                        screen_grid[sy][sx] = (AsciiCell){
                            '@', scale_rgb(255.0f, 230.0f, 85.0f, fmaxf(0.92f * fog, 0.65f)), bg_black
                        };
                        continue;
                    }
                    int mat = player_appearance_material(skin_vid, skin_app, sx_u, vu, skin_back_view);
                    if (mat) {
                        static const char *SKIN_3D_RAMP = "#%8&BWM@";
                        Color color = character_color(mat);
                        float nz = sqrtf(fmaxf(0.20f, 1.0f - sx_u * sx_u * 0.55f));
                        float lum = clamp_f01((0.50f + 0.46f * nz) * fog);
                        float sl = clamp_f01(lum + bayer4x4(sx, sy) * 0.35f);
                        int ri = (int)(sl * 7.9f);
                        if (ri < 0) ri = 0;
                        if (ri > 7) ri = 7;
                        char ch = (mat == 3) ? '@' : SKIN_3D_RAMP[ri];
                        cell_depth[sy][sx] = dist;
                        screen_grid[sy][sx] = (AsciiCell){
                            ch,
                            scale_rgb((float)color.r, (float)color.g, (float)color.b,
                                      (0.76f + 0.28f * nz) * fog),
                            bg_black
                        };
                    }
                    continue;
                }

                int hit_part = 0;
                float nx = 0.0f, ny = 0.0f;
                float depth_shade = 1.0f;

                if (vu < 0.21f) {
                    float hx = ax_u / 0.34f;
                    float hy = (vu - 0.105f) / 0.105f;
                    if (hx * hx + hy * hy <= 1.0f) {
                        if (is_self) {
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
                            if (ax_u >= 0.10f && ax_u <= 0.22f && vu >= 0.31f && vu <= 0.41f) {
                                hit_part = 2;
                                depth_shade = 0.88f + 0.24f * amp;
                            } else {
                                hit_part = 3;
                            }
                        } else if (!is_self && fabsf(vu - 0.35f) < 0.055f && ax_u < 0.14f) {
                            hit_part = 2;
                        } else if (vu > 0.50f && vu < 0.54f) {
                            hit_part = 3;
                        }
                    } else if (v_arm < 0.40f && ax_u <= 0.86f) {
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

                    if (is_self && sx_u > 0.46f && sx_u < 0.88f && v_arm > 0.28f && v_arm < 0.54f) {
                        hit_part = draw_list[i].shooting && v_arm < 0.35f ? 2 : 3;
                    } else if (draw_list[i].shooting && sx_u > 0.30f && sx_u < 0.95f &&
                               v_arm > 0.34f && v_arm < 0.52f) {
                        hit_part = 2;
                    }
                } else if (vu >= 0.56f) {
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
                int slot_idx = draw_list[i].team_accent;
                if (slot_idx < 0) slot_idx = -slot_idx;
                if (hit_part == 2) {
                    if (draw_list[i].shooting) {
                        cr = 255.0f; cg = 230.0f; cb = 85.0f;
                    } else if (is_self) {
                        cr = 95.0f; cg = 225.0f; cb = 255.0f;
                    } else if (draw_list[i].label == 'O') {
                        if (slot_idx == 2)      { cr = 255.0f; cg = 185.0f; cb = 80.0f; }
                        else if (slot_idx == 3) { cr = 95.0f;  cg = 235.0f; cb = 145.0f; }
                        else if (slot_idx == 4) { cr = 235.0f; cg = 125.0f; cb = 245.0f; }
                        else                    { cr = 95.0f;  cg = 195.0f; cb = 245.0f; }
                    } else {
                        cr = 255.0f; cg = 145.0f; cb = 55.0f;
                    }
                } else if (hit_part == 3) {
                    cr = 105.0f; cg = 120.0f; cb = 145.0f;
                } else {
                    if (is_self) {
                        cr = 165.0f; cg = 185.0f; cb = 212.0f;
                    } else if (draw_list[i].label == 'O') {
                        if (slot_idx == 2)      { cr = 240.0f; cg = 155.0f; cb = 75.0f; }
                        else if (slot_idx == 3) { cr = 85.0f;  cg = 210.0f; cb = 135.0f; }
                        else if (slot_idx == 4) { cr = 215.0f; cg = 110.0f; cb = 235.0f; }
                        else                    { cr = 95.0f;  cg = 195.0f; cb = 245.0f; }
                    } else {
                        cr = 225.0f; cg = 88.0f; cb = 195.0f;
                    }
                }

                char sch = (hit_part == 2) ? '@' : sample_dense_ramp(lum, sx, sy);
                set_cell_if_closer(sx, sy, dist, sch, scale_rgb(cr, cg, cb, lum + 0.12f), bg_black);
            }
        }
    }
}

/* Projecao de efeitos/entidades 3D na mesma grade e camera do raycasting. */
static int project_effect(const Player *p, float x, float y, float height,
                          int *sx, int *sy, float *depth) {
    if (!p || !sx || !sy || !depth) {
        return 0;
    }
    float vx = cam_view_ready ? cam_view_x : p->x;
    float vy = cam_view_ready ? cam_view_y : p->y;
    float angle = cam_view_ready ? cam_view_angle : p->angle;
    float dx = x - vx;
    float dy = y - vy;
    float dist = sqrtf(dx * dx + dy * dy);
    float delta = wrap_angle_signed(atan2f(dy, dx) - angle);
    if (dist < 0.15f || dist > MAX_DEPTH || fabsf(delta) > p->fov * 0.6f) {
        return 0;
    }
    *sx = (int)((0.5f + delta / p->fov) * (float)r_width);
    float bob = cam_third_person ? 0.0f : (sinf(p->bob_phase * 2.0f) * 1.6f * walk_amp * ((float)r_height / 180.0f));
    *sy = (int)((float)r_height * 0.5f + cam_pitch * ((float)r_height / 180.0f) + bob +
                (EYE_FLOOR_H - height) * (float)r_height / dist);
    *depth = dist;
    return (*sx >= 0 && *sx < r_width && dist < z_buffer[*sx]);
}

/* ============================================================================
 * INTERFACE DA BIBLIOTECA GRAFICA (RendererApi) PARA ENTIDADES, PROJETEIS E MAPAS
 * ============================================================================ */

/* Projeta um sprite generico 3D no mundo, aplica fog por distancia e invoca
 * sampler(u, v, ...) para cada celula visivel diante do z_buffer[sx]. */
static void api_draw_world_sprite(const void *player_ptr,
                                  const RenderSpriteSpec *spec,
                                  RendererSpriteSamplerFn sampler,
                                  const void *user_data) {
    const Player *player = (const Player *)player_ptr;
    if (!player || !spec || !sampler) {
        return;
    }

    float vx = cam_view_ready ? cam_view_x : player->x;
    float vy = cam_view_ready ? cam_view_y : player->y;
    float vangle = cam_view_ready ? cam_view_angle : player->angle;

    float dx = spec->world_x - vx;
    float dy = spec->world_y - vy;
    float dist_sq = dx * dx + dy * dy;
    if (dist_sq < 0.0324f || dist_sq >= MAX_DEPTH * MAX_DEPTH) {
        return;
    }
    float dist = sqrtf(dist_sq);

    float angle = wrap_angle_signed(atan2f(dy, dx) - vangle);
    if (fabsf(angle) > player->fov * 0.65f) {
        return;
    }

    float horizon_y = cam_view_ready
        ? frame_horizon_y
        : ((float)r_height * 0.5f + cam_pitch * ((float)r_height / 180.0f) +
           (cam_third_person ? 0.0f : (sinf(player->bob_phase * 2.0f) * 1.6f * walk_amp * ((float)r_height / 180.0f))));

    int center_x = (int)((0.5f + angle / player->fov) * (float)r_width);
    int bot_y = (int)floorf(horizon_y + (EYE_FLOOR_H * (float)r_height) / dist + 0.5f);

    int sprite_h = (int)floorf((0.78f * (float)r_height) / dist * spec->height_scale + 0.5f);
    int min_h = (spec->min_h > 1) ? spec->min_h : 1;
    if (sprite_h < min_h) sprite_h = min_h;
    if (sprite_h > r_height) sprite_h = r_height;

    int sprite_w = (int)floorf((float)sprite_h * spec->width_ratio + 0.5f);
    int min_w = (spec->min_w > 1) ? spec->min_w : 1;
    if (sprite_w < min_w) sprite_w = min_w;
    if (sprite_w > r_width) sprite_w = r_width;

    int top_y = bot_y - sprite_h + 1;
    int left_x = center_x - sprite_w / 2;
    int right_x = left_x + sprite_w - 1;

    int sx_lo = (left_x < 0) ? 0 : left_x;
    int sx_hi = (right_x >= r_width) ? (r_width - 1) : right_x;
    int sy_lo = (top_y < 0) ? 0 : top_y;
    int sy_hi = (bot_y >= r_height) ? (r_height - 1) : bot_y;
    if (sx_lo > sx_hi || sy_lo > sy_hi) {
        return;
    }

    float fog = 1.0f - (dist / 14.5f);
    if (fog < spec->min_fog) fog = spec->min_fog;
    float time_now = cam_view_ready ? frame_time_now : (IsWindowReady() ? (float)GetTime() : 0.0f);
    AsciiColor bg_black = make_color(0, 0, 0);
    const float inv_sw = 1.0f / (float)sprite_w;
    const float inv_sh = 1.0f / (float)sprite_h;
    const float v_step = ((float)(sy_lo - top_y) + 0.5f) * inv_sh;

    for (int sx = sx_lo; sx <= sx_hi; sx++) {
        if (dist >= z_buffer[sx]) {
            continue;
        }
        float u = ((float)(sx - left_x) + 0.5f) * inv_sw;
        float v = v_step;
        for (int sy = sy_lo; sy <= sy_hi; sy++, v += inv_sh) {
            if (dist >= cell_depth[sy][sx]) {
                continue;
            }
            char out_ch = 0;
            AsciiColor out_col = bg_black;
            if (sampler(u, v, sx, sy, sprite_w, sprite_h, dist, fog, time_now,
                        user_data, &out_ch, &out_col) && out_ch) {
                cell_depth[sy][sx] = dist;
                screen_grid[sy][sx] = (AsciiCell){out_ch, out_col, bg_black};
            }
        }
    }
}

static void api_draw_world_orb(const void *player_ptr,
                               float world_x, float world_y, float height,
                               float radius_scale, int min_r, int max_r,
                               char core_ch, AsciiColor core_col,
                               char edge_ch, AsciiColor edge_col,
                               float edge_threshold) {
    int sx, sy;
    float depth;
    if (!project_effect((const Player *)player_ptr, world_x, world_y, height,
                        &sx, &sy, &depth)) {
        return;
    }
    int radius = (int)((float)r_height * radius_scale / depth);
    if (radius < min_r) radius = min_r;
    if (radius > max_r) radius = max_r;

    AsciiColor black = make_color(0, 0, 0);
    float fog = clamp_f01(1.15f - (depth / 16.0f));
    if (fog < 0.45f) fog = 0.45f;

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            float d = (float)(x * x + y * y) / (float)(radius * radius);
            int px = sx + x;
            if (d > 1.0f || px < 0 || px >= r_width || depth >= z_buffer[px]) {
                continue;
            }
            int is_edge = (d > edge_threshold);
            char glyph;
            if (radius >= 5 && is_edge) {
                glyph = sample_dense_ramp((1.10f - d) * fog, px, sy + y);
            } else if (radius >= 5 && d > edge_threshold * 0.45f) {
                glyph = sample_dense_ramp(0.92f * fog, px, sy + y);
            } else {
                glyph = is_edge ? edge_ch : core_ch;
            }
            AsciiColor base = is_edge ? edge_col : core_col;
            set_cell_if_closer(px, sy + y, depth, glyph,
                               scale_rgb((float)base.r, (float)base.g, (float)base.b, fog),
                               black);
        }
    }
}

static void api_draw_world_floor_ring(float cx, float cy, float radius,
                                      char inner_ch, AsciiColor inner_col,
                                      char outer_ch, AsciiColor outer_col) {
    if (!floor_cast_ready || radius <= 0.0f) {
        return;
    }
    const float inv_rh = 0.65f / (float)r_height;
    const float max_edge = 2.0f * (0.07f + MAX_DEPTH * inv_rh);
    const float r_hi_sq = (radius + max_edge) * (radius + max_edge);
    const float r_lo = (radius > max_edge) ? (radius - max_edge) : 0.0f;
    const float r_lo_sq = r_lo * r_lo;
    AsciiColor bg_black = make_color(0, 0, 0);

    for (int y = floor_cast_begin; y < r_height; y++) {
        float bfd = floor_base_dist[y];
        if (bfd > MAX_DEPTH) continue;

        AsciiCell *row = screen_grid[y];
        float *drow = cell_depth[y];
        for (int x = 0; x < r_width; x++) {
            if (y <= floor_col_draw_end[x]) continue;

            float row_dist = bfd * floor_rel_inv_cos[x];
            if (row_dist > MAX_DEPTH || row_dist >= drow[x]) continue;

            float wx = cam_view_x + bfd * floor_col_step_x[x];
            float wy = cam_view_y + bfd * floor_col_step_y[x];
            float dx = wx - cx;
            float dy = wy - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 <= r_hi_sq && d2 >= r_lo_sq) {
                float edge = fabsf(sqrtf(d2) - radius);
                float thickness = 0.07f + row_dist * inv_rh;
                if (edge < thickness) {
                    row[x].ch = inner_ch;
                    row[x].fg = inner_col;
                    row[x].bg = bg_black;
                } else if (edge < thickness * 2.0f) {
                    row[x].ch = outer_ch;
                    row[x].fg = outer_col;
                    row[x].bg = bg_black;
                }
            }
        }
    }
}

static int api_project_point(const void *player_ptr,
                             float world_x, float world_y, float height,
                             int *out_sx, int *out_sy, float *out_depth) {
    return project_effect((const Player *)player_ptr, world_x, world_y, height,
                          out_sx, out_sy, out_depth);
}

static void api_get_grid_size(int *out_w, int *out_h) {
    if (out_w) *out_w = r_width;
    if (out_h) *out_h = r_height;
}

static float api_get_time(void) {
    return cam_view_ready ? frame_time_now : (IsWindowReady() ? (float)GetTime() : 0.0f);
}

static float api_get_view_angle(const void *player_ptr) {
    if (cam_view_ready) return cam_view_angle;
    return player_ptr ? ((const Player *)player_ptr)->angle : 0.0f;
}

/* Expoe a vtable estatica RendererApi com operacoes independentes de Raylib.
 * Chamado em main.c para entregar a biblioteca grafica a entities.c, map.c e player.c. */
const RendererApi *renderer_get_api(void) {
    static const RendererApi API = {
        api_draw_world_sprite,
        api_draw_world_orb,
        api_draw_world_floor_ring,
        api_project_point,
        set_cell,
        set_cell_if_closer,
        api_get_grid_size,
        sample_dense_ramp,
        scale_rgb,
        api_get_time,
        api_get_view_angle
    };
    return &API;
}

void renderer_draw_weapon(const Player *player) {
    if (!player || cam_third_person) {
        return;
    }

    int w_cols = (r_width * 23) / 100;
    int w_rows = (r_height * 27) / 100;
    if (w_cols < 36) w_cols = 36;
    if (w_cols & 1) w_cols++;
    if (w_rows < 22) w_rows = 22;

    int wtype = player->active_weapon;
    if (wtype < 0 || wtype >= PLAYER_MAX_WEAPONS) wtype = 0;
    float wep_cd = player_weapon_cooldown(wtype);
    float shot_elapsed = wep_cd - player->shoot_timer;
    int firing = (player->shoot_timer > 0.0f && shot_elapsed >= 0.0f && shot_elapsed < 0.11f);
    float bob_sx = (float)r_width / 360.0f;
    float bob_sy = (float)r_height / 180.0f;
    int bob_x = (int)roundf(sinf(player->bob_phase) * 7.5f * walk_amp * bob_sx);
    int bob_y = (int)roundf((1.0f - cosf(player->bob_phase * 2.0f)) * 3.8f * walk_amp * bob_sy) -
                (firing ? (int)fmaxf(3.0f, 6.0f * bob_sy) : 0);

    int base_x = (r_width - w_cols) / 2 + bob_x;
    int base_y = r_height - w_rows + bob_y;

    AsciiColor bg_black = make_color(0, 0, 0);

    /* Reticulo tatico da Sniper: linhas de mira fina no centro da tela */
    if (wtype == 2) {
        int cx = r_width / 2;
        int cy = r_height / 2;
        AsciiColor scope_col = make_color(120, 255, 140);
        for (int d = 3; d <= 9; d++) {
            set_cell(cx - d, cy, '-', scope_col, bg_black);
            set_cell(cx + d, cy, '-', scope_col, bg_black);
        }
        for (int d = 2; d <= 5; d++) {
            set_cell(cx, cy - d, '|', scope_col, bg_black);
            set_cell(cx, cy + d, '|', scope_col, bg_black);
        }
        set_cell(cx, cy, '+', make_color(220, 255, 160), bg_black);
    }

    if (firing) {
        int flash_off = (wtype == 1) ? (int)roundf(0.14f * (float)w_cols)
                      : (wtype == 4 ? (int)roundf(0.085f * (float)w_cols) : 0);
        int mcx_c = base_x + (w_cols / 2);
        int mcy = base_y + (wtype == 0 ? (w_rows * 24) / 100 : 2);
        int rad_x = (w_cols * (wtype == 1 ? 26 : (wtype == 2 ? 12 : 16))) / 100;
        int rad_y = (w_rows * (wtype == 1 ? 24 : (wtype == 2 ? 22 : 17))) / 100;
        if (rad_x < 5) rad_x = 5;
        if (rad_y < 4) rad_y = 4;
        int barrels = (wtype == 1) ? 3 : (flash_off > 0 ? 2 : 1);
        for (int b = 0; b < barrels; b++) {
            int mcx = (wtype == 1)
                ? (b == 0 ? mcx_c - flash_off : (b == 1 ? mcx_c : mcx_c + flash_off))
                : (flash_off > 0 ? (b == 0 ? mcx_c - 1 - flash_off : mcx_c + flash_off) : mcx_c);
            for (int dy = -rad_y; dy <= rad_y; dy++) {
                for (int dx = -rad_x; dx <= rad_x; dx++) {
                    float ex = (float)dx / (float)rad_x;
                    float ey = (float)dy / (float)rad_y;
                    float d = sqrtf(ex * ex + ey * ey);
                    float rays = 0.78f + 0.25f * sinf(atan2f(ey, ex) * 7.0f);
                    if (d < rays * 0.92f) {
                        float f_lum = clamp_f01(1.15f - d);
                        char fch = sample_dense_ramp(f_lum, mcx + dx, mcy + dy);
                        AsciiColor fcol = (wtype == 4)
                            ? ((d < 0.42f) ? scale_rgb(200.0f, 255.0f, 255.0f, 1.0f)
                                           : scale_rgb(130.0f, 95.0f, 255.0f, f_lum + 0.2f))
                            : (wtype == 2
                                ? ((d < 0.45f) ? scale_rgb(245.0f, 255.0f, 180.0f, 1.0f)
                                               : scale_rgb(140.0f, 245.0f, 95.0f, f_lum + 0.2f))
                                : ((d < 0.42f) ? scale_rgb(255.0f, 245.0f, 150.0f, 1.0f)
                                               : scale_rgb(255.0f, 130.0f, 38.0f, f_lum + 0.2f)));
                        set_cell(mcx + dx, mcy + dy, fch, fcol, bg_black);
                    }
                }
            }
        }
    }

    enum { MAX_WEP_W = 160, MAX_WEP_H = 128 };
    static AsciiCell wep_cache[MAX_WEP_H][MAX_WEP_W];
    static int wep_c_cols = -1, wep_c_rows = -1, wep_c_firing = -1, wep_c_type = -1;

    if (w_cols <= MAX_WEP_W && w_rows <= MAX_WEP_H) {
        if (wep_c_cols != w_cols || wep_c_rows != w_rows ||
            wep_c_firing != firing || wep_c_type != wtype) {
            for (int r = 0; r < w_rows; r++) {
                float v = ((float)r + 0.5f) / (float)w_rows;
                for (int c = 0; c < w_cols; c++) {
                    float u = ((float)c + 0.5f) / (float)w_cols;
                    float ax = fabsf(u - 0.5f) * 2.0f;
                    int part = 0;
                    float nx = 0.0f;

                    if (wtype == 0) {
                        /* Pistola: cano unico compacto */
                        if (v >= 0.24f && v < 0.62f && ax <= 0.17f) {
                            nx = (u >= 0.5f ? 1.0f : -1.0f) * (ax / 0.17f);
                            part = (v < 0.30f || ax < 0.05f) ? 3 : 1;
                        } else if (v >= 0.62f && ax <= 0.22f + (v - 0.62f) * 0.22f) {
                            nx = (u >= 0.5f ? 1.0f : -1.0f) * (ax / 0.30f);
                            part = (ax > 0.18f || v > 0.86f) ? 2 : 1;
                        }
                    } else if (wtype == 2) {
                        /* Sniper: cano longo e estreito com luneta */
                        if (v >= 0.01f && v < 0.52f && ax <= 0.08f) {
                            nx = (u >= 0.5f ? 1.0f : -1.0f) * (ax / 0.08f);
                            part = (v < 0.06f) ? 3 : 1;
                        } else if (v >= 0.26f && v < 0.50f && ax > 0.08f && ax <= 0.20f) {
                            nx = (u >= 0.5f ? 1.0f : -1.0f) * ((ax - 0.08f) / 0.12f);
                            part = (v > 0.34f && v < 0.42f) ? 4 : 2;
                        } else if (v >= 0.52f && ax <= 0.22f + (v - 0.52f) * 0.25f) {
                            nx = (u >= 0.5f ? 1.0f : -1.0f) * (ax / 0.34f);
                            part = (ax < 0.10f) ? 3 : 1;
                        }
                    } else if (wtype == 3) {
                        /* Metralhadora: chassi largo com tres canos */
                        if (v >= 0.04f && v < 0.42f) {
                            float d0 = fabsf(u - 0.40f);
                            float d1 = fabsf(u - 0.50f);
                            float d2 = fabsf(u - 0.60f);
                            float rad = 0.045f;
                            if (d0 <= rad || d1 <= rad || d2 <= rad) {
                                part = (v < 0.10f || (r % 5 == 0)) ? 3 : 1;
                                nx = (d1 <= rad) ? (u - 0.50f) / rad : 0.4f;
                            }
                        } else if (v >= 0.42f && ax <= 0.34f + (v - 0.42f) * 0.34f) {
                            nx = (u >= 0.5f ? 1.0f : -1.0f) * (ax / 0.52f);
                            part = (ax < 0.14f && v < 0.62f) ? 4 : ((ax > 0.32f) ? 2 : 1);
                        }
                    } else {
                        /* Shotgun (cano duplo) e Arma Futurista (canos paralelos + nucleo) */
                        if (v >= 0.02f && v < 0.36f) {
                            float d_left = fabsf(u - 0.415f);
                            float d_right = fabsf(u - 0.585f);
                            float b_rad = 0.064f + v * 0.03f;
                            if (d_left <= b_rad || d_right <= b_rad) {
                                float d_min = (d_left < d_right) ? (u - 0.415f) : (u - 0.585f);
                                nx = clamp_f01(fabsf(d_min) / b_rad) * (d_min >= 0.0f ? 1.0f : -1.0f);
                                part = (v < 0.07f || (v > 0.14f && v < 0.18f)) ? 3 : 1;
                            } else if (fabsf(u - 0.50f) < 0.026f && v >= 0.11f) {
                                part = (v < 0.16f || wtype == 4) ? 4 : 2;
                                nx = (u - 0.50f) / 0.026f;
                            }
                        } else if (v >= 0.36f && v < 0.62f) {
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
                            float hw = 0.30f + (v - 0.62f) * 0.42f;
                            if (hw > 0.58f) hw = 0.58f;
                            if (ax <= hw) {
                                nx = (u >= 0.50f ? 1.0f : -1.0f) * (ax / hw);
                                if (v < 0.70f && ax > 0.18f && ax < 0.29f) {
                                    part = 3;
                                } else if (v >= 0.68f && v <= 0.77f && ax < 0.20f) {
                                    part = 4;
                                } else if (ax > hw * 0.84f || v > 0.88f) {
                                    part = 2;
                                } else {
                                    part = 1;
                                }
                            }
                        }
                    }

                    if (part == 0) {
                        wep_cache[r][c].ch = '\0';
                        continue;
                    }

                    float cyl = sqrtf(fmaxf(0.14f, 1.0f - nx * nx * 0.85f));
                    char wch;
                    AsciiColor wcol;
                    float br = 142.0f, bg = 156.0f, bb = 180.0f;
                    float gr = 95.0f,  gg = 225.0f, gb = 255.0f;
                    if (wtype == 1) {
                        br = 185.0f; bg = 128.0f; bb = 88.0f;
                        gr = 255.0f; gg = 165.0f; gb = 55.0f;
                    } else if (wtype == 2) {
                        br = 155.0f; bg = 168.0f; bb = 145.0f;
                        gr = 185.0f; gg = 255.0f; gb = 95.0f;
                    } else if (wtype == 3) {
                        br = 118.0f; bg = 142.0f; bb = 108.0f;
                        gr = 255.0f; gg = 215.0f; gb = 75.0f;
                    } else if (wtype == 4) {
                        br = 115.0f; bg = 148.0f; bb = 215.0f;
                        gr = 110.0f; gg = 245.0f; gb = 255.0f;
                    }
                    if (part == 4) {
                        float glow = firing ? 1.25f : 0.96f;
                        wch = '@';
                        wcol = firing ? scale_rgb(255.0f, 195.0f, 65.0f, glow)
                                      : scale_rgb(gr, gg, gb, glow);
                    } else if (part == 2) {
                        wch = (r % 2 == 0) ? '=' : '+';
                        wcol = scale_rgb(br * 0.55f, bg * 0.55f, bb * 0.58f, 0.82f);
                    } else if (part == 3) {
                        float lum = 0.74f + 0.26f * cyl;
                        wch = sample_dense_ramp(lum, c, r);
                        wcol = scale_rgb(fminf(255.0f, br * 1.45f), fminf(255.0f, bg * 1.45f), fminf(255.0f, bb * 1.38f), lum);
                    } else {
                        float lum = 0.36f + 0.56f * cyl;
                        wch = sample_dense_ramp(lum, c, r);
                        wcol = scale_rgb(br, bg, bb, lum + 0.16f);
                    }
                    wep_cache[r][c] = (AsciiCell){wch, wcol, bg_black};
                }
            }
            wep_c_cols = w_cols;
            wep_c_rows = w_rows;
            wep_c_firing = firing;
            wep_c_type = wtype;
        }

        int r_lo = (base_y < 0) ? -base_y : 0;
        int r_hi = (base_y + w_rows > r_height) ? (r_height - base_y) : w_rows;
        int c_lo = (base_x < 0) ? -base_x : 0;
        int c_hi = (base_x + w_cols > r_width) ? (r_width - base_x) : w_cols;
        for (int r = r_lo; r < r_hi; r++) {
            AsciiCell *dst = &screen_grid[base_y + r][base_x];
            const AsciiCell *src = wep_cache[r];
            for (int c = c_lo; c < c_hi; c++) {
                if (src[c].ch != '\0') {
                    dst[c] = src[c];
                }
            }
        }
        return;
    }

    for (int r = 0; r < w_rows; r++) {
        float v = ((float)r + 0.5f) / (float)w_rows;
        for (int c = 0; c < w_cols; c++) {
            float u = ((float)c + 0.5f) / (float)w_cols;
            float ax = fabsf(u - 0.5f) * 2.0f;

            int part = 0;
            float nx = 0.0f;

            if (v >= 0.02f && v < 0.36f) {
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
                float hw = 0.30f + (v - 0.62f) * 0.42f;
                if (hw > 0.58f) hw = 0.58f;
                if (ax <= hw) {
                    nx = (u >= 0.50f ? 1.0f : -1.0f) * (ax / hw);
                    if (v < 0.70f && ax > 0.18f && ax < 0.29f) {
                        part = 3;
                    } else if (v >= 0.68f && v <= 0.77f && ax < 0.20f) {
                        part = 4;
                    } else if (ax > hw * 0.84f || v > 0.88f) {
                        part = 2;
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
            int is_w = map_is_visible_wall(map, x, y);
            char gch = '=';
            AsciiColor col = mm_floor;
            if (is_w) {
                gch = tile;
                col = get_tile_base_color(tile);
                if (g_wall_hook) {
                    float dummy_dist = 1.0f;
                    int dummy_edge = 0, dummy_side = 0;
                    g_wall_hook(map, x, y, tile, &col, &dummy_dist, &dummy_edge, &dummy_side);
                }
            } else if (g_minimap_hook) {
                (void)g_minimap_hook(map, x, y, &gch, &col);
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
                char mch = 'E';
                AsciiColor mcol = make_color(255, 95, 225);
                if (sprites[i].label == 'O') {
                    int slot_n = sprites[i].team_accent;
                    int is_pvp = (slot_n < 0);
                    if (slot_n < 0) slot_n = -slot_n;
                    mch = (slot_n >= 1 && slot_n <= 9) ? (char)('0' + slot_n) : '@';
                    mcol = is_pvp ? make_color(255, 115, 75) : make_color(85, 225, 255);
                }
                for (int mmy = 0; mmy < scale; mmy++) {
                    for (int mmx = 0; mmx < scale; mmx++) {
                        set_cell(ox + 1 + sx * scale + mmx, oy + 1 + sy * scale + mmy,
                                 mch, mcol, bg_black);
                    }
                }
            }
        }
    }

    int look_x = (int)(player->x + cosf(player->angle) * 1.15f);
    int look_y = (int)(player->y + sinf(player->angle) * 1.15f);
    if (look_x >= 0 && look_x < map->width && look_y >= 0 && look_y < map->height &&
        !map_is_visible_wall(map, look_x, look_y)) {
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

/* Desenha o rosto reativo do HUD via textura GPU dedicada (so reenvia quando o estado muda) */
static void draw_hud_mugshot(int bx, int by, int bw, int bh, const Player *player) {
    static AsciiCell mug_buf[GPU_MUG_SIZE * GPU_MUG_SIZE];
    static int cache_valid = 0;
    static int c_custom = -1, c_vid = -1, c_style = -1;
    static CharacterAppearance c_app = {0};
    static int c_hp_tier = -1, c_firing = -1, c_turn_dir = 99, c_blink = -1;
    static int c_mw = 0, c_mh = 0;

    const AsciiColor bg_zero = {0, 0, 0};
    int solid = (current_visual_style == ASCII_STYLE_SOLID);

    if (player && player_appearance_visual(player->visual_id, &player->appearance).custom_sprite) {
        DrawRectangle(bx, by, bw, bh, (Color){10, 12, 17, 255});
        DrawRectangleLines(bx, by, bw, bh, (Color){235, 55, 100, 255});

        int dirty = !cache_valid || c_custom != 1 || c_vid != player->visual_id ||
                    c_style != current_visual_style ||
                    memcmp(&c_app, &player->appearance, sizeof(c_app)) != 0;

        if (dirty) {
            CharacterVisual visual = player_appearance_visual(player->visual_id, &player->appearance);
            int rows = visual.portrait_rows;
            int cols = visual.portrait_cols;
            if (rows < 1) rows = 1;
            if (cols < 1) cols = 1;
            if (rows > GPU_MUG_SIZE) rows = GPU_MUG_SIZE;
            if (cols > GPU_MUG_SIZE) cols = GPU_MUG_SIZE;
            float crop_x = visual.portrait_crop_x;
            float crop_y = visual.portrait_crop_y;

            for (int r = 0; r < rows; r++) {
                float vy = ((float)r + 0.5f) / (float)rows * crop_y;
                for (int c = 0; c < cols; c++) {
                    int idx = r * cols + c;
                    int mat = player_render_material(player,
                        (((float)c + 0.5f) / (float)cols * 2.0f - 1.0f) * crop_x,
                        vy, 0);
                    if (mat) {
                        CharacterColor mc = player_get_material_color(mat);
                        mug_buf[idx].ch = (mat == 3) ? '@' : '#';
                        mug_buf[idx].fg = make_color(mc.r, mc.g, mc.b);
                        mug_buf[idx].bg = bg_zero;
                    } else {
                        mug_buf[idx].ch = ' ';
                        mug_buf[idx].fg = bg_zero;
                        mug_buf[idx].bg = bg_zero;
                    }
                }
            }
            c_custom = 1;
            c_vid = player->visual_id;
            c_app = player->appearance;
            c_style = current_visual_style;
            c_mw = cols;
            c_mh = rows;
            cache_valid = 1;
            if (gpu_ready) {
                pack_ascii_to_u32(mug_buf, c_mw, c_mh, c_mw, solid, gpu_upload_buf);
                UpdateTextureRec(gpu_mug_tex,
                                 (Rectangle){0.0f, 0.0f, (float)c_mw, (float)c_mh},
                                 gpu_upload_buf);
            }
        }

        if (gpu_ready) {
            draw_gpu_grid_tex(gpu_mug_tex, GPU_MUG_SIZE, GPU_MUG_SIZE, c_mw, c_mh, solid,
                              (float)(bx + 3), (float)(by + 3),
                              (float)(bw - 6), (float)(bh - 6));
        } else {
            renderer_draw_ascii_buffer(mug_buf, c_mw, c_mh, c_mw,
                                       (float)(bx + 3), (float)(by + 3),
                                       (float)(bw - 6), (float)(bh - 6));
        }
        return;
    }

    int hp = player ? player->health : 100;
    int firing = (player && player->shoot_timer > 0.12f);
    double t = GetTime();

    float look_wave = sinf((float)t * 1.05f);
    int turn_dir = 0;
    if (!firing) {
        if (look_wave > 0.60f) turn_dir = 1;
        else if (look_wave < -0.60f) turn_dir = -1;
    }
    float head_turn = (float)turn_dir * 0.065f;
    int is_blinking = (!firing && fmodf((float)t, 4.2f) > 4.06f);
    int hp_tier = (hp <= 30) ? 0 : (hp <= 35) ? 1 : (hp <= 50) ? 2 : (hp <= 65) ? 3 : 4;

    Color frame_col = (hp <= 30) ? (Color){225, 60, 60, 255}
                    : firing      ? (Color){255, 195, 70, 255}
                                  : (Color){95, 112, 138, 255};
    DrawRectangle(bx, by, bw, bh, (Color){10, 12, 17, 255});
    DrawRectangleLines(bx, by, bw, bh, frame_col);

    int mh = (bh - 6) / 4;
    if (mh < 12) mh = 12;
    if (mh > 20) mh = 20;
    int mw = (mh * 6) / 5;
    if (mw > GPU_MUG_SIZE) mw = GPU_MUG_SIZE;

    int dirty = !cache_valid || c_custom != 0 || c_hp_tier != hp_tier ||
                c_firing != firing || c_turn_dir != turn_dir ||
                c_blink != is_blinking || c_mw != mw || c_mh != mh ||
                c_style != current_visual_style;

    if (dirty) {
        float muzzle_flash = firing ? 0.35f : 0.0f;

        for (int r = 0; r < mh; r++) {
            float fy = (((float)r + 0.5f) / (float)mh) * 2.0f - 1.0f;
            for (int c = 0; c < mw; c++) {
                int idx = r * mw + c;
                float fx = (((float)c + 0.5f) / (float)mw) * 2.0f - 1.0f;
                float ax = fabsf(fx);
                float fx_t = fx - head_turn;
                float ax_t = fabsf(fx_t);

                float rad = sqrtf(fx * fx * 1.10f + fy * fy * 0.92f);
                int in_ear = (ax >= 0.72f && ax <= 0.96f && fy > -0.35f && fy < 0.38f);
                int in_neck = (ax < 0.42f && fy >= 0.66f);
                if (rad > 0.90f && !in_ear && !in_neck) {
                    mug_buf[idx].ch = ' ';
                    mug_buf[idx].fg = bg_zero;
                    mug_buf[idx].bg = bg_zero;
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
                    cr = 58.0f; cg = 68.0f; cb = 78.0f;
                    glyph = '-';
                } else if (fy >= 0.68f) {
                    cr = 85.0f; cg = 96.0f; cb = 112.0f;
                    glyph = sample_dense_ramp(light, c, r);
                } else {
                    cr = 228.0f + muzzle_flash * 22.0f;
                    cg = 174.0f + muzzle_flash * 18.0f;
                    cb = 138.0f - (hp <= 50 ? 20.0f : 0.0f);

                    int in_brow = (fy < -0.12f && ax_t < 0.48f);

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

                    float mouth_y = firing ? 0.39f : 0.41f;
                    float mouth_h = firing ? 0.12f : (hp <= 35 ? 0.070f : 0.052f);
                    int in_mouth = (ax_t < (firing ? 0.26f : 0.22f)) &&
                                   (fabsf(fy - mouth_y) < mouth_h);

                    int in_nose = (ax_t < 0.075f && fy > 0.01f && fy < 0.24f);

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

                mug_buf[idx].ch = glyph;
                mug_buf[idx].fg = scale_rgb(cr, cg, cb, light + 0.14f);
                mug_buf[idx].bg = bg_zero;
            }
        }
        c_custom = 0;
        c_hp_tier = hp_tier;
        c_firing = firing;
        c_turn_dir = turn_dir;
        c_blink = is_blinking;
        c_mw = mw;
        c_mh = mh;
        c_style = current_visual_style;
        cache_valid = 1;
        if (gpu_ready) {
            pack_ascii_to_u32(mug_buf, mw, mh, mw, solid, gpu_upload_buf);
            UpdateTextureRec(gpu_mug_tex,
                             (Rectangle){0.0f, 0.0f, (float)mw, (float)mh},
                             gpu_upload_buf);
        }
    }

    if (gpu_ready) {
        draw_gpu_grid_tex(gpu_mug_tex, GPU_MUG_SIZE, GPU_MUG_SIZE, mw, mh, solid,
                          (float)(bx + 3), (float)(by + 3),
                          (float)(bw - 6), (float)(bh - 6));
    } else {
        renderer_draw_ascii_buffer(mug_buf, mw, mh, mw,
                                   (float)(bx + 3), (float)(by + 3),
                                   (float)(bw - 6), (float)(bh - 6));
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

/* Envia o screen_grid para a GPU em 1 draw call (renderer_draw_ascii_buffer) e
 * sobrepoe mira vetorial, paineis inferiores do HUD, mugshot reativo e status de habilidade. */
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

    float cell_h = (float)view_h / (float)r_height;
    float glyph_size = cell_h * 1.85f;
    if (glyph_size < 4.0f) {
        glyph_size = 4.0f;
    }

    ClearBackground((Color){5, 7, 11, 255});

    /* Viewport 3D 100% em ASCII puro renderizado em 1 draw call via GPU Shader */
    renderer_draw_ascii_buffer(&screen_grid[0][0], r_width, r_height, MAX_R_WIDTH,
                               0.0f, 0.0f, (float)sw, (float)view_h);

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

        draw_hud_mugshot(face_x, face_y, face_w, face_h, player);

        PlayerWeaponHudInfo whud;
        player_get_weapon_hud_info(player, &whud);
        Color ammo_col = (player->ammo > 5) ? (Color){252, 208, 72, 255}
                                            : (Color){245, 72, 72, 255};
        const char *am_val = whud.custom ? whud.value : TextFormat("%02d/30", player->ammo);
        int am_lf = fit_hud_font("MUN", lbl_font, (s4_w - 20) / 2);
        const char *am_lbl = whud.custom ? whud.label :
            (MeasureText("MUNICAO", am_lf) + MeasureText(am_val, val_font) + 28 <= s4_w) ? "MUNICAO" : "MUN";
        int am_vf = fit_hud_font(am_val, val_font, s4_w - MeasureText(am_lbl, am_lf) - 24);
        DrawText(am_lbl, s4_x + 10, card_y + 8, am_lf, lbl_col);
        DrawText(am_val, s4_x + s4_w - MeasureText(am_val, am_vf) - 10, card_y + 6, am_vf, ammo_col);
        draw_hud_ascii_meter(s4_x + 10, bar_y, s4_w - 20, bar_h,
                             whud.custom ? whud.bar_ratio : (float)player->ammo / 30.0f, ammo_col);

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
