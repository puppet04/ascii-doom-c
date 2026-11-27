/* player.c - classes, aparencia ASCII, movimentacao, armas e habilidades do jogador */

#include "player.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Paleta compartilhada de cores (indice de material -> RGBA) usada pelos sprites do jogador */
CharacterColor player_get_material_color(int mat) {
    static const CharacterColor palette[] = {
        {0, 0, 0, 0}, {100, 105, 125, 255}, {255, 226, 213, 255},
        {203, 48, 79, 255}, {55, 48, 65, 255}, {112, 136, 166, 255},
        {105, 28, 52, 255}, {255, 247, 239, 255},
        {244, 139, 155, 255}, {255, 166, 178, 255},
        {48, 46, 60, 255}, {72, 76, 90, 255}, {17, 16, 24, 255},
        {74, 29, 45, 255}, {42, 21, 32, 255}, {153, 133, 113, 255},
        {109, 112, 125, 255}, {227, 51, 76, 255},
        {222, 70, 100, 255}, {147, 151, 174, 255}, {232, 185, 175, 255},
        {57, 64, 76, 255}, {98, 109, 124, 255}, {177, 184, 192, 255},
        {216, 170, 137, 255}, {143, 93, 77, 255},
        {236, 195, 98, 255}, {65, 126, 200, 255}, {65, 142, 102, 255},
        {151, 88, 183, 255}, {172, 115, 79, 255}, {100, 64, 49, 255},
        {72, 82, 46, 255}, {111, 119, 65, 255}
    };
    if (mat < 0 || mat >= (int)(sizeof(palette) / sizeof(palette[0]))) return palette[0];
    return palette[mat];
}

/* Retorna os parametros visuais (resolucao da grade, escala e descricao) de um preset */
CharacterVisual player_get_visual(int visual_id) {
    static const CharacterVisual visuals[PRESET_CLASS_COUNT] = {
        {0, 18, 24, 1.0f, 0.49f, "Fuzileiro // Pronto para o combate"},
        {1, 28, 36, 0.66f, 0.35f, "Kasane Teto // Voz e energia na arena!"},
        {1, 18, 24, 1.0f, 0.49f, "Arauto das Sombras // Manto e misterio."},
        {1, 18, 24, 1.0f, 0.49f, "Guts // O Espadachim Negro"},
        {1, 24, 30, 0.65f, 0.33f, "Rambo // Um exercito de um homem so"}
    };
    if (visual_id < 0 || visual_id >= PRESET_CLASS_COUNT) visual_id = PRESET_FUZILEIRO;
    return visuals[visual_id];
}

/* Testa se o ponto (x, y) cai dentro de uma elipse centrada em (cx, cy) */
static int inside_oval(float x, float y, float cx, float cy, float rx, float ry) {
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    return dx * dx + dy * dy < 1.0f;
}

/* Catalogo das pecas: o menu so enumera estes dados. */
static const char *PART_LABELS[CHARACTER_PART_COUNT] = {
    "Cabeca", "Corpo", "Cabelo", "Roupa", "Cor do cabelo", "Cor da roupa", "Tom de pele"
};
static const int PART_COUNTS[CHARACTER_PART_COUNT] = {3, 3, 5, 4, 6, 6, 4};
static const char *PART_OPTIONS[CHARACTER_PART_COUNT][6] = {
    {"Oval", "Angular", "Chibi"},
    {"Esbelto", "Medio", "Forte"},
    {"Sem cabelo", "Curto", "Espetado", "Longo", "Cachos duplos"},
    {"Casaco", "Armadura", "Vestido", "Tunica"},
    {"Preto", "Castanho", "Loiro", "Vermelho", "Azul", "Branco"},
    {"Cinza", "Preto", "Vermelho", "Azul", "Verde", "Roxo"},
    {"Clara", "Media", "Morena", "Escura"}
};

/* Inicia uma aparencia customizada com valores padrao validos */
void player_appearance_init(CharacterAppearance *a) {
    if (!a) return;
    memset(a, 0, sizeof(*a));
    a->enabled = 1;
    a->choice[PART_BODY] = 1;
    a->choice[PART_HAIR] = 1;
}

/* Quantidade de opcoes disponiveis para uma peca de customizacao */
int player_part_count(int part) {
    return part >= 0 && part < CHARACTER_PART_COUNT ? PART_COUNTS[part] : 0;
}

/* Nome legivel da categoria de peca para exibir no criador de personagem */
const char *player_part_label(int part) {
    return player_part_count(part) ? PART_LABELS[part] : "";
}

/* Nome da opcao selecionada dentro de uma categoria */
const char *player_part_option(int part, int choice) {
    return choice >= 0 && choice < player_part_count(part) ? PART_OPTIONS[part][choice] : "";
}

/* Confere se todas as escolhas de aparencia estao dentro dos limites do catalogo */
int player_appearance_valid(const CharacterAppearance *a) {
    if (!a) return 0;
    if (!a->enabled) return 1;
    if (a->enabled != 1) return 0;
    for (int i = 0; i < CHARACTER_PART_COUNT; i++)
        if (a->choice[i] < 0 || a->choice[i] >= PART_COUNTS[i]) return 0;
    return 1;
}

/* Usa sprite customizado se habilitado e valido; caso contrario usa o preset */
CharacterVisual player_appearance_visual(int visual_id, const CharacterAppearance *a) {
    if (a && a->enabled && player_appearance_valid(a))
        return (CharacterVisual){1, 28, 36, 0.75f, 0.35f, "Personagem personalizado"};
    return player_get_visual(visual_id);
}

/* Amostra o indice de material na coordenada normalizada (x, y) do sprite customizado */
int player_appearance_material(int visual_id, const CharacterAppearance *a, float x, float y, int back) {
    if (!a || !a->enabled || !player_appearance_valid(a))
        return player_character_material(visual_id, x, y, back);
    if (fabsf(x) > 1 || y < 0 || y > 1) return 0;
    static const int hair_colors[] = {12, 25, 26, 3, 27, 7};
    static const int cloth_colors[] = {19, 10, 13, 27, 28, 29};
    static const int skin_colors[] = {2, 24, 30, 31};
    int hair = hair_colors[a->choice[PART_HAIR_COLOR]];
    int cloth = cloth_colors[a->choice[PART_OUTFIT_COLOR]];
    int skin = skin_colors[a->choice[PART_SKIN]];
    int outfit = a->choice[PART_OUTFIT], style = a->choice[PART_HAIR];
    int head = a->choice[PART_HEAD];
    float torso = a->choice[PART_BODY] == 0 ? 0.23f : a->choice[PART_BODY] == 1 ? 0.29f : 0.37f;
    float ax = fabsf(x);
    int mat = 0;
    /* Cabelo longo e cachos ficam atras dos ombros. */
    if (style == 3 && y > 0.12f && y < 0.53f && ax < 0.37f - (y - 0.12f) * 0.12f)
        mat = ax > 0.29f || sinf(x * 65) < -0.6f ? 4 : hair;
    if (style == 4 && y > 0.10f && y < 0.45f) {
        float t = (y - 0.10f) / 0.35f;
        float phase = t * 20;
        float center = 0.43f + t * 0.08f + 0.02f * sinf(phase);
        float width = 0.16f * (1-t) + 0.02f;
        if (fabsf(ax - center) < width) mat = sinf(phase + ax * 8) < -0.6f ? 4 : hair;
    }
    /* Corpo muda a largura do tronco, pernas, bracos e roupas. */
    float leg = torso * 0.52f, leg_w = torso * 0.28f;
    if (y > 0.61f && y < 0.97f && fabsf(ax - leg) < leg_w)
        mat = y > 0.82f ? 4 : (outfit == 2 ? skin : cloth);
    if (y > 0.95f && y < 0.99f && ax > leg - leg_w && ax < leg + leg_w + 0.04f) mat = 4;
    float waist = torso * (1 - 0.55f * (y - 0.31f));
    if (y > 0.31f && y < 0.64f && ax < waist) {
        mat = cloth;
        if (ax > waist - 0.017f) mat = 4;
        if (!back && outfit == 0 && ax < 0.015f) mat = 4;
        if (!back && outfit == 0 && ax < 0.025f && fmodf(y * 35, 1) < 0.18f) mat = 15;
        if (outfit == 1) {
            if (fmodf((y + ax * 0.12f) * 32, 1) < 0.16f) mat = 22;
            if (ax < 0.025f) mat = 21;
        }
        if (y > 0.565f && y < 0.59f) mat = 4;
    }
    if ((outfit == 2 || outfit == 3) && y > 0.54f) {
        float bottom = outfit == 2 ? 0.75f : 0.94f;
        float width = torso * 0.8f + (y - 0.54f) * 0.48f;
        if (y < bottom - 0.015f * cosf(x * 30) && ax < width) {
            mat = cosf(x / width * 22) < -0.7f ? 4 : cloth;
            if (y > bottom - 0.028f) mat = 15;
        }
    }
    float arm = torso + 0.075f + (y - 0.35f) * 0.06f;
    if (y > 0.34f && y < 0.66f && fabsf(ax - arm) < 0.05f + torso * 0.035f) {
        mat = y > 0.61f ? skin : cloth;
        if (outfit == 1 && y < 0.61f && fmodf(y * 35, 1) < 0.17f) mat = 22;
        if (outfit == 2 && y < 0.43f) mat = skin;
    }
    if (outfit == 1 && y > 0.31f && y < 0.39f && ax > torso - 0.03f && ax < torso + 0.14f)
        mat = y < 0.325f ? 22 : cloth;
    if (y > 0.26f && y < 0.33f && ax < 0.085f) mat = skin;
    /* Tres formatos de rosto, com olhos e boca apenas na frente. */
    float hw = head == 2 ? 0.34f : head == 1 ? 0.235f : 0.27f;
    float cy = head == 2 ? 0.18f : 0.175f, hh = head == 2 ? 0.145f : 0.125f;
    int in_head = head == 1 ? (y > 0.055f && y < 0.29f && ax < hw - fmaxf(0, y - 0.21f) * 0.75f) :
                              inside_oval(x,y,0,cy,hw,hh);
    if (in_head) {
        mat = skin;
        if (!back) {
            float eye_x = hw * 0.46f;
            float eye_h = head == 2 ? 0.033f : 0.014f;
            if (inside_oval(ax,y,eye_x,0.20f,hw*0.25f,eye_h)) mat = 4;
            if (inside_oval(ax,y,eye_x,0.203f,hw*0.20f,eye_h*0.74f)) mat = 7;
            if (fabsf(ax-eye_x)<0.021f && fabsf(y-0.203f)<eye_h*0.72f) mat = 6;
            if (head == 2 && inside_oval(ax,y,eye_x-0.010f,0.193f,0.009f,0.008f)) mat = 7;
            if (ax < hw * 0.20f && fabsf(y-(0.263f-ax*0.10f))<0.004f) mat=6;
        }
        if (style && (back || y < 0.13f + 0.035f * sinf(x * 17))) mat = hair;
        if (style && ax > hw * 0.87f && y < 0.23f) mat = hair;
    }
    if (style == 2 && ax < hw + 0.02f && y > 0.012f + 0.032f * fabsf(sinf(x * 47)) && y < 0.11f) mat=hair;
    if ((style == 1 || style == 3 || style == 4) && inside_oval(x,y,0,0.12f,hw+0.02f,hh*0.80f) && y < 0.105f)
        mat = hair;
    return mat;
}

