#ifndef PLAYER_H
#define PLAYER_H

#include "config.h"
#include "map.h"
#include "render_api.h"

/*
 * MODULO 2: PERSONAGENS, CRIADOR DE PERSONAGEM E FISICA
 * (Responsavel: Strazzer - branch strazzer)
 * Responsavel pelos personagens selecionaveis, atributos,
 * movimentacao com colisao por Delta Time, mouse look e disparos.
 */

#define PRESET_CLASS_COUNT 5
#define PRESET_FUZILEIRO 0
#define PRESET_KASANE_TETO 1
#define PRESET_SHADOW_HERALD 2
#define PRESET_GUTS 3
#define PRESET_RAMBO 4

/* Dados visuais independentes de Raylib; o renderer apenas os consome. */
typedef struct { unsigned char r, g, b, a; } CharacterColor;
typedef struct {
    int custom_sprite; /* 0 usa o soldado original do motor. */
    int portrait_rows, portrait_cols;
    float portrait_crop_x, portrait_crop_y;
    const char *description;
} CharacterVisual;

typedef enum {
    PART_HEAD, PART_BODY, PART_HAIR, PART_OUTFIT,
    PART_HAIR_COLOR, PART_OUTFIT_COLOR, PART_SKIN, CHARACTER_PART_COUNT
} CharacterPart;
typedef struct {
    int enabled;
    int choice[CHARACTER_PART_COUNT];
} CharacterAppearance;

typedef enum { ABILITY_NONE, ABILITY_TETO, ABILITY_SHADOW, ABILITY_BERSERK, ABILITY_GUERRILLA } AbilityType;
#define GUERRILLA_CHARGE_TIME 5.0f
#define TETO_ZONE_DURATION 10.0f
#define TETO_ZONE_RADIUS 3.0f
#define ABILITY_COOLDOWN 30.0f
#define BERSERK_DURATION 10.0f
#define BERSERK_KILLS 5
#define BERSERK_WEAPON 4
typedef struct {
    int kills, berserk_used, saved_weapon;
    float guerrilla_time;
    int boosted_shot;
    float cooldown, berserk_time, zone_time, zone_x, zone_y;
    int fireball_active;
    float fireball_x, fireball_y, fireball_dx, fireball_dy, fireball_time;
} AbilityState;

void player_appearance_init(CharacterAppearance *appearance);
int player_part_count(int part);
const char *player_part_label(int part);
const char *player_part_option(int part, int choice);
int player_appearance_valid(const CharacterAppearance *appearance);
CharacterVisual player_appearance_visual(int visual_id, const CharacterAppearance *appearance);
int player_appearance_material(int visual_id, const CharacterAppearance *appearance,
                               float x, float y, int back_view);

typedef struct {
    char name[32];
    int max_health;
    int start_ammo;
    float move_speed;
    int start_weapon;
    int visual_id;
    CharacterAppearance appearance;
    AbilityType ability;
} CharacterClass;

#define MAX_CUSTOM_CHARACTERS 8
typedef struct {
    CharacterClass custom[MAX_CUSTOM_CHARACTERS];
    int count;
} CharacterRoster;

void player_roster_init(CharacterRoster *roster);
int player_roster_count(const CharacterRoster *roster);
CharacterClass player_roster_get(const CharacterRoster *roster, int index);
/* Retorna indice criado ou -1 para dados invalidos/limite atingido. */
int player_roster_add(CharacterRoster *roster, const CharacterClass *draft);

#define PLAYER_MAX_WEAPONS 9

typedef struct {
    int id;
    char class_name[32];
    int visual_id;
    CharacterAppearance appearance;
    AbilityType ability;
    AbilityState ability_state;
    float x;
    float y;
    float angle;
    float fov;
    float move_speed;
    int health;
    int max_health;
    int ammo;
    int active_weapon;
    int weapon_owned[PLAYER_MAX_WEAPONS];
    int weapon_mag[PLAYER_MAX_WEAPONS];
    int weapon_reserve[PLAYER_MAX_WEAPONS];
    int score;
    float bob_phase;
    float shoot_timer;
    float hurt_timer;
    int last_damage;
} Player;

int player_weapon_mag_size(int weapon_type);
float player_weapon_cooldown(int weapon_type);
const char *player_weapon_name(int weapon_type);
int player_weapon_has_infinite_reserve(int weapon_type);
int player_pickup_weapon(Player *p, int weapon_type, int ammo_amount);
int player_reload(Player *p);
int player_switch_weapon(Player *p, int weapon_type);
int player_cycle_weapon(Player *p, int direction);

void player_tick_ability(Player *p, float dt);
void player_register_kill(Player *p, float x, float y);
int player_activate_ability(Player *p);
float player_effective_speed(const Player *p);
int player_attack_damage(const Player *p, int base_damage);
void player_take_damage(Player *p, int damage);
int player_begin_attack(Player *p);
int player_render_material(const Player *p, float x, float y, int back_view);

/* Dados opcionais para sobrescrever o cartao de municao/arma no HUD (ex.: Espada Berserk). */
typedef struct {
    int custom;        /* 0 = padrao MUNICAO do renderer; 1 = usa os campos abaixo */
    char label[16];
    char value[16];
    float bar_ratio;
} PlayerWeaponHudInfo;

void player_get_weapon_hud_info(const Player *p, PlayerWeaponHudInfo *out);
void player_render_abilities(const Player *p, const RendererApi *r);
/* Retorna 1 se desenhou arma customizada em 1a pessoa (ex.: Espada Berserk), 0 para usar a padrao. */
int player_render_weapon(const Player *p, const RendererApi *r);
void player_draw_ability_hud(const Player *p, int screen_w, int screen_h);

/* Retorna um personagem; indices invalidos retornam Fuzileiro. */
CharacterClass player_get_preset_class(int index);
CharacterVisual player_get_visual(int visual_id);
CharacterColor player_get_material_color(int material);

/* Silhueta ASCII da Teto: x em [-1,1], y em [0,1].
 * Materiais: 0 vazio, 1 roupa, 2 pele, 3 cabelo, 4 botas/contorno,
 * 6 sombra/iris, 7 branco dos olhos, 8 blush, 9 brilho do cabelo,
 * 15 metal, 18 meio-tom do cabelo, 19 tecido iluminado, 20 sombra da pele. */
int player_teto_material(float x, float y, int back_view);
int player_character_material(int preset, float x, float y, int back_view);

/* Aplica os atributos de uma classe (base ou customizada) ao jogador. */
void player_apply_class(Player *p, const CharacterClass *cls);

/* Inicializa o jogador com posicao, angulo e atributos da classe padrao. */
void player_init(Player *p, float x, float y, float angle);

/*
 * Move o jogador considerando colisao por eixo (permite deslizar nas paredes).
 * forward: deslocamento para frente (>0) ou tras (<0)
 * strafe:  deslocamento lateral para direita (>0) ou esquerda (<0)
 * Retorna 1 se houve movimento em algum eixo, 0 se bloqueado.
 */
int player_move(Player *p, const GameMap *map, float forward, float strafe);

/* Gira o angulo de visao do jogador e normaliza entre 0 e 2*PI. */
void player_rotate(Player *p, float delta_angle);

/*
 * Processa teclado (WASD/Shift/R/ESC) e mouse (Mouse Look + Clique Esquerdo)
 * multiplicando todas as velocidades por dt (Delta Time).
 */
void player_handle_input(Player *p, const GameMap *map, float dt,
                         int *running, int *did_shoot);

#endif /* PLAYER_H */