/* --- Sprites procedurais dos personagens preset --- */

/* Desenha a silhueta e detalhes da Kasane Teto (frente e costas) */
int player_teto_material(float x, float y, int back_view) {
    if (y < 0.0f || y > 1.0f || fabsf(x) > 1.0f) return 0;
    int mat = 0;
    /* Leve inclinacao da pose: pernas proximas e ombros relaxados. */
    float body_x = x - 0.025f * sinf(y * 5.0f);
    float ax = fabsf(body_x);
    for (int side = -1; side <= 1; side += 2) {
        float leg = side * (0.125f + (y - 0.74f) * 0.10f);
        if (y > 0.70f && y < 0.96f && fabsf(body_x - leg) < 0.065f) {
            mat = y < 0.81f ? 2 : 4;
            if (y > 0.815f && fabsf(body_x - leg + 0.025f) < 0.015f) mat = 1;
            if (y > 0.805f && y < 0.819f) mat = 3;
        }
        if (inside_oval(body_x, y, leg + side * 0.017f, 0.963f, 0.092f, 0.023f)) mat = 4;
    }
    /* Saia arredondada, pregas que acompanham o leque e debrum rosa. */
    float skirt_t = (y - 0.565f) / 0.16f;
    float skirt_w = 0.19f + 0.145f * sinf(fmaxf(0, skirt_t) * 1.4f);
    float hem = 0.72f - 0.045f * (ax / 0.34f) * (ax / 0.34f);
    if (y > 0.565f && y < hem && ax < skirt_w) {
        float pleat = cosf(body_x / skirt_w * 22.0f);
        mat = pleat > 0.35f ? 19 : (pleat < -0.65f ? 4 : 1);
        if (y > hem - 0.012f) mat = 3;
    }
    /* Colete longo cinza com vivos vermelhos e botoes pretos da referencia. */
    if (y > 0.35f && y < 0.65f - ax * 0.12f) {
        float width = 0.235f - 0.065f * sinf((y - 0.35f) / 0.235f * 2.2f);
        if (ax < width) {
            mat = body_x < -0.05f ? 19 : 1;
            if (ax > width - 0.016f || y > 0.634f - ax * 0.12f) mat = 3;
            if (!back_view && (fabsf(ax - 0.115f) < 0.010f || ax < 0.012f)) mat = 3;
            if (!back_view && ax > 0.065f && ax < 0.17f && fabsf(y - 0.445f) < 0.006f) mat = 3;
            if (!back_view && ax < 0.021f &&
                (fabsf(y - 0.435f) < 0.009f || fabsf(y - 0.495f) < 0.009f ||
                 fabsf(y - 0.555f) < 0.009f || fabsf(y - 0.615f) < 0.009f)) mat = 4;
        }
    }
    /* Mangas destacadas, punhos e maos pequenas com contorno arredondado. */
    for (int side = -1; side <= 1; side += 2) {
        float arm_x = side * (0.27f + 0.055f * sinf((y - 0.36f) * 9.0f));
        float sleeve_width = y > 0.41f ? 0.05f + (y - 0.41f) * 0.22f : 0.047f;
        if (y > 0.36f && y < 0.61f && fabsf(body_x - arm_x) < sleeve_width) {
            mat = y < 0.41f ? 2 : 4;
            if (y > 0.43f && body_x < arm_x && fabsf(body_x - arm_x) < 0.03f) mat = 1;
            if ((y > 0.409f && y < 0.423f) || y > 0.592f) mat = 3;
        }
        if (inside_oval(body_x, y, side * 0.308f, 0.622f, 0.045f, 0.027f)) mat = 2;
    }
    if (y > 0.305f && y < 0.367f && ax < 0.065f) mat = y < 0.345f ? 20 : 2;
    if (!back_view) {
        if (y > 0.356f && y < 0.405f && ax < (0.413f - y) * 2.3f)
            mat = ax > (0.413f - y) * 2.3f - 0.018f ? 3 : 4;
    }
    /* Cachos em fitas curvas: espirais com volume, luz e pontas finas. */
    for (int side = -1; side <= 1; side += 2) {
        float yy = y + (side == 1 ? 0.012f : 0);
        float t = (yy - 0.10f) / 0.34f;
        if (t > 0 && t < 1) {
            float phase = t * 3.5f * 2 * PI_F;
            float center = side * (0.47f + 0.035f * t + 0.025f * sinf(phase));
            float width = (0.19f * (1-t) + 0.023f) * (0.86f + 0.14f * cosf(phase));
            float u = (x - center) / width;
            if (fabsf(u) < 1) {
                mat = u < -0.6f || sinf(phase + u * 1.5f) < -0.55f ? 6 : 3;
                if (u > -0.5f && u < 0.1f) mat = 18;
                if (sinf(phase + u) > 0.70f && fabsf(u) < 0.7f) mat = 9;
            }
        }
        if (inside_oval(x,y,side*0.355f,0.145f,0.048f,0.029f)) mat=4;
        if (inside_oval(x,y,side*0.355f,0.141f,0.018f,0.023f)) mat=9;
    }
    /* Teto inclina a cabeca levemente; cabelo com luz curva, nunca em faixa reta. */
    float fx = (x + 0.035f) / 1.12f;
    float fy = (y + x * 0.028f) / 1.06f;
    float fax = fabsf(fx);
    if (inside_oval(fx,fy,0,0.179f,0.33f,0.153f)) {
        mat = fx > 0.20f ? 6 : 3;
        if (inside_oval(fx,fy,-0.055f,0.091f,0.23f,0.038f)) mat=18;
        if (inside_oval(fx,fy,-0.085f,0.081f,0.16f,0.014f)) mat=9;
        /* Face oval, queixo suave e sombra nas laterais. */
        if (!back_view && inside_oval(fx,fy,0,0.211f,0.259f,0.112f)) {
            mat = fax > 0.235f || fy > 0.309f ? 20 : 2;
            for (int side=-1; side<=1; side+=2) {
                float ex=side*0.129f;
                float ey=0.218f;
                float dx=(fx-ex)/0.083f, dy=(fy-ey)/0.044f;
                /* Olhos chibi rubi com reflexos de quatro pontas. */
                if (dx*dx+dy*dy < 1.14f) mat=6;
                if (dx*dx+dy*dy < 0.86f && dy > -0.60f) mat=7;
                if (inside_oval(fx,fy,ex-0.010f,ey+0.003f,0.052f,0.039f)) {
                    mat=fy > ey+0.005f ? 18 : 6;
                    if (fabsf(fx-ex+0.010f)<0.015f && fy<ey+0.015f) mat=4;
                    float star_x=fabsf(fx-ex+0.010f)/0.043f;
                    float star_y=fabsf(fy-ey)/0.033f;
                    if (sqrtf(star_x)+sqrtf(star_y)<1.04f) mat=7;
                }
                if (fabsf(fx-(ex+side*0.079f))<0.020f && fabsf(fy-0.198f)<0.004f) mat=6;
                if (inside_oval(fx,fy,side*0.183f,0.265f,0.048f,0.010f)) mat=8;
                if (fabsf(fx-ex)<0.052f && fabsf(fy-(0.177f+fabsf(fx-ex)*0.1f))<0.003f) mat=6;
            }
            if (inside_oval(fx,fy,-0.005f,0.267f,0.010f,0.005f)) mat=20;
            if (inside_oval(fx,fy,0,0.289f,0.042f,0.025f)) {
                mat=6;
                if (inside_oval(fx,fy,0,0.291f,0.033f,0.020f)) mat=8;
                if (fy<0.278f && fax<0.029f) mat=7;
            }
            /* Franja lateral com mechas de comprimentos diferentes. */
            float fringe=0.135f;
            float strand=fx+0.17f;
            if (strand>0 && strand<0.23f) fringe=0.125f+strand*0.36f;
            if (fx>0.08f) fringe=0.15f+(fx-0.08f)*0.30f;
            if (fy<fringe) { mat=3; if (fabsf(fy-fringe)<0.006f) mat=6; }
        }
        if (fax>0.268f && fy>0.14f && fy<0.28f) mat=fx<0 ? 18 : 6;
        if (fax>0.295f && fy>0.19f && fy<0.242f) mat=4;
    }
    /* Mecha rebelde curva. */
    if (y>0.006f && y<0.055f && fabsf(x-(-0.045f+0.085f*sinf(y*48)))<0.012f) mat=3;
    return mat;
}

/* Desenha o Guts com capa preta, armadura de placas e a Dragonslayer nas costas */
static int player_guts_material(float x, float y, int back_view) {
    if (y < 0 || y > 1 || fabsf(x) > 1) return 0;
    float ax = fabsf(x);
    int mat = 0;
    /* Dragonslayer como parte da silhueta: lamina larga atras do ombro. */
    float sword_x = -0.68f + y * 0.83f;
    float sword_w = y < 0.10f ? 0.09f + y * 0.55f : 0.145f;
    if (y > 0.025f && y < 0.73f && fabsf(x - sword_x) < sword_w)
        mat = fabsf(x - sword_x) > sword_w - 0.022f ? 23 : (x < sword_x ? 22 : 21);
    if (y > 0.73f && y < 0.755f && fabsf(x - sword_x) < 0.22f) mat = 23;
    if (y > 0.755f && y < 0.90f && fabsf(x - sword_x) < 0.04f)
        mat = fmodf(y * 100, 2) < 0.5f ? 22 : 12;
    /* Capa preta recortada; botas e grevas pesadas. */
    float cape_edge = 0.44f + y * 0.17f;
    if (y > 0.22f && y < 0.85f + 0.04f * sinf(x * 31) && ax < cape_edge)
        mat = sinf(x * 34 + y * 5) > 0.65f ? 10 : 12;
    if (y > 0.64f && y < 0.97f && ax > 0.08f && ax < 0.29f) {
        mat = ax < 0.14f ? 12 : 21;
        if (y > 0.76f && y < 0.79f) mat = 22;
        if (y > 0.83f && ax > 0.23f) mat = 22;
    }
    if (y > 0.94f && y < 0.99f && ax > 0.07f && ax < 0.34f) mat = 12;
    /* Peitoral largo em placas sobrepostas e cinto com fivela. */
    if (y > 0.26f && y < 0.67f && ax < 0.38f - (y - 0.26f) * 0.32f) {
        mat = back_view ? 12 : 21;
        if (!back_view) {
            float plate = fmodf((y + ax * 0.10f) * 32, 1);
            if (plate < 0.15f) mat = 22;
            if (ax < 0.025f) mat = 10;
            if (y > 0.55f && y < 0.585f) mat = 15;
            if (y > 0.55f && y < 0.585f && ax < 0.065f) mat = 23;
            if (fabsf(x - (0.53f - y * 1.6f)) < 0.035f && y < 0.54f) mat = 12;
        }
    }
    /* Ombreiras angulares, rebites, braco direito e protese esquerda. */
    if (y > 0.26f && y < 0.39f && ax > 0.29f && ax < 0.52f - (y - 0.26f) * 0.45f) {
        mat = y < 0.28f || ax > 0.47f ? 22 : 21;
        if (fabsf(y - 0.31f) < 0.006f && fabsf(ax - 0.43f) < 0.014f) mat = 23;
    }
    if (y > 0.38f && y < 0.64f && ax > 0.34f && ax < 0.48f) {
        mat = x > 0 ? 24 : 22;
        if (y > 0.46f) mat = x > 0 ? 21 : 22;
        if (fmodf(y * 50, 1) < 0.12f) mat = 12;
        if (x < 0 && ax > 0.44f) mat = 23;
    }
    if (y > 0.63f && y < 0.69f && ax > 0.35f && ax < 0.47f) mat = x > 0 ? 24 : 21;
    if (y > 0.21f && y < 0.29f && ax < 0.13f) mat = back_view ? 25 : 24;
    /* Rosto angular, olho direito fechado e cicatriz. */
    float face_w = y < 0.18f ? 0.20f : 0.20f - (y - 0.18f) * 1.2f;
    if (y > 0.07f && y < 0.245f && ax < face_w) {
        mat = back_view ? 12 : (x > 0.11f ? 25 : 24);
        if (!back_view) {
            if (y > 0.143f && y < 0.159f && x > 0.045f && x < 0.15f) mat = 7;
            if (y > 0.143f && y < 0.159f && x > 0.087f && x < 0.113f) mat = 12;
            if (fabsf(y - (0.132f + ax * 0.05f)) < 0.006f && ax > 0.04f && ax < 0.16f) mat = 12;
            if (x < -0.04f && x > -0.15f && fabsf(y - (0.155f + x * 0.06f)) < 0.004f) mat = 12;
            if (fabsf(x + 0.10f + (y - 0.15f) * 0.20f) < 0.006f && y > 0.12f && y < 0.20f) mat = 25;
            if (x > -0.012f && x < 0.015f && y > 0.16f && y < 0.193f) mat = 25;
            if (ax < 0.08f && fabsf(y - (0.216f - x * 0.03f)) < 0.004f) mat = 12;
        }
    }
    /* Cabelo curto preto com pontas irregulares, sem capacete. */
    float spikes = 0.020f + 0.025f * fabsf(sinf(x * 49));
    if (y > spikes && y < 0.105f + x * 0.06f && ax < 0.215f) {
        mat = 12;
        if (fmodf((x + y * 0.6f) * 60 + 60, 3) < 0.35f) mat = 21;
    }
    if (y > 0.10f && y < 0.16f && ax > 0.18f && ax < 0.205f) mat = 12;
    /* Na vista traseira a espada fica sobre a capa. */
    if (back_view && y > 0.03f && y < 0.73f && fabsf(x - sword_x) < sword_w)
        mat = fabsf(x - sword_x) > sword_w - 0.022f ? 23 : 21;
    return mat;
}

/* Desenha o Rambo com calca camuflada, faixa de municao e bandana vermelha */
static int player_rambo_material(float x, float y, int back) {
    if (fabsf(x) > 1 || y < 0 || y > 1) return 0;
    float ax = fabsf(x);
    int mat = 0;
    /* Calca cargo camuflada, joelheiras e botas de combate. */
    if (y > 0.55f && y < 0.97f && ax > 0.065f && ax < 0.29f) {
        float camo = sinf(x*47+y*39) + cosf(y*61-x*17);
        mat = y > 0.86f ? 12 : camo > 0.7f ? 33 : camo < -0.6f ? 4 : 32;
        if (y > 0.64f && y < 0.72f && ax > 0.21f) mat = 33;
        if (y > 0.87f && y < 0.95f && ax > 0.13f && ax < 0.23f && fmodf(y*110,2)<0.5f) mat = 22;
    }
    if (y > 0.95f && y < 0.99f && ax > 0.055f && ax < 0.34f) mat = 12;
    /* Tronco forte sem camisa e bracos com volume. */
    float chest = 0.38f - (y-0.28f)*0.33f;
    if (y > 0.26f && y < 0.59f && ax < chest) {
        mat = ax > chest-0.035f ? 25 : 24;
        if (!back && y > 0.31f && y < 0.38f && ax > 0.03f && ax < 0.29f && fabsf(y-(0.365f-ax*0.07f))<0.005f) mat=25;
        if (!back && y > 0.40f && y < 0.54f && ax < 0.15f && (ax < 0.009f || fmodf(y*50,2)<0.19f)) mat=25;
        if (back && ax > 0.09f && ax < 0.105f && y < 0.47f) mat=25;
    }
    if (y > 0.28f && y < 0.65f) {
        float arm = 0.40f + 0.025f*sinf(y*12);
        float width = 0.07f + 0.025f*sinf((y-0.28f)*10);
        if (fabsf(ax-arm)<width) {
            mat = ax > arm+width*0.5f ? 25 : 24;
            if (y > 0.565f && y < 0.61f) mat=12;
        }
    }
    if (y > 0.235f && y < 0.28f && ax < 0.12f) mat=24;
    /* Cinto, coldre de faca e faixa de cartuchos cruzando o peito. */
    if (y > 0.55f && y < 0.59f && ax < 0.30f) mat = ax < 0.055f ? 15 : 12;
    if (x > 0.27f && x < 0.33f && y > 0.57f && y < 0.72f) mat = y < 0.61f ? 23 : 4;
    if (y > 0.28f && y < 0.555f && fabsf(x-(0.88f-y*2.15f))<0.054f) {
        mat=12;
        if (!back && fmodf(y*92,2)<1.25f && fabsf(x-(0.88f-y*2.15f))<0.041f) mat=26;
    }
    /* Cabelo comprido e rosto angular com expressao concentrada. */
    if (y > 0.05f && y < 0.29f && ax < 0.25f) mat=12;
    float face_w= y<0.18f ? 0.185f : 0.185f-(y-0.18f)*1.0f;
    if (!back && y > 0.10f && y < 0.25f && ax < face_w) {
        mat = x>0.10f ? 25 : 24;
        if (y > 0.149f && y < 0.163f && ax > 0.045f && ax < 0.139f) mat=7;
        if (y > 0.149f && y < 0.163f && ax > 0.08f && ax < 0.105f) mat=12;
        if (fabsf(y-(0.14f+ax*0.04f))<0.005f && ax>0.04f && ax<0.15f) mat=12;
        if (ax<0.012f && y>0.166f && y<0.195f) mat=25;
        if (ax<0.07f && fabsf(y-0.22f)<0.0035f) mat=12;
    }
    if (inside_oval(x,y,0,0.087f,0.25f,0.063f) && y<0.095f)
        mat = sinf(x*65+y*40)>0.75f ? 4 : 12;
    /* Bandana vermelha e duas pontas soltas ao lado da cabeca. */
    if (y>0.09f && y<0.12f && ax<0.245f) mat=y<0.099f ? 18 : 3;
    if (x < -0.22f && x > -0.43f) {
        float tail=-x-0.22f;
        if (fabsf(y-(0.12f+tail*0.5f))<0.013f || fabsf(y-(0.13f+tail*0.95f))<0.012f) mat=3;
    }
    return mat;
}

/* Despacha o desenho do sprite do preset escolhido (ou desenha o Arauto das Sombras) */
int player_character_material(int preset, float x, float y, int back_view) {
    if (!player_get_visual(preset).custom_sprite) return 0;
    if (preset == PRESET_GUTS) return player_guts_material(x, y, back_view);
    if (preset == PRESET_RAMBO) return player_rambo_material(x, y, back_view);
    if (preset != PRESET_SHADOW_HERALD) return player_teto_material(x, y, back_view);
    float ax = fabsf(x);
    int mat = 0;
    if (y < 0.0f || y > 1.0f || ax > 1.0f) return 0;
    /* Manto longo, tecido em dobras e barra irregular. Sem cajado. */
    float hem = 0.96f + 0.025f * sinf(x * 35.0f);
    if (y > 0.23f && y < hem && ax < 0.30f + y * 0.28f) {
        float fold = sinf(x * 47.0f + y * 3.0f);
        mat = fold > 0.55f ? 11 : (fold < -0.6f ? 12 : 10);
        if (!back_view && ax < 0.14f + y * 0.075f) mat = fold > 0 ? 13 : 14;
        if (ax > 0.27f + y * 0.28f) mat = 11;
    }
    /* Mangas amplas e maos enluvadas junto ao corpo. */
    if (y > 0.27f && y < 0.63f && ax > 0.29f && ax < 0.58f - (y - 0.27f) * 0.13f)
        mat = ax > 0.48f ? 11 : 10;
    if (y > 0.59f && y < 0.66f && ax > 0.36f && ax < 0.46f) mat = 15;
    /* Ombreiras de penas sobrepostas, com pontas serrilhadas. */
    if (y > 0.21f && y < 0.47f && ax > 0.23f) {
        float row = (y - 0.21f) * 31.0f - ax * 7.0f;
        float tooth = row - floorf(row);
        float edge = 0.62f - (y - 0.25f) * 0.32f - tooth * 0.10f;
        if (ax < edge) mat = tooth < 0.20f ? 16 : (tooth > 0.78f ? 12 : 11);
    }
    if (!back_view) {
        /* Cinto, fivela, correntes e pequenos talismas de metal envelhecido. */
        if (y > 0.465f && y < 0.50f && ax < 0.32f) mat = 12;
        if (y > 0.465f && y < 0.50f && ax < 0.07f) mat = ax > 0.045f || y < 0.475f || y > 0.49f ? 15 : 12;
        if (y > 0.51f && y < 0.77f && fabsf(ax - (0.14f + 0.04f * sinf(y * 20))) < 0.012f) mat = 15;
        if (y > 0.74f && y < 0.785f && fabsf(ax - 0.16f) < 0.025f) mat = 16;
        if (y > 0.27f && y < 0.43f && fabsf(ax - (0.43f - y) * 0.85f) < 0.012f) mat = 15;
        if (x*x / 0.0018f + (y - 0.40f)*(y - 0.40f) / 0.00045f < 1) mat = 17;
    }
    /* Capuz pontudo, borda iluminada e rosto quase oculto. */
    if (y > 0.02f && y < 0.30f) {
        float hood = y < 0.11f ? 0.055f + (y - 0.02f) * 2.1f : 0.244f - (y - 0.11f) * 0.26f;
        if (ax < hood) {
            mat = ax > hood - 0.035f ? 16 : 10;
            if (!back_view && y > 0.105f && ax < hood - 0.045f) {
                mat = ax > hood - 0.065f ? 14 : 12;
                if (y > 0.18f && y < 0.245f && ax < (0.245f - y) * 0.95f) mat = 10;
                if (y > 0.182f && y < 0.215f && x > -0.018f && x < 0.008f) mat = 11;
                if (y > 0.165f && y < 0.177f && ax > 0.04f && ax < 0.095f) mat = 17;
            }
        }
    }
    return mat;
}

/* Mantem o angulo sempre no intervalo [0, 2*PI) */
static float normalize_angle(float angle) {
    float two_pi = 2.0f * PI_F;
    while (angle < 0.0f) {
        angle += two_pi;
    }
    while (angle >= two_pi) {
        angle -= two_pi;
    }
    return angle;
}

/* Testa os 4 cantos da caixa de colisao do jogador para evitar atravessar paredes */
static int is_position_blocked(const GameMap *map, float x, float y) {
    float r = PLAYER_RADIUS;

    if (map_is_wall(map, (int)floorf(x - r), (int)floorf(y - r))) return 1;
    if (map_is_wall(map, (int)floorf(x + r), (int)floorf(y - r))) return 1;
    if (map_is_wall(map, (int)floorf(x - r), (int)floorf(y + r))) return 1;
    if (map_is_wall(map, (int)floorf(x + r), (int)floorf(y + r))) return 1;

    return 0;
}

/* Devolve a configuracao base de uma das classes jogaveis pre-definidas */
CharacterClass player_get_preset_class(int index) {
    static const CharacterClass PRESETS[PRESET_CLASS_COUNT] = {
        {"Fuzileiro", PLAYER_MAX_HEALTH, 30, MOVE_SPEED, 0, PRESET_FUZILEIRO, {0}, ABILITY_NONE},
        {"Kasane Teto", PLAYER_MAX_HEALTH, 30, MOVE_SPEED, 0, PRESET_KASANE_TETO, {0}, ABILITY_TETO},
        {"Arauto das Sombras", PLAYER_MAX_HEALTH, 30, MOVE_SPEED, 0, PRESET_SHADOW_HERALD, {0}, ABILITY_SHADOW},
        {"Guts", PLAYER_MAX_HEALTH, 30, MOVE_SPEED, 0, PRESET_GUTS, {0}, ABILITY_BERSERK},
        {"Rambo", PLAYER_MAX_HEALTH, 30, MOVE_SPEED, 0, PRESET_RAMBO, {0}, ABILITY_GUERRILLA}
    };

    if (index < 0 || index >= PRESET_CLASS_COUNT) {
        return PRESETS[0];
    }
    return PRESETS[index];
}

/* Atualiza contadores das habilidades, flash de dano e retorno do modo Berserk */
void player_tick_ability(Player *p, float dt) {
    if (!p || dt <= 0) return;
    AbilityState *s = &p->ability_state;
    if (p->ability == ABILITY_GUERRILLA && p->health > 0)
        s->guerrilla_time = fminf(GUERRILLA_CHARGE_TIME, s->guerrilla_time + dt);
    s->cooldown = fmaxf(0, s->cooldown - dt);
    s->zone_time = fmaxf(0, s->zone_time - dt);
    if (p->hurt_timer > 0.0f) {
        p->hurt_timer = fmaxf(0.0f, p->hurt_timer - dt);
    }
    if (s->berserk_time > 0) {
        s->berserk_time = fmaxf(0, s->berserk_time - dt);
        if (s->berserk_time == 0) {
            /* Devolve a arma anterior quando a armadura Berserker expira (ou Pistola se acabou) */
            int restore = player_weapon_has_ammo(p, s->saved_weapon) ? s->saved_weapon : 0;
            p->active_weapon = restore;
            p->ammo = p->weapon_mag[restore];
            p->shoot_timer = 0;
        }
    }
}

/* Contabiliza abate para pontuacao e desbloqueio do Berserk do Guts */
void player_register_kill(Player *p, float x, float y) {
    (void)x;
    (void)y;
    if (!p || p->health <= 0) return;
    AbilityState *s = &p->ability_state;
    s->kills++;
    p->score += 10;
}

/* Ativa a habilidade especial da classe ao apertar E; retorna 1 se disparou */
int player_activate_ability(Player *p) {
    if (!p || p->health <= 0) return 0;
    AbilityState *s = &p->ability_state;
    if (p->ability == ABILITY_TETO && s->cooldown <= 0) {
        s->zone_x = p->x; s->zone_y = p->y;
        s->zone_time = TETO_ZONE_DURATION;
        s->cooldown = ABILITY_COOLDOWN;
        return 1;
    }
    if (p->ability == ABILITY_SHADOW && s->cooldown <= 0 && !s->fireball_active) {
        s->fireball_active = 1;
        s->fireball_x = p->x; s->fireball_y = p->y;
        s->fireball_dx = cosf(p->angle); s->fireball_dy = sinf(p->angle);
        s->fireball_time = 3;
        s->cooldown = ABILITY_COOLDOWN;
        return 1;
    }
    if (p->ability == ABILITY_BERSERK && s->kills >= BERSERK_KILLS && !s->berserk_used) {
        s->berserk_used = 1;
        s->berserk_time = BERSERK_DURATION;
        s->saved_weapon = p->active_weapon;
        p->active_weapon = BERSERK_WEAPON;
        p->shoot_timer = 0;
        return 1;
    }
    return 0;
}

/* Checa se a Teto esta dentro da propria zona de coracoes ativa */
static int player_inside_teto_zone(const Player *p) {
    const AbilityState *s = &p->ability_state;
    float dx = p->x - s->zone_x, dy = p->y - s->zone_y;
    return p->ability == ABILITY_TETO && s->zone_time > 0 &&
           dx*dx + dy*dy <= TETO_ZONE_RADIUS*TETO_ZONE_RADIUS;
}

/* Velocidade atual considerando o bonus de +35% da zona da Teto */
float player_effective_speed(const Player *p) {
    float speed = p->move_speed > 0.5f ? p->move_speed : MOVE_SPEED;
    return player_inside_teto_zone(p) ? speed * 1.35f : speed;
}

/* Dano ajustado pelo bonus de +25% dentro da zona da Teto */
int player_attack_damage(const Player *p, int base_damage) {
    return player_inside_teto_zone(p) ? (int)roundf(base_damage * 1.25f) : base_damage;
}

/* Aplica dano no jogador (zera carga do Rambo e dobra o dano recebido durante Berserk) */
void player_take_damage(Player *p, int damage) {
    if (!p || damage <= 0 || p->health <= 0) return;
    p->ability_state.guerrilla_time = 0;
    if (p->ability_state.berserk_time > 0) damage *= 2;
    p->health = p->health > damage ? p->health - damage : 0;
    p->hurt_timer = 0.35f;
    p->last_damage = damage;
}

/* --- Tabela de armas, inventario e recarga --- */

/* Capacidade do pente de cada arma (padrao e drops dos bosses) */
int player_weapon_mag_size(int weapon_type) {
    switch (weapon_type) {
        case 0: return 10; /* Pistola */
        case 1: return 6;  /* Shotgun */
        case 2: return 5;  /* Sniper */
        case 3: return 30; /* Metralhadora */
        case 4: return 4;  /* Arma Futurista */
        case 5: return 10; /* Maquita (Boss 1) */
        case 6: return 6;  /* Revolver Grande (Boss 2) */
        case 7: return 50; /* Tommy Gun (Boss 3) */
        case 8: return 40; /* Lanca-Chamas (Boss 4) */
        default: return 10;
    }
}

/* Intervalo minimo entre disparos de cada arma em segundos */
float player_weapon_cooldown(int weapon_type) {
    switch (weapon_type) {
        case 0: return 0.28f; /* Pistola */
        case 1: return 0.72f; /* Shotgun */
        case 2: return 1.50f; /* Sniper */
        case 3: return 0.075f;/* Metralhadora (altissima cadencia) */
        case 4: return 0.90f; /* Arma Futurista */
        case 5: return 0.045f;/* Maquita (corpo a corpo ultra-rapida, mais rapida que metralhadora) */
        case 6: return 0.62f; /* Revolver Grande (Boss 2) */
        case 7: return 0.075f;/* Tommy Gun (Boss 3) */
        case 8: return 0.11f; /* Lanca-Chamas (Boss 4) */
        default: return FIRE_COOLDOWN;
    }
}

/* Nome curto exibido no HUD inferior para cada arma */
const char *player_weapon_name(int weapon_type) {
    switch (weapon_type) {
        case 0: return "PISTOLA";
        case 1: return "SHOTGUN";
        case 2: return "SNIPER";
        case 3: return "METRALH";
        case 4: return "FUTURIST";
        case 5: return "MAQUITA";
        case 6: return "REVOLVER";
        case 7: return "TOMMYGUN";
        case 8: return "L.CHAMAS";
        default: return "PISTOLA";
    }
}

/* Somente a pistola inicial possui reserva infinita de balas */
int player_weapon_has_infinite_reserve(int weapon_type) {
    return weapon_type == 0;
}

/* Retorna 1 se o jogador possui a arma e ela ainda tem municao (ou nao gasta bala) */
int player_weapon_has_ammo(const Player *p, int weapon_type) {
    if (!p || weapon_type < 0 || weapon_type >= PLAYER_MAX_WEAPONS || !p->weapon_owned[weapon_type]) {
        return 0;
    }
    if (weapon_type == 5 || player_weapon_has_infinite_reserve(weapon_type)) {
        return 1;
    }
    return (p->weapon_mag[weapon_type] > 0 || p->weapon_reserve[weapon_type] > 0);
}

/* Entrega uma arma ou soma municao na reserva (1 = arma nova, 2 = municao extra) */
int player_pickup_weapon(Player *p, int weapon_type, int ammo_amount) {
    if (!p || weapon_type < 0 || weapon_type >= PLAYER_MAX_WEAPONS || ammo_amount <= 0) {
        return 0;
    }
    int mag = player_weapon_mag_size(weapon_type);
    if (!p->weapon_owned[weapon_type]) {
        p->weapon_owned[weapon_type] = 1;
        int load = ammo_amount < mag ? ammo_amount : mag;
        p->weapon_mag[weapon_type] = load;
        if (!player_weapon_has_infinite_reserve(weapon_type)) {
            p->weapon_reserve[weapon_type] += ammo_amount - load;
        }
        if (p->active_weapon == weapon_type) {
            p->ammo = p->weapon_mag[weapon_type];
        }
        return 1;
    }
    if (!player_weapon_has_infinite_reserve(weapon_type)) {
        p->weapon_reserve[weapon_type] += ammo_amount;
    }
    return 2;
}

/* Recarrega o pente da arma ativa usando a reserva (nao funciona no Berserk nem na Maquita) */
int player_reload(Player *p) {
    if (!p || p->health <= 0 || p->ability_state.berserk_time > 0.0f) {
        return 0;
    }
    int w = p->active_weapon;
    if (w < 0 || w >= PLAYER_MAX_WEAPONS || !p->weapon_owned[w]) {
        return 0;
    }
    int mag = player_weapon_mag_size(w);
    if (w == 5) {
        return 0;
    }
    if (p->weapon_mag[w] >= mag) {
        p->ammo = p->weapon_mag[w];
        return 0;
    }
    if (player_weapon_has_infinite_reserve(w)) {
        p->weapon_mag[w] = mag;
        p->ammo = mag;
        return 1;
    }
    int need = mag - p->weapon_mag[w];
    if (p->weapon_reserve[w] <= 0) {
        return 0;
    }
    int take = p->weapon_reserve[w] < need ? p->weapon_reserve[w] : need;
    p->weapon_mag[w] += take;
    p->weapon_reserve[w] -= take;
    p->ammo = p->weapon_mag[w];
    return 1;
}

/* Troca diretamente para a arma indicada se o jogador a possuir com municao */
int player_switch_weapon(Player *p, int weapon_type) {
    if (!p || p->ability_state.berserk_time > 0.0f) {
        return 0;
    }
    if (!player_weapon_has_ammo(p, weapon_type)) {
        return 0;
    }
    p->active_weapon = weapon_type;
    p->ammo = p->weapon_mag[weapon_type];
    return 1;
}

/* Avanca ou volta para a proxima arma disponivel (roda do mouse / teclas de troca) */
int player_cycle_weapon(Player *p, int dir) {
    if (!p || p->ability_state.berserk_time > 0.0f) {
        return 0;
    }
    int step = (dir < 0) ? -1 : 1;
    int cur = p->active_weapon;
    if (cur < 0 || cur >= PLAYER_MAX_WEAPONS) {
        cur = 0;
    }
    for (int i = 1; i < PLAYER_MAX_WEAPONS; i++) {
        int cand = (cur + step * i) % PLAYER_MAX_WEAPONS;
        if (cand < 0) cand += PLAYER_MAX_WEAPONS;
        if (player_weapon_has_ammo(p, cand)) {
            p->active_weapon = cand;
            p->ammo = p->weapon_mag[cand];
            return 1;
        }
    }
    return 0;
}

/* Inicia um ataque/tiro consumindo municao; se zerar pente e reserva, descarta a arma */
int player_begin_attack(Player *p) {
    if (!p || p->health <= 0 || p->shoot_timer > 0) return 0;
    if (p->ability_state.berserk_time > 0) {
        p->shoot_timer = 0.60f;
        return 1;
    }
    int w = p->active_weapon;
    if (w < 0 || w >= PLAYER_MAX_WEAPONS) w = 0;
    if (w == 5) {
        /* Maquita (corpo a corpo continua): nao consome municao ao segurar ligada. */
        p->ability_state.boosted_shot = p->ability == ABILITY_GUERRILLA &&
            p->ability_state.guerrilla_time >= GUERRILLA_CHARGE_TIME;
        if (p->ability_state.boosted_shot) p->ability_state.guerrilla_time = 0;
        p->shoot_timer = player_weapon_cooldown(w);
        return 1;
    }
    if (p->ammo <= 0) {
        if (!player_weapon_has_infinite_reserve(w) && p->weapon_reserve[w] <= 0 && w != 0) {
            p->weapon_owned[w] = 0;
            p->weapon_mag[w] = 0;
            if (!player_cycle_weapon(p, -1)) {
                p->active_weapon = 0;
                p->ammo = p->weapon_mag[0];
            }
        }
        return 0;
    }
    if (p->weapon_mag[w] <= 0) {
        int mag = player_weapon_mag_size(w);
        p->weapon_mag[w] = p->ammo > mag ? mag : p->ammo;
    }
    if (p->weapon_mag[w] <= 0) return 0;
    p->ability_state.boosted_shot = p->ability == ABILITY_GUERRILLA &&
        p->ability_state.guerrilla_time >= GUERRILLA_CHARGE_TIME;
    if (p->ability_state.boosted_shot) p->ability_state.guerrilla_time = 0;
    p->weapon_mag[w]--;
    p->ammo = p->weapon_mag[w];
    float shot_cd = player_weapon_cooldown(w);
    p->shoot_timer = shot_cd;

    /* Se acabou toda a bala (pente + reserva) de uma arma finita, some do inventario e volta pra outra */
    if (!player_weapon_has_infinite_reserve(w) && p->weapon_mag[w] <= 0 && p->weapon_reserve[w] <= 0) {
        p->weapon_owned[w] = 0;
        p->weapon_mag[w] = 0;
        p->weapon_reserve[w] = 0;
        if (!player_cycle_weapon(p, -1)) {
            p->active_weapon = 0;
            p->ammo = p->weapon_mag[0];
        }
    }
    return 1;
}

/* Resolve o material de renderizacao do jogador (sobrepoe o elmo se estiver em Berserk) */
int player_render_material(const Player *p, float x, float y, int back) {
    if (p->ability_state.berserk_time <= 0)
        return player_appearance_material(p->visual_id, &p->appearance, x, y, back);
    /* Armadura Berserker: elmo de fera, olhos vermelhos e placas negras. */
    int mat = player_character_material(PRESET_GUTS, x, y, back);
    if (mat == 24 || mat == 25) mat = 21;
    if (mat == 21) mat = 10;
    float ax = fabsf(x);
    if (y > 0.035f && y < 0.25f && ax < 0.22f - fmaxf(0, y-0.17f)*0.7f) {
        mat = ax > 0.17f ? 22 : 12;
        if (!back && y > 0.14f && y < 0.16f && ax > 0.035f && ax < 0.16f) mat = 17;
        if (!back && y > 0.19f && y < 0.22f && ax < 0.13f)
            mat = fmodf(ax*90, 2) < 0.6f ? 23 : 12;
    }
    if (y > 0.005f && y < 0.08f && ax > 0.13f && ax < 0.14f + y*1.2f) mat = 21;
    return mat;
}

/* Zera a lista de personagens customizados criados pelo usuario */
void player_roster_init(CharacterRoster *roster) {
    if (roster) memset(roster, 0, sizeof(*roster));
}

/* Total de opcoes disponiveis no menu de selecao (presets + customizados) */
int player_roster_count(const CharacterRoster *roster) {
    return PRESET_CLASS_COUNT + (roster ? roster->count : 0);
}

/* Retorna a classe do catalogo pelo indice unificado */
CharacterClass player_roster_get(const CharacterRoster *roster, int index) {
    if (roster && index >= PRESET_CLASS_COUNT && index < player_roster_count(roster))
        return roster->custom[index - PRESET_CLASS_COUNT];
    return player_get_preset_class(index);
}

/* Valida, limpa espacos do nome e salva um novo personagem customizado no roster */
int player_roster_add(CharacterRoster *roster, const CharacterClass *draft) {
    if (!roster || !draft || roster->count >= MAX_CUSTOM_CHARACTERS ||
        !player_appearance_valid(&draft->appearance) ||
        !memchr(draft->name, '\0', sizeof(draft->name)) ||
        draft->visual_id < 0 || draft->visual_id >= PRESET_CLASS_COUNT) return -1;
    CharacterClass cls = *draft;
    /* Criacoes sao skins: somente nome e aparencia variam. */
    CharacterClass base = player_get_preset_class(PRESET_FUZILEIRO);
    cls.ability = ABILITY_NONE;
    cls.max_health = base.max_health;
    cls.start_ammo = base.start_ammo;
    cls.move_speed = base.move_speed;
    cls.start_weapon = base.start_weapon;
    size_t start = 0, len = strlen(cls.name);
    while (start < len && cls.name[start] == ' ') start++;
    while (len > start && cls.name[len - 1] == ' ') len--;
    if (len == start) return -1;
    memmove(cls.name, cls.name + start, len - start);
    cls.name[len - start] = '\0';
    roster->custom[roster->count] = cls;
    return PRESET_CLASS_COUNT + roster->count++;
}

/* Aplica os atributos, aparencia, habilidade e inventario inicial de uma classe no jogador */
void player_apply_class(Player *p, const CharacterClass *cls) {
    if (!p || !cls) {
        return;
    }

    snprintf(p->class_name, sizeof(p->class_name), "%s", cls->name);
    p->ability = cls->appearance.enabled ? ABILITY_NONE : cls->ability;
    memset(&p->ability_state, 0, sizeof(p->ability_state));
    p->appearance = player_appearance_valid(&cls->appearance) ? cls->appearance : (CharacterAppearance){0};
    p->visual_id = (cls->visual_id >= 0 && cls->visual_id < PRESET_CLASS_COUNT)
        ? cls->visual_id : PRESET_FUZILEIRO;
    p->max_health = (cls->max_health > 0) ? cls->max_health : PLAYER_MAX_HEALTH;
    p->health = p->max_health;
    memset(p->weapon_owned, 0, sizeof(p->weapon_owned));
    memset(p->weapon_mag, 0, sizeof(p->weapon_mag));
    memset(p->weapon_reserve, 0, sizeof(p->weapon_reserve));
    p->weapon_owned[0] = 1;
    p->weapon_mag[0] = player_weapon_mag_size(0);
    p->weapon_reserve[0] = 0;
    p->ammo = (cls->start_ammo > 0) ? cls->start_ammo : 30;
    p->move_speed = (cls->move_speed > 0.5f) ? cls->move_speed : MOVE_SPEED;
    p->active_weapon = cls->start_weapon;
}

/* Inicializa o jogador na posicao (x, y) olhando para angle, com a classe Fuzileiro */
void player_init(Player *p, float x, float y, float angle) {
    if (!p) {
        return;
    }

    p->id = 1;
    p->x = x;
    p->y = y;
    p->angle = normalize_angle(angle);
    p->fov = FOV;
    p->score = 0;
    p->bob_phase = 0.0f;
    p->shoot_timer = 0.0f;
    p->hurt_timer = 0.0f;
    p->last_damage = 0;

    CharacterClass base_cls = player_get_preset_class(PRESET_FUZILEIRO);
    player_apply_class(p, &base_cls);
}

/* Tenta mover em X e Y separado para deslizar nas paredes sem travar */
int player_move(Player *p, const GameMap *map, float forward, float strafe) {
    if (!p || !map) {
        return 0;
    }

    float dx = cosf(p->angle) * forward - sinf(p->angle) * strafe;
    float dy = sinf(p->angle) * forward + cosf(p->angle) * strafe;
    int moved = 0;

    if (!is_position_blocked(map, p->x + dx, p->y)) {
        p->x += dx;
        moved = 1;
    }
    if (!is_position_blocked(map, p->x, p->y + dy)) {
        p->y += dy;
        moved = 1;
    }

    return moved;
}

/* Gira a camera do jogador mantendo o angulo normalizado */
void player_rotate(Player *p, float delta_angle) {
    if (!p) {
        return;
    }
    p->angle = normalize_angle(p->angle + delta_angle);
}

/* Processa teclado e mouse do jogador a cada frame (habilidade, movimento, tiro e pausa) */
void player_handle_input(Player *p, const GameMap *map, float dt,
                         int *running, int *did_shoot) {
    if (!p || !map || !running || !did_shoot) {
        return;
    }

    *did_shoot = 0;
    player_tick_ability(p, dt);
    if (IsKeyPressed(KEY_E)) player_activate_ability(p);

    if (p->shoot_timer > 0.0f) {
        p->shoot_timer -= dt;
        if (p->shoot_timer < 0.0f) {
            p->shoot_timer = 0.0f;
        }
    }

    /* Rotacao pelo mouse (Mouse Look 360) quando o cursor esta preso */
    if (IsCursorHidden()) {
        Vector2 mouse_delta = GetMouseDelta();
        if (mouse_delta.x != 0.0f) {
            player_rotate(p, mouse_delta.x * MOUSE_SENSITIVITY);
        }
    }

    /* E reservado para habilidades; Q/Setas giram a camera. */
    float rot_dir = 0.0f;
    if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_LEFT))  rot_dir -= 1.0f;
    if (IsKeyDown(KEY_RIGHT)) rot_dir += 1.0f;
    if (rot_dir != 0.0f) {
        player_rotate(p, rot_dir * ROT_SPEED * dt);
    }

    /* Movimentacao continua em WASD (ou setas cima/baixo) */
    float fwd_input = 0.0f;
    float str_input = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))   fwd_input += 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) fwd_input -= 1.0f;
    if (IsKeyDown(KEY_A)) str_input -= 1.0f;
    if (IsKeyDown(KEY_D)) str_input += 1.0f;

    if (fwd_input != 0.0f || str_input != 0.0f) {
        /* Normaliza vetor diagonal para nao correr mais rapido na diagonal */
        float mag = sqrtf(fwd_input * fwd_input + str_input * str_input);
        fwd_input /= mag;
        str_input /= mag;

        float speed = player_effective_speed(p);
        int sprinting = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (sprinting) {
            speed *= SPRINT_MULT;
        }

        if (player_move(p, map, fwd_input * speed * dt, str_input * speed * dt)) {
            p->bob_phase += dt * (sprinting ? 14.0f : 10.0f);
        }
    }

    /* Recarregar municao e troca de arma por scroll ou F */
    if (IsKeyPressed(KEY_R)) {
        player_reload(p);
    }
    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) {
        player_cycle_weapon(p, -1);
    } else if (wheel < 0.0f) {
        player_cycle_weapon(p, 1);
    }
    if (IsKeyPressed(KEY_F)) {
        player_cycle_weapon(p, 1);
    }

    /* Disparo com clique esquerdo ou Espaco, limitado por FIRE_COOLDOWN (dt) */
    int want_shoot = (IsCursorHidden() && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ||
                     IsKeyDown(KEY_SPACE);
    if (want_shoot) *did_shoot = player_begin_attack(p);

    /* ESC solicita encerramento da partida */
    if (IsKeyPressed(KEY_ESCAPE)) {
        *running = 0;
    }
}

/* Chamas afiladas do Fogo Negro do Arauto das Sombras via RendererApi. */
static void player_draw_shadow_flame(const Player *p, const RendererApi *r,
                                     float wx, float wy, float height,
                                     float size, float phase, int dark_core) {
    int sx, sy;
    float depth;
    if (!r->project_point(p, wx, wy, height, &sx, &sy, &depth)) {
        return;
    }
    int rw = 0, rh = 0;
    r->get_grid_size(&rw, &rh);
    int radius = (int)((float)rh * size / depth);
    if (radius < 2) radius = 2;
    if (radius > 32) radius = 32;
    AsciiColor black = {0, 0, 0};
    for (int y = -radius * 2; y <= radius; y++) {
        float v = (float)y / (float)radius;
        float width = v < 0.0f ? 1.0f + v * 0.46f : sqrtf(fmaxf(0.0f, 1.0f - v * v));
        float sway = sinf(phase * 7.0f + v * 5.0f) * 0.16f * (1.0f - v);
        for (int x = -radius * 2; x <= radius * 2; x++) {
            int px = sx + x;
            int py = sy + y;
            if (px < 0 || px >= rw || py < 0 || py >= rh) {
                continue;
            }
            float u = (float)x / (float)radius - sway;
            float edge = fabsf(u) / fmaxf(width, 0.01f);
            float ripple = 0.09f * sinf(v * 13.0f + phase * 11.0f + u * 4.0f);
            if (edge > 1.0f + ripple) {
                continue;
            }
            float vein = sinf(u * 9.0f + v * 8.0f - phase * 12.0f);
            AsciiColor color;
            char ch;
            if (dark_core && u * u + v * v < 0.30f) {
                color = (AsciiColor){65, 28, 95};
                ch = ':';
            } else if (edge < 0.48f && v > -0.6f) {
                color = (AsciiColor){250, 205, 255};
                ch = '@';
            } else if (edge < 0.78f) {
                color = (AsciiColor){195, 95, 255};
                ch = vein > 0.0f ? '#' : '*';
            } else {
                color = (AsciiColor){110, 48, 185};
                ch = v < -0.6f ? '^' : '+';
            }
            r->set_cell_if_closer(px, py, depth, ch, color, black);
        }
    }
}

/* Desenha os efeitos visuais das habilidades ativas no mundo 3D (zona da Teto e Fogo Negro) */
void player_render_abilities(const Player *p, const RendererApi *r) {
    if (!p || !r) {
        return;
    }
    const AbilityState *s = &p->ability_state;
    AsciiColor black = {0, 0, 0};
    float time = r->get_time();

    if (s->zone_time > 0.0f) {
        r->draw_world_floor_ring(s->zone_x, s->zone_y, TETO_ZONE_RADIUS,
                                 '#', (AsciiColor){255, 80, 170},
                                 '+', (AsciiColor){190, 55, 120});
        int rw = 0, rh = 0;
        r->get_grid_size(&rw, &rh);
        for (int i = 0; i < 16; i++) {
            float a = (float)i * 2.4f;
            float radius = 0.5f + (float)(i % 4) * 0.42f;
            float height = 0.15f + fmodf(time * 0.45f + (float)i * 0.17f, 0.8f);
            int sx, sy;
            float depth;
            if (!r->project_point(p, s->zone_x + cosf(a) * radius,
                                  s->zone_y + sinf(a) * radius, height, &sx, &sy, &depth)) {
                continue;
            }
            static const char *heart[] = {"@ @", "@@@", " @ "};
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    int px = sx + col - 1;
                    int py = sy + row;
                    if (heart[row][col] != ' ' && px >= 0 && px < rw && py >= 0 && py < rh) {
                        r->set_cell_if_closer(px, py, depth, heart[row][col],
                                              (AsciiColor){255, 115, 180}, black);
                    }
                }
            }
        }
    }

    if (s->fireball_active) {
        float age = fmaxf(0.0f, 3.0f - s->fireball_time);
        for (int i = 7; i >= 1; i--) {
            float lag = (float)i * 0.075f;
            if (lag > age * 7.0f) continue;
            float flutter = sinf(time * 14.0f - (float)i * 1.8f) * 0.035f;
            player_draw_shadow_flame(p, r,
                                     s->fireball_x - s->fireball_dx * lag - s->fireball_dy * flutter,
                                     s->fireball_y - s->fireball_dy * lag + s->fireball_dx * flutter,
                                     0.48f + (float)i * 0.012f,
                                     0.075f - (float)i * 0.008f,
                                     time + (float)i * 0.4f, 0);
        }
        player_draw_shadow_flame(p, r, s->fireball_x, s->fireball_y, 0.48f,
                                 0.115f + 0.009f * sinf(time * 17.0f), time, 1);
    }
}

/* Renderiza a Dragonslayer em primeira pessoa durante o modo Berserk; retorna 1 se desenhou */
int player_render_weapon(const Player *p, const RendererApi *r) {
    if (!p || !r || p->ability_state.berserk_time <= 0.0f) {
        return 0;
    }
    int rw = 0, rh = 0;
    r->get_grid_size(&rw, &rh);
    if (rw <= 0 || rh <= 0) {
        return 0;
    }

    AsciiColor black = {0, 0, 0};
    float aspect = (float)rw / (float)(rh > 0 ? rh : 1);
    float time_now = r->get_time ? r->get_time() : 0.0f;
    int swinging = (p->shoot_timer > 0.0f);
    float prog = swinging ? (1.0f - fminf(1.0f, p->shoot_timer / 0.60f)) : 0.0f;

    /* Fases do golpe da Matadora de Dragoes:
     * 0.00..0.16: preparacao (puxa para cima/direita)
     * 0.16..0.44: corte diagonal explosivo (direita-topo -> esquerda-baixo)
     * 0.44..0.58: impacto pesado (pausa/recuo embaixo-esquerda)
     * 0.58..1.00: retorno pesado para a guarda a direita */
    float idle_bob_x = sinf(p->bob_phase) * 0.012f;
    float idle_bob_y = fabsf(cosf(p->bob_phase)) * 0.016f + sinf(time_now * 3.0f) * 0.006f;

    /* Angulo 0 = apontando para cima; negativo = inclinado p/ esquerda; positivo = p/ direita */
    float piv_x = 0.72f + idle_bob_x;
    float piv_y = 0.96f + idle_bob_y;
    float ang = -0.22f;
    float blade_len = 0.78f;

    if (swinging) {
        if (prog < 0.16f) {
            float k = prog / 0.16f;
            float e = k * k;
            piv_x = 0.72f + 0.14f * e;
            piv_y = 0.96f - 0.16f * e;
            ang = -0.22f + 0.62f * e;
            blade_len = 0.78f - 0.04f * e;
        } else if (prog < 0.44f) {
            float k = (prog - 0.16f) / 0.28f;
            /* Curva rapida de aceleracao do golpe (ease-out cubico) */
            float inv = 1.0f - k;
            float e = 1.0f - inv * inv * inv;
            piv_x = 0.86f - 0.72f * e;
            piv_y = 0.80f + 0.24f * e;
            ang = 0.40f - 2.35f * e;
            blade_len = 0.74f + 0.18f * sinf(k * PI_F);
        } else if (prog < 0.58f) {
            float k = (prog - 0.44f) / 0.14f;
            float shake = sinf(k * PI_F * 3.0f) * 0.012f * (1.0f - k);
            piv_x = 0.14f + shake;
            piv_y = 1.04f + 0.02f * sinf(k * PI_F);
            ang = -1.95f - 0.06f * sinf(k * PI_F);
            blade_len = 0.78f;
        } else {
            float k = (prog - 0.58f) / 0.42f;
            float e = k * k * (3.0f - 2.0f * k); /* smoothstep */
            piv_x = 0.14f + (0.72f - 0.14f) * e;
            piv_y = 1.04f + (0.96f - 1.04f) * e;
            ang = -1.95f + (-0.22f - (-1.95f)) * e;
            blade_len = 0.78f;
        }
    }

    /* 1) Rastro de corte em arco carmesim + aco incandescente durante o cleave */
    if (swinging && prog >= 0.17f && prog <= 0.56f) {
        float slash_k = (prog - 0.17f) / 0.39f;
        float fade = (slash_k < 0.35f) ? (slash_k / 0.35f) : (1.0f - (slash_k - 0.35f) / 0.65f);
        float head_t = fminf(1.0f, (prog - 0.16f) / 0.28f);
        float tail_t = fmaxf(0.0f, head_t - 0.62f);

        int sy0 = (int)((float)rh * 0.14f);
        int sy1 = (int)((float)rh * 0.90f);
        int sx0 = (int)((float)rw * 0.06f);
        int sx1 = (int)((float)rw * 0.94f);
        for (int y = sy0; y < sy1; y++) {
            float ny = ((float)y + 0.5f) / (float)rh;
            for (int x = sx0; x < sx1; x++) {
                float nx = ((float)x + 0.5f) / (float)rw;
                /* Arco diagonal varrendo de cima-direita (0.86, 0.22) para baixo-esquerda (0.14, 0.78) */
                float arc_t = (0.88f - nx) / 0.76f;
                if (arc_t < tail_t || arc_t > head_t) continue;
                float curve_y = 0.22f + arc_t * 0.54f - sinf(arc_t * PI_F) * 0.14f;
                float dy = ny - curve_y;
                float thick = (0.045f + 0.085f * sinf(arc_t * PI_F)) * (0.45f + 0.55f * fade);
                if (fabsf(dy) > thick) continue;

                float edge_dist = fabsf(dy) / fmaxf(thick, 0.001f);
                float trail_pos = (arc_t - tail_t) / fmaxf(head_t - tail_t, 0.01f);
                float intensity = (1.0f - edge_dist * edge_dist) * (0.35f + 0.65f * trail_pos) * fade;
                if (intensity < 0.14f) continue;

                char sch = (edge_dist < 0.28f && trail_pos > 0.55f) ? '@'
                         : (edge_dist < 0.58f ? r->sample_ramp(0.55f + 0.42f * intensity, x, y) : '/');
                AsciiColor scol;
                if (edge_dist < 0.30f && trail_pos > 0.45f) {
                    scol = r->scale_rgb(255.0f, 238.0f, 230.0f, fminf(1.0f, intensity * 1.25f));
                } else if (edge_dist < 0.62f) {
                    scol = r->scale_rgb(240.0f, 45.0f, 38.0f, fminf(1.0f, intensity * 1.15f));
                } else {
                    scol = r->scale_rgb(135.0f, 16.0f, 22.0f, fminf(1.0f, intensity));
                }
                r->set_cell(x, y, sch, scol, black);
            }
        }
    }

    /* 2) Espada Matadora de Dragoes rotacionada em 2D real (restrita a caixa delimitadora) */
    float dir_x = sinf(ang);
    float dir_y = -cosf(ang);
    float perp_x = cosf(ang);
    float perp_y = sinf(ang);
    float inv_x_aspect = 1.65f / aspect;
    float reach_a0 = -0.22f * blade_len;
    float reach_a1 = 1.02f * blade_len;
    float pad_ac = 0.13f;
    float c0x = piv_x + (reach_a0 * dir_x - pad_ac * fabsf(perp_x)) * inv_x_aspect;
    float c1x = piv_x + (reach_a0 * dir_x + pad_ac * fabsf(perp_x)) * inv_x_aspect;
    float c2x = piv_x + (reach_a1 * dir_x - pad_ac * fabsf(perp_x)) * inv_x_aspect;
    float c3x = piv_x + (reach_a1 * dir_x + pad_ac * fabsf(perp_x)) * inv_x_aspect;
    float min_nx = fminf(fminf(c0x, c1x), fminf(c2x, c3x));
    float max_nx = fmaxf(fmaxf(c0x, c1x), fmaxf(c2x, c3x));
    float c0y = piv_y + reach_a0 * dir_y - pad_ac * fabsf(perp_y);
    float c1y = piv_y + reach_a0 * dir_y + pad_ac * fabsf(perp_y);
    float c2y = piv_y + reach_a1 * dir_y - pad_ac * fabsf(perp_y);
    float c3y = piv_y + reach_a1 * dir_y + pad_ac * fabsf(perp_y);
    float min_ny = fminf(fminf(c0y, c1y), fminf(c2y, c3y));
    float max_ny = fmaxf(fmaxf(c0y, c1y), fmaxf(c2y, c3y));

    int by0 = (int)floorf(min_ny * (float)rh) - 1;
    int by1 = (int)ceilf(max_ny * (float)rh) + 1;
    int bx0 = (int)floorf(min_nx * (float)rw) - 1;
    int bx1 = (int)ceilf(max_nx * (float)rw) + 1;
    if (by0 < 0) by0 = 0;
    if (by1 > rh) by1 = rh;
    if (bx0 < 0) bx0 = 0;
    if (bx1 > rw) bx1 = rw;

    for (int y = by0; y < by1; y++) {
        float ny = ((float)y + 0.5f) / (float)rh;
        float rel_y = ny - piv_y;
        for (int x = bx0; x < bx1; x++) {
            float nx = ((float)x + 0.5f) / (float)rw;
            /* Compensa aspecto da tela para manter espessura uniforme em qualquer angulo */
            float rel_x = (nx - piv_x) * (aspect / 1.65f);

            float along = (rel_x * dir_x + rel_y * dir_y) / blade_len;
            if (along < -0.22f || along > 1.02f) continue;

            float across = rel_x * perp_x + rel_y * perp_y;
            float abs_ac = fabsf(across);

            int part = 0;
            float half_w = 0.0f;
            if (along < 0.0f) {
                /* Cabo longo enrolado em couro + pomo pesado */
                half_w = (along < -0.17f) ? 0.036f : 0.022f;
                if (abs_ac <= half_w) part = 1;
            } else if (along < 0.065f) {
                /* Guarda-mao larga de ferro */
                half_w = 0.115f - fabsf(along - 0.032f) * 0.45f;
                if (abs_ac <= half_w) part = 2;
            } else if (along <= 1.00f) {
                /* Lamina colossal da Dragon Slayer: chapa larga + ponta triangular */
                float base_w = 0.088f - (along - 0.065f) * 0.018f;
                if (along > 0.84f) {
                    float tip_k = (along - 0.84f) / 0.16f;
                    half_w = base_w * (1.0f - tip_k);
                } else {
                    half_w = base_w;
                }
                if (abs_ac <= half_w && half_w > 0.002f) {
                    part = 3;
                }
            }

            if (part == 0) continue;

            float u_across = (half_w > 0.001f) ? (abs_ac / half_w) : 0.0f;
            char gch;
            AsciiColor color;

            if (part == 1) {
                /* Empunhadura escura com faixas de couro */
                int wrap = (((int)floorf((along + 0.25f) * 42.0f)) & 1);
                float lum = 0.30f + 0.32f * (1.0f - u_across) + (wrap ? 0.10f : 0.0f);
                gch = r->sample_ramp(lum, x, y);
                color = r->scale_rgb(92.0f, 52.0f, 44.0f, 0.72f + 0.28f * (1.0f - u_across));
            } else if (part == 2) {
                /* Guarda de ferro forjado com detalhe central */
                float lum = 0.45f + 0.38f * (1.0f - u_across);
                gch = r->sample_ramp(lum, x, y);
                color = r->scale_rgb(138.0f, 144.0f, 158.0f, 0.80f + 0.20f * (1.0f - u_across));
            } else {
                /* Lamina: nucleo de ferro bruto escuro + chanfros de aco brilhante nas bordas */
                int is_edge = (u_across > 0.66f);
                int is_ridge = (u_across < 0.16f);
                float glint = (swinging && prog >= 0.16f && prog <= 0.48f) ? 0.20f : 0.0f;
                if (is_edge) {
                    float lum = fminf(1.0f, 0.84f + glint);
                    gch = (u_across > 0.86f) ? '@' : r->sample_ramp(lum, x, y);
                    if (swinging && prog >= 0.18f && prog <= 0.46f && u_across > 0.86f) {
                        color = (AsciiColor){255, 195, 195};
                    } else {
                        color = (AsciiColor){228, 236, 248};
                    }
                } else if (is_ridge) {
                    gch = '|';
                    color = r->scale_rgb(165.0f, 174.0f, 190.0f, 0.85f);
                } else {
                    /* Ferro forjado escuro da Dragon Slayer com leve reflexo metalico */
                    float lum = 0.30f + 0.34f * (1.0f - u_across) + glint * 0.5f;
                    gch = r->sample_ramp(lum, x, y);
                    color = r->scale_rgb(96.0f, 106.0f, 124.0f, 0.76f + 0.28f * (1.0f - u_across));
                }
            }
            r->set_cell(x, y, gch, color, black);
        }
    }
    return 1;
}

/* Preenche os textos e a barra de municao/cooldown mostrados no HUD inferior */
void player_get_weapon_hud_info(const Player *p, PlayerWeaponHudInfo *out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!p) {
        return;
    }
    if (p->ability_state.berserk_time > 0.0f) {
        out->custom = 1;
        snprintf(out->label, sizeof(out->label), "ESPADA");
        snprintf(out->value, sizeof(out->value), "1.5m");
        out->bar_ratio = 1.0f - fminf(1.0f, p->shoot_timer / 0.60f);
        return;
    }
    int w = p->active_weapon;
    if (w < 0 || w >= PLAYER_MAX_WEAPONS) w = 0;
    int mag = player_weapon_mag_size(w);
    int cur = p->weapon_mag[w];
    out->custom = 1;
    snprintf(out->label, sizeof(out->label), "%s", player_weapon_name(w));
    if (w == 5) {
        snprintf(out->value, sizeof(out->value), "MELEE");
        out->bar_ratio = 1.0f;
        return;
    }
    if (player_weapon_has_infinite_reserve(w)) {
        snprintf(out->value, sizeof(out->value), "%02d/INF", cur);
    } else {
        snprintf(out->value, sizeof(out->value), "%02d/%03d", cur, p->weapon_reserve[w]);
    }
    out->bar_ratio = mag > 0 ? (float)cur / (float)mag : 0.0f;
}

/* Cor tematica e icone curto em ASCII de cada arma na torre lateral do HUD */
static Color weapon_tower_accent(int w, const char **out_icon) {
    switch (w) {
        case 0: if (out_icon) *out_icon = "==";  return (Color){125, 215, 255, 255};
        case 1: if (out_icon) *out_icon = "=]="; return (Color){255, 165, 65, 255};
        case 2: if (out_icon) *out_icon = "--+"; return (Color){135, 245, 115, 255};
        case 3: if (out_icon) *out_icon = "==="; return (Color){255, 215, 80, 255};
        case 4: if (out_icon) *out_icon = "=>="; return (Color){105, 245, 255, 255};
        case 5: if (out_icon) *out_icon = "(*)"; return (Color){65, 225, 238, 255};
        case 6: if (out_icon) *out_icon = "=o="; return (Color){235, 222, 165, 255};
        case 7: if (out_icon) *out_icon = "=#="; return (Color){240, 185, 95, 255};
        case 8: if (out_icon) *out_icon = "=*";  return (Color){255, 118, 48, 255};
        default: if (out_icon) *out_icon = "=="; return (Color){165, 190, 220, 255};
    }
}

/* Torre vertical de armas possuidas no canto inferior direito (logo acima da barra geral do HUD) */
static void draw_weapon_tower_hud(const Player *p, int screen_w, int screen_h) {
    int owned[PLAYER_MAX_WEAPONS];
    int count = 0;
    for (int w = 0; w < PLAYER_MAX_WEAPONS; w++) {
        if (player_weapon_has_ammo(p, w)) {
            owned[count++] = w;
        }
    }
    int berserk = (p->ability_state.berserk_time > 0.0f);
    if (count <= 0 && !berserk) {
        return;
    }

    int hud_h = (int)((float)screen_h * 0.145f);
    if (hud_h < 82) hud_h = 82;
    int hud_y = screen_h - hud_h;

    float sc = (float)screen_h / 720.0f;
    if (sc < 0.72f) sc = 0.72f;
    if (sc > 1.35f) sc = 1.35f;

    int total_rows = count + (berserk ? 1 : 0);
    int top_limit = (int)(232.0f * sc);
    if (top_limit > hud_y - 90) top_limit = 82;
    int avail_h = (hud_y - 10) - top_limit - 18;
    if (avail_h < 60) avail_h = 60;

    int gap = (int)(4.0f * sc);
    if (gap < 3) gap = 3;
    int row_h = (int)(28.0f * sc);
    if (row_h < 20) row_h = 20;
    if (total_rows * (row_h + gap) > avail_h && total_rows > 0) {
        row_h = (avail_h / total_rows) - gap;
        if (row_h < 17) row_h = 17;
    }

    int card_w = (int)(192.0f * sc);
    if (card_w < 146) card_w = 146;
    if (card_w > 236) card_w = 236;
    int right_margin = 12;
    int base_x = screen_w - card_w - right_margin;
    int bottom_y = hud_y - 10;
    int tower_h = total_rows * row_h + (total_rows - 1) * gap;
    int start_y = bottom_y - tower_h;

    int hdr_fs = (row_h <= 19) ? 9 : ((screen_w < 1000) ? 10 : 11);
    const char *hdr = "ARSENAL [SCROLL]";
    int hdr_w = MeasureText(hdr, hdr_fs);
    DrawRectangle(screen_w - hdr_w - right_margin - 12, start_y - hdr_fs - 8,
                  hdr_w + 12, hdr_fs + 5, (Color){10, 14, 22, 195});
    DrawText(hdr, screen_w - hdr_w - right_margin - 6, start_y - hdr_fs - 6,
             hdr_fs, (Color){145, 168, 198, 230});

    int fs_name = (row_h <= 20) ? 10 : ((int)(12.5f * sc) < 11 ? 11 : (int)(12.5f * sc));
    int fs_ammo = fs_name;

    int draw_idx = 0;
    if (berserk) {
        int rx = base_x - 7;
        int rw = card_w + 7;
        int ry = start_y;
        Color acc = (Color){255, 78, 68, 255};
        DrawRectangle(rx, ry, rw, row_h, (Color){38, 14, 18, 242});
        DrawRectangle(rx, ry, 4, row_h, acc);
        DrawRectangleLinesEx((Rectangle){(float)rx, (float)ry, (float)rw, (float)row_h}, 1.5f, acc);
        int ty = ry + (row_h - fs_name) / 2;
        DrawText(">> ESPADA", rx + 10, ty, fs_name, (Color){255, 220, 210, 255});
        const char *bt = TextFormat("%.1fs", p->ability_state.berserk_time);
        DrawText(bt, rx + rw - MeasureText(bt, fs_ammo) - 8, ty, fs_ammo, acc);
        draw_idx++;
    }

    for (int i = 0; i < count; i++, draw_idx++) {
        int w = owned[i];
        int is_active = (!berserk && p->active_weapon == w);
        const char *icon = "==";
        Color acc = weapon_tower_accent(w, &icon);

        int shift = is_active ? 7 : 0;
        int rx = base_x - shift;
        int rw = card_w + shift;
        int ry = start_y + draw_idx * (row_h + gap);

        Color bg = is_active ? (Color){22, 29, 42, 240} : (Color){12, 16, 24, 205};
        Color bd = is_active ? acc : (Color){48, 58, 76, 205};
        Color bar_c = is_active ? acc : (Color){(unsigned char)(acc.r * 0.62f),
                                                (unsigned char)(acc.g * 0.62f),
                                                (unsigned char)(acc.b * 0.62f), 220};

        DrawRectangle(rx, ry, rw, row_h, bg);
        DrawRectangle(rx, ry, is_active ? 4 : 3, row_h, bar_c);
        DrawRectangleLinesEx((Rectangle){(float)rx, (float)ry, (float)rw, (float)row_h},
                             is_active ? 1.5f : 1.0f, bd);

        int mag = player_weapon_mag_size(w);
        int cur_mag = p->weapon_mag[w];
        int res = p->weapon_reserve[w];

        /* Barra fina de municao na base do card ativo */
        if (is_active && w != 5 && mag > 0) {
            float ratio = (float)cur_mag / (float)mag;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            int meter_w = rw - 10;
            int fill_w = (int)((float)meter_w * ratio);
            DrawRectangle(rx + 6, ry + row_h - 3, meter_w, 2, (Color){34, 42, 56, 230});
            if (fill_w > 0) {
                DrawRectangle(rx + 6, ry + row_h - 3, fill_w, 2,
                              cur_mag > 0 ? acc : (Color){245, 78, 68, 255});
            }
        }

        int ty = ry + (row_h - fs_name) / 2 - (is_active ? 1 : 0);
        char left_txt[40];
        snprintf(left_txt, sizeof(left_txt), "%d %s %s", i + 1, icon, player_weapon_name(w));

        char ammo_txt[24];
        Color ammo_col;
        if (w == 5) {
            snprintf(ammo_txt, sizeof(ammo_txt), "MELEE");
            ammo_col = is_active ? acc : (Color){145, 200, 210, 215};
        } else if (player_weapon_has_infinite_reserve(w)) {
            if (cur_mag <= 0) {
                snprintf(ammo_txt, sizeof(ammo_txt), "[R]");
                ammo_col = (Color){255, 115, 85, 255};
            } else {
                snprintf(ammo_txt, sizeof(ammo_txt), "%02d/INF", cur_mag);
                ammo_col = is_active ? (Color){230, 242, 255, 255} : (Color){155, 175, 198, 215};
            }
        } else if (cur_mag <= 0) {
            snprintf(ammo_txt, sizeof(ammo_txt), "[R]/%d", res);
            ammo_col = (Color){255, 105, 75, 255};
        } else {
            snprintf(ammo_txt, sizeof(ammo_txt), "%02d/%03d", cur_mag, res);
            ammo_col = is_active ? (Color){255, 224, 115, 255} : (Color){175, 188, 205, 215};
        }

        int ammo_w = MeasureText(ammo_txt, fs_ammo);
        int max_left_w = rw - ammo_w - 20;
        int l_fs = fs_name;
        while (l_fs > 9 && MeasureText(left_txt, l_fs) > max_left_w) {
            l_fs--;
        }
        Color name_col = is_active ? WHITE : (Color){182, 196, 215, 220};
        DrawText(left_txt, rx + 9, ry + (row_h - l_fs) / 2 - (is_active ? 1 : 0), l_fs, name_col);
        DrawText(ammo_txt, rx + rw - ammo_w - 8, ty, fs_ammo, ammo_col);
    }
}

/* Desenha o painel de status da habilidade, contador de kills, torre de armas e borda Berserk */
void player_draw_ability_hud(const Player *p, int screen_w, int screen_h) {
    if (!p) {
        return;
    }
    const AbilityState *a = &p->ability_state;
    const char *status = "SEM HABILIDADE";
    Color accent = {190, 205, 220, 255};
    if (p->ability == ABILITY_TETO) {
        accent = (Color){255, 115, 180, 255};
        status = player_attack_damage(p, 100) > 100
            ? TextFormat("DANO +25%% | VEL +35%% | %.1fs", a->zone_time)
            : (a->zone_time > 0
                ? TextFormat("CORACOES %.1fs | RECARGA %.0fs", a->zone_time, ceilf(a->cooldown))
                : (a->cooldown > 0
                    ? TextFormat("CORACOES: RECARGA %.0fs", ceilf(a->cooldown))
                    : "[E] CORACOES: PRONTO"));
    } else if (p->ability == ABILITY_SHADOW) {
        accent = (Color){185, 125, 245, 255};
        status = a->cooldown > 0
            ? TextFormat("FOGO NEGRO: %.0fs", ceilf(a->cooldown))
            : "[E] FOGO NEGRO: PRONTO";
    } else if (p->ability == ABILITY_GUERRILLA) {
        accent = (Color){175, 225, 115, 255};
        status = a->guerrilla_time >= GUERRILLA_CHARGE_TIME
            ? "GUERRILHEIRO: PROXIMO TIRO x2"
            : TextFormat("GUERRILHEIRO: %.1fs SEM DANO", GUERRILLA_CHARGE_TIME - a->guerrilla_time);
    } else if (p->ability == ABILITY_BERSERK) {
        accent = (Color){255, 95, 80, 255};
        status = a->berserk_time > 0
            ? TextFormat("BERSERK %.1fs | ESPADA | DANO RECEBIDO x2", a->berserk_time)
            : (a->berserk_used
                ? "BERSERK: JA UTILIZADO"
                : (a->kills >= BERSERK_KILLS
                    ? "[E] BERSERK: PRONTO"
                    : TextFormat("BERSERK: %d/5 KILLS", a->kills)));
    }
    int f = screen_w < 1000 ? 12 : 16;
    int width = MeasureText(status, f) + 24;
    DrawRectangle(screen_w - width - 12, 12, width, f + 14, (Color){12, 14, 22, 225});
    DrawText(status, screen_w - width, 19, f, accent);
    const char *kills = TextFormat("KILLS: %d", a->kills);
    int kw = MeasureText(kills, f) + 20;
    DrawRectangle(screen_w - kw - 12, f + 29, kw, f + 10, (Color){12, 14, 22, 220});
    DrawText(kills, screen_w - kw - 2, f + 34, f, WHITE);
    draw_weapon_tower_hud(p, screen_w, screen_h);
    if (a->berserk_time > 0) {
        DrawRectangleLinesEx((Rectangle){2, 2, (float)screen_w - 4, (float)screen_h - 4},
                             4, (Color){190, 20, 30, 220});
    }
}
