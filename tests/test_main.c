/* test_main.c - suite de testes unitarios de mapa, fisica, personagens, combate e rede */

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "config.h"
#include "map.h"
#include "network.h"
#include "player.h"
#include "entities.h"

/* Contadores globais da execucao dos testes */
static int tests_run = 0;
static int tests_failed = 0;

/* Macro simples de assercao que imprime o resultado e conta falhas sem abortar */
#define ASSERT_TRUE(cond, msg)                                                \
    do {                                                                      \
        tests_run++;                                                          \
        if (!(cond)) {                                                        \
            tests_failed++;                                                   \
            printf("  [FALHOU] %s (linha %d)\n", (msg), __LINE__);            \
        } else {                                                              \
            printf("  [OK]     %s\n", (msg));                                 \
        }                                                                     \
    } while (0)

/* Verifica dimensoes e paredes padrao do mapa embutido de fallback */
static void test_map_default(void) {
    printf("\nTeste 1: Inicializacao do mapa padrao\n");
    GameMap map;
    map_init_default(&map);

    ASSERT_TRUE(map.width == MAP_WIDTH, "Largura do mapa igual a MAP_WIDTH");
    ASSERT_TRUE(map.height == MAP_HEIGHT, "Altura do mapa igual a MAP_HEIGHT");
    ASSERT_TRUE(map_is_wall(&map, 0, 0) == 1, "Canto (0,0) e parede");
    ASSERT_TRUE(map_is_wall(&map, 1, 1) == 0, "Posicao interna (1,1) e livre");
}

/* Testa leitura de arquivo .txt e tratamento seguro quando o arquivo nao existe */
static void test_map_file_loading(void) {
    printf("\nTeste 2: Carregamento de mapa via arquivo\n");
    GameMap map;

    int ok_missing = map_load_from_file(&map, "assets/maps/nao_existe.txt");
    ASSERT_TRUE(ok_missing == 0, "Retorna 0 para arquivo inexistente");

    int ok_valid = map_load_from_file(&map, "assets/maps/level1.txt");
    ASSERT_TRUE(ok_valid == 1, "Carrega assets/maps/level1.txt com sucesso");
    ASSERT_TRUE(map_get_tile(&map, 0, 0) == '#', "Borda (0,0) do level1.txt e '#'");
    ASSERT_TRUE(map_is_wall(&map, 4, 4) == 1, "Obstaculo em (4,4) e parede");
    ASSERT_TRUE(map_is_wall(&map, 2, 2) == 0, "Corredor em (2,2) e livre");
}

/* Garante que coordenadas fora da grade retornem como parede sem invadir memoria */
static void test_map_bounds(void) {
    printf("\nTeste 3: Seguranca de limites do mapa\n");
    GameMap map;
    map_init_default(&map);

    ASSERT_TRUE(map_is_wall(&map, -1, 5) == 1, "Coordenada X negativa tratada como parede");
    ASSERT_TRUE(map_is_wall(&map, 5, -1) == 1, "Coordenada Y negativa tratada como parede");
    ASSERT_TRUE(map_is_wall(&map, MAP_WIDTH, 5) == 1, "Coordenada X >= width tratada como parede");
    ASSERT_TRUE(map_is_wall(&map, 5, MAP_HEIGHT) == 1, "Coordenada Y >= height tratada como parede");
}

/* Testa deslocamento livre e travamento contra paredes no mapa padrao */
static void test_player_movement_and_collision(void) {
    printf("\nTeste 4: Movimentacao e colisao do jogador\n");
    GameMap map;
    Player p;
    map_init_default(&map);
    player_init(&p, 2.5f, 1.5f, 0.0f);

    int moved_free = player_move(&p, &map, 0.25f, 0.0f);
    ASSERT_TRUE(moved_free == 1, "Jogador move em espaco livre");
    ASSERT_TRUE(p.x > 2.7f, "Coordenada X avancou corretamente");

    /* Encosta perto da parede esquerda (coluna 0) e tenta atravessar */
    player_init(&p, 1.25f, 1.5f, PI_F); /* Olhando para -X */
    for (int i = 0; i < 10; i++) {
        player_move(&p, &map, 0.2f, 0.0f);
    }
    ASSERT_TRUE(p.x >= 1.0f, "Colisao impede jogador de atravessar a parede X=0");
}

/* Garante que player_rotate mantem o angulo sempre no intervalo [0, 2*PI) */
static void test_player_rotation(void) {
    printf("\nTeste 5: Rotacao e normalizacao de angulo\n");
    Player p;
    player_init(&p, 2.5f, 2.5f, 0.0f);

    player_rotate(&p, -0.5f);
    ASSERT_TRUE(p.angle >= 0.0f && p.angle < 2.0f * PI_F, "Angulo negativo normalizado para [0, 2*PI)");

    player_rotate(&p, 3.0f * PI_F);
    ASSERT_TRUE(p.angle >= 0.0f && p.angle < 2.0f * PI_F, "Angulo grande normalizado para [0, 2*PI)");
}

/* Testa modo offline e troca real de pacote NetPacket via UDP em 127.0.0.1 */
static void test_network_udp_loopback(void) {
    printf("\nTeste 6: Rede offline e comunicacao UDP loopback\n");
    NetContext offline;
    Player p1, p2;
    player_init(&p1, 4.5f, 5.5f, 1.0f);
    player_init(&p2, 10.5f, 10.5f, 2.0f);
    p1.id = 1;
    p2.id = 2;

    ASSERT_TRUE(net_init(&offline, NET_MODE_OFFLINE, NULL, 0) == 1, "Modo offline inicializa com sucesso");
    ASSERT_TRUE(net_send_state(&offline, &p1, 0) == 0, "Modo offline nao envia pacotes");
    net_close(&offline);

    NetContext host, client;
    int test_port = 15432;
    int host_ok = net_init(&host, NET_MODE_HOST, NULL, test_port);
    int client_ok = net_init(&client, NET_MODE_CLIENT, "127.0.0.1", test_port);

    ASSERT_TRUE(host_ok == 1, "Host UDP abre socket na porta de teste");
    ASSERT_TRUE(client_ok == 1, "Client UDP configura destino 127.0.0.1");

    if (host_ok && client_ok) {
        int sent = net_send_state(&client, &p2, 1);
        ASSERT_TRUE(sent == 1, "Client envia pacote NetPacket via UDP");

#ifdef _WIN32
        Sleep(15);
#endif
        int got = net_receive_state(&host);
        ASSERT_TRUE(got == 1, "Host recebe pacote UDP do client");
        ASSERT_TRUE(host.remote_state.player_id == 2, "Player ID recebido corresponde ao client");
        ASSERT_TRUE(fabsf(host.remote_state.x - 10.5f) < 0.01f, "Posicao X sincronizada corretamente");
        ASSERT_TRUE(host.remote_state.shooting == 1, "Flag de tiro sincronizada corretamente");
    }

    net_close(&client);
    net_close(&host);
}

/* Verifica selecao da Kasane Teto, preservacao de spawn/id e fallback de indice invalido */
static void test_teto_selection(void) {
    Player p;
    CharacterClass teto = player_get_preset_class(PRESET_KASANE_TETO);
    player_init(&p, 2.5f, 4.5f, 0.0f);
    ASSERT_TRUE(strcmp(p.class_name, "Fuzileiro") == 0,
                "Fuzileiro e o personagem padrao");
    ASSERT_TRUE(p.health == 100 && p.ammo == 30 && fabsf(p.move_speed - 4.2f) < 0.001f,
                "Fuzileiro inicia com os atributos originais");
    p.id = 2;
    player_apply_class(&p, &teto);
    ASSERT_TRUE(strcmp(p.class_name, "Kasane Teto") == 0, "Selecao aplica Kasane Teto");
    ASSERT_TRUE(p.health == 100 && p.max_health == 100 && p.ammo == 30,
                "Teto inicia com vida e municao corretas");
    ASSERT_TRUE(fabsf(p.move_speed - 4.2f) < 0.001f && p.active_weapon == 0,
                "Teto recebe velocidade e arma inicial");
    ASSERT_TRUE(p.id == 2 && p.x == 2.5f && p.y == 4.5f,
                "Selecao preserva ID multiplayer e ponto de spawn");
    CharacterClass fallback = player_get_preset_class(PRESET_CLASS_COUNT);
    ASSERT_TRUE(strcmp(fallback.name, "Fuzileiro") == 0, "Indice invalido usa Fuzileiro");
    ASSERT_TRUE(player_teto_material(2.0f, 0.2f, 0) == 0,
                "Silhueta fora dos limites e transparente");
}

/* Verifica atributos do Arauto das Sombras e detalhes da silhueta frontal/traseira */
static void test_shadow_selection(void) {
    Player p;
    player_init(&p, 2.5f, 4.5f, 0.0f);
    CharacterClass cls = player_get_preset_class(PRESET_SHADOW_HERALD);
    player_apply_class(&p, &cls);
    ASSERT_TRUE(strcmp(p.class_name, "Arauto das Sombras") == 0,
                "Arauto das Sombras disponivel na selecao");
    ASSERT_TRUE(p.health == 100 && p.max_health == 100 && p.ammo == 30 &&
                fabsf(p.move_speed - 4.2f) < 0.001f, "Atributos do Arauto aplicados");
    ASSERT_TRUE(player_character_material(PRESET_SHADOW_HERALD, 0.06f, 0.17f, 0) == 17 &&
                player_character_material(PRESET_SHADOW_HERALD, 0.06f, 0.17f, 1) != 17,
                "Olhos vermelhos aparecem apenas na vista frontal");
    cls = player_get_preset_class(PRESET_KASANE_TETO);
    player_apply_class(&p, &cls);
    ASSERT_TRUE(p.max_health == 100 && strcmp(p.class_name, "Kasane Teto") == 0,
                "Trocar de volta para Teto restaura seus atributos");
}

/* Verifica selecao do Guts e desenho da espada na silhueta frontal e de costas */
static void test_guts_selection(void) {
    Player p;
    player_init(&p, 2.5f, 4.5f, 0.0f);
    CharacterClass cls = player_get_preset_class(PRESET_GUTS);
    player_apply_class(&p, &cls);
    ASSERT_TRUE(strcmp(p.class_name, "Guts") == 0 && p.health == 100 && p.max_health == 100,
                "Guts selecionavel com 100 de vida");
    ASSERT_TRUE(p.ammo == 30 && fabsf(p.move_speed - 4.2f) < 0.001f,
                "Guts recebe municao e velocidade corretas");
    ASSERT_TRUE(player_character_material(PRESET_GUTS, -0.60f, 0.10f, 0) != 0 &&
                player_character_material(PRESET_GUTS, -0.60f, 0.10f, 1) != 0,
                "Espada aparece na silhueta frontal e traseira de Guts");
    ASSERT_TRUE(player_character_material(PRESET_GUTS, 2.0f, 0.2f, 0) == 0,
                "Silhueta de Guts respeita limites");
}

/* Valida metadados e cores de todos os personagens base e fallbacks de visual/material invalidos */
static void test_character_visual_api(void) {
    Player p;
    player_init(&p, 2.5f, 4.5f, 0.0f);
    CharacterClass cls = player_get_preset_class(PRESET_KASANE_TETO);
    snprintf(cls.name, sizeof(cls.name), "Nome personalizado");
    player_apply_class(&p, &cls);
    CharacterVisual visual = player_get_visual(p.visual_id);
    ASSERT_TRUE(p.visual_id == PRESET_KASANE_TETO && visual.custom_sprite &&
                visual.portrait_rows == 28, "Visual independe do nome do personagem");
    cls = player_get_preset_class(PRESET_FUZILEIRO);
    player_apply_class(&p, &cls);
    ASSERT_TRUE(!player_get_visual(p.visual_id).custom_sprite,
                "Fuzileiro preserva renderizacao original");
    cls.visual_id = -1;
    player_apply_class(&p, &cls);
    ASSERT_TRUE(p.visual_id == PRESET_FUZILEIRO && !player_get_visual(999).custom_sprite,
                "Visual invalido usa o soldado padrao");
    ASSERT_TRUE(player_get_material_color(-1).a == 0 && player_get_material_color(999).a == 0,
                "Material invalido retorna cor transparente");
    ASSERT_TRUE(player_character_material(-1, 0.0f, 0.2f, 0) == 0,
                "Visual invalido nao recebe silhueta de outro personagem");
    int valid = 1;
    for (int id = 0; id < PRESET_CLASS_COUNT; id++) {
        CharacterVisual v = player_get_visual(id);
        if (v.portrait_rows <= 0 || v.portrait_cols <= 0 || !v.description) valid = 0;
        for (int back = 0; back < 2; back++) {
            for (int y = 0; y < 100; y++) {
                for (int x = -100; x <= 100; x++) {
                    int mat = player_character_material(id, x / 100.0f, y / 100.0f, back);
                    if (mat && player_get_material_color(mat).a == 0) valid = 0;
                }
            }
        }
    }
    ASSERT_TRUE(valid, "Todos os personagens possuem metadados e materiais validos");
}

/* Testa criacao de skins no CharacterRoster, validacao de nome/visual e limite de slots */
static void test_character_creator(void) {
    CharacterRoster roster;
    player_roster_init(&roster);
    ASSERT_TRUE(player_roster_count(&roster) == PRESET_CLASS_COUNT,
                "Criador inicia sem substituir personagens base");
    CharacterClass draft = player_get_preset_class(PRESET_KASANE_TETO);
    snprintf(draft.name, sizeof(draft.name), "   ");
    ASSERT_TRUE(player_roster_add(&roster, &draft) == -1 && roster.count == 0,
                "Nome vazio nao cria personagem");
    snprintf(draft.name, sizeof(draft.name), "  Minha personagem  ");
    draft.max_health = 180;
    draft.move_speed = 5.2f;
    int id = player_roster_add(&roster, &draft);
    CharacterClass saved = player_roster_get(&roster, id);
    Player p;
    player_init(&p, 2.5f, 4.5f, 0);
    player_apply_class(&p, &saved);
    ASSERT_TRUE(id == PRESET_CLASS_COUNT && !strcmp(p.class_name, "Minha personagem") &&
                p.health == 100 && p.ammo == 30 && p.active_weapon == 0 &&
                p.visual_id == PRESET_KASANE_TETO && fabsf(p.move_speed - 4.2f) < 0.001f,
                "Skin preserva nome e visual, mas usa atributos do Fuzileiro");
    draft.visual_id = 999;
    ASSERT_TRUE(player_roster_add(&roster, &draft) == -1 && roster.count == 1,
                "Visual invalido nao altera catalogo");
    draft.visual_id = PRESET_KASANE_TETO;
    for (int i = 1; i < MAX_CUSTOM_CHARACTERS; i++) player_roster_add(&roster, &draft);
    ASSERT_TRUE(player_roster_count(&roster) == PRESET_CLASS_COUNT + MAX_CUSTOM_CHARACTERS &&
                player_roster_add(&roster, &draft) == -1,
                "Limite de criacoes respeitado sem sobrescrever personagens");
    saved = player_roster_get(&roster, -1);
    ASSERT_TRUE(!strcmp(saved.name, "Fuzileiro"), "Selecao invalida preserva padrao");
}

/* Gera um hash FNV-1a da silhueta para comparar se trocar uma peca mudou o desenho */
static unsigned appearance_fingerprint(const CharacterAppearance *a, int back) {
    unsigned hash = 2166136261u;
    for (int y = 0; y < 100; y++) for (int x = -80; x <= 80; x++) {
        int mat = player_appearance_material(PRESET_FUZILEIRO, a, x / 100.0f, y / 100.0f, back);
        hash = (hash ^ (unsigned)mat) * 16777619u;
    }
    return hash;
}

/* Testa todas as opcoes do criador modular e verifica que nao vaza para presets base */
static void test_modular_appearance(void) {
    CharacterClass draft = player_get_preset_class(PRESET_FUZILEIRO);
    player_appearance_init(&draft.appearance);
    unsigned base = appearance_fingerprint(&draft.appearance, 0);
    int different = 1, valid = 1;
    for (int part = 0; part < CHARACTER_PART_COUNT; part++) {
        CharacterAppearance a = draft.appearance;
        int initial = a.choice[part];
        for (int option = 0; option < player_part_count(part); option++) {
            a.choice[part] = option;
            if (option != initial && appearance_fingerprint(&a, 0) == base) different = 0;
            if (!player_part_option(part, option)[0] || !player_appearance_valid(&a)) valid = 0;
            for (int back = 0; back < 2; back++) {
                for (int y = 0; y < 100; y++) for (int x = -80; x <= 80; x += 2) {
                    int mat = player_appearance_material(0, &a, x / 100.0f, y / 100.0f, back);
                    if (mat && !player_get_material_color(mat).a) valid = 0;
                }
            }
        }
    }
    ASSERT_TRUE(different, "Cada opcao de peca altera o visual independentemente");
    ASSERT_TRUE(valid, "Pecas possuem nomes e cores validas na frente e nas costas");
    CharacterRoster roster;
    player_roster_init(&roster);
    draft.appearance.choice[PART_HEAD] = 2;
    draft.appearance.choice[PART_HAIR] = 4;
    draft.appearance.choice[PART_OUTFIT] = 2;
    draft.appearance.choice[PART_OUTFIT_COLOR] = 5;
    int id = player_roster_add(&roster, &draft);
    CharacterClass saved = player_roster_get(&roster, id);
    Player p;
    player_init(&p, 2.5f, 4.5f, 0);
    player_apply_class(&p, &saved);
    ASSERT_TRUE(id >= PRESET_CLASS_COUNT &&
                memcmp(&p.appearance, &draft.appearance, sizeof(p.appearance)) == 0 &&
                appearance_fingerprint(&p.appearance, 0) == appearance_fingerprint(&draft.appearance, 0),
                "Combinacao salva chega intacta ao jogador");
    ASSERT_TRUE(player_appearance_visual(p.visual_id, &p.appearance).custom_sprite,
                "Avatar modular utiliza renderizacao propria no HUD e terceira pessoa");
    draft.appearance.choice[PART_HAIR] = 999;
    ASSERT_TRUE(player_roster_add(&roster, &draft) == -1, "Criador rejeita indice de peca invalido");
    CharacterClass preset = player_get_preset_class(PRESET_GUTS);
    player_apply_class(&p, &preset);
    ASSERT_TRUE(!p.appearance.enabled &&
                player_appearance_material(p.visual_id, &p.appearance, -0.60f, 0.10f, 0) ==
                player_character_material(PRESET_GUTS, -0.60f, 0.10f, 0),
                "Trocar para personagem base restaura seu visual original");
}

/* Monta uma sala limpa com 1 inimigo parado na frente do jogador para testes de combate */
static void prepare_combat(Player *p, EnemyManager *mgr, GameMap *map, int preset) {
    map_init_default(map);
    for (int y=1;y<map->height-1;y++) for(int x=1;x<map->width-1;x++) map->tiles[y][x]='.';
    player_init(p, 2.5f, 4.5f, 0);
    CharacterClass cls=player_get_preset_class(preset);
    player_apply_class(p,&cls);
    memset(mgr,0,sizeof(*mgr));
    mgr->count=1;
    mgr->enemies[0]=(Enemy){
        .active=1, .x=3.5f, .y=4.5f, .health=60, .max_health=60,
        .damage=10, .speed=0, .symbol='Z'
    };
}

/* Testa projeteis do Imp (bola de fogo), Kushan (flecha) e Cacodemon (orbe teleguiado) */
static void test_enemy_projectiles(void) {
    Player p; EnemyManager mgr; GameMap map;
    prepare_combat(&p,&mgr,&map,PRESET_FUZILEIRO);

    enemies_init_defaults(&mgr);
    mgr.count=1;
    mgr.enemies[0]=(Enemy){
        .active=1, .x=5.5f, .y=4.5f, .health=60, .max_health=60,
        .damage=7, .type=ENEMY_DOOM_IMP, .attack_kind=ENEMY_ATTACK_PROJECTILE,
        .attack_range=8.0f, .attack_cooldown=1.6f, .aggro_range=9.5f
    };
    enemies_update(&mgr,&map,&p,0.01f);
    const EnemyProjectile *projectiles=enemies_projectile_pool();
    ASSERT_TRUE(projectiles[0].active &&
                projectiles[0].kind==ENEMY_PROJECTILE_FIREBALL &&
                projectiles[0].path==ENEMY_PROJECTILE_WAVERING &&
                fabsf(projectiles[0].speed-3.1f)<0.001f,
                "Imp dispara bola de fogo mais lenta com trajetoria oscilante");
    enemies_update(&mgr,&map,&p,0.8f);
    ASSERT_TRUE(p.health==p.max_health-7 && !projectiles[0].active,
                "Projetil do Imp move por Delta Time e causa dano ao colidir");

    enemies_init_defaults(&mgr);
    mgr.count=1;
    mgr.enemies[0]=(Enemy){
        .active=1, .x=5.5f, .y=4.5f, .health=50, .max_health=50,
        .damage=8, .type=ENEMY_BERSERK_KUSHAN, .attack_kind=ENEMY_ATTACK_PROJECTILE,
        .attack_range=7.0f, .attack_cooldown=2.5f, .aggro_range=9.0f
    };
    enemies_update(&mgr,&map,&p,0.01f);
    ASSERT_TRUE(projectiles[0].active &&
                projectiles[0].kind==ENEMY_PROJECTILE_ARROW &&
                projectiles[0].path==ENEMY_PROJECTILE_STRAIGHT &&
                fabsf(projectiles[0].speed-6.0f)<0.001f,
                "Kushan dispara flecha veloz em linha reta");
    map.tiles[4][3]='#';
    enemies_update(&mgr,&map,&p,0.2f);
    ASSERT_TRUE(!projectiles[0].active && p.health==p.max_health-7,
                "Flecha colide com parede sem atingir o jogador");
    map.tiles[4][3]='.';

    enemies_init_defaults(&mgr);
    mgr.count=1;
    mgr.enemies[0]=(Enemy){
        .active=1, .x=8.5f, .y=4.5f, .health=250, .max_health=250,
        .damage=18, .type=ENEMY_DOOM_CACODEMON, .attack_kind=ENEMY_ATTACK_PROJECTILE,
        .attack_range=9.5f, .attack_cooldown=2.6f, .aggro_range=11.5f
    };
    enemies_update(&mgr,&map,&p,0.01f);
    ASSERT_TRUE(projectiles[0].active &&
                projectiles[0].kind==ENEMY_PROJECTILE_ENERGY_ORB &&
                projectiles[0].path==ENEMY_PROJECTILE_HOMING &&
                fabsf(projectiles[0].speed-2.2f)<0.001f,
                "Cacodemon dispara orbe de energia teleguiado");
    p.y=7.5f;
    enemies_update(&mgr,&map,&p,0.5f);
    ASSERT_TRUE(projectiles[0].dy>0.0f,
                "Orbe do Cacodemon curva em direcao ao jogador");

    prepare_combat(&p,&mgr,&map,PRESET_GUTS);
    enemies_init_defaults(&mgr);
    mgr.count=1;
    mgr.enemies[0]=(Enemy){
        .active=1, .x=3.2f, .y=4.5f, .health=60, .max_health=60,
        .damage=7, .speed=0, .symbol='Z'
    };
    enemies_update(&mgr,&map,&p,0.01f);
    ASSERT_TRUE(p.health==p.max_health-7,
                "Dano inimigo usa valor fixo sem escalar com a vida maxima da classe");
}

/* Testa queimadura de 5s do fogo negro do Arauto (tick de 1s, expiracao e morte) */
static void test_shadow_burn(void) {
    Player p; EnemyManager mgr; GameMap map;
    prepare_combat(&p, &mgr, &map, PRESET_SHADOW_HERALD);
    player_activate_ability(&p);
    enemies_update_fireball(&mgr, &map, &p, 0.5f);
    ASSERT_TRUE(mgr.enemies[0].burn_time == 5 && mgr.enemies[0].health == 55,
                "Impacto aplica cinco segundos de fogo");
    enemies_update(&mgr, &map, &p, 0.5f);
    ASSERT_TRUE(mgr.enemies[0].health == 55, "Fogo acumula tempo antes do primeiro dano");
    enemies_update(&mgr, &map, &p, 0.5f);
    ASSERT_TRUE(mgr.enemies[0].health == 53, "Fogo causa dois de dano por segundo");
    for (int i=0; i<240; i++) enemies_update(&mgr, &map, &p, 1.0f/60.0f);
    enemies_update(&mgr, &map, &p, 0.01f);
    ASSERT_TRUE(mgr.enemies[0].health == 45 && mgr.enemies[0].burn_time == 0,
                "Fogo expira com dez de dano total em passos pequenos");
    enemies_update(&mgr, &map, &p, 3);
    ASSERT_TRUE(mgr.enemies[0].health == 45, "Fogo expirado nao causa dano");
    player_tick_ability(&p, 30); player_activate_ability(&p);
    enemies_update_fireball(&mgr, &map, &p, 0.5f);
    enemies_update(&mgr, &map, &p, 8);
    ASSERT_TRUE(mgr.enemies[0].health == 30 && mgr.enemies[0].burn_time == 0,
                "Passo longo limita dano a cinco segundos");
    player_tick_ability(&p, 30); player_activate_ability(&p);
    enemies_update_fireball(&mgr, &map, &p, 0.5f);
    mgr.enemies[0].health = 2;
    enemies_update(&mgr, &map, &p, 1);
    enemies_update(&mgr, &map, &p, 5);
    ASSERT_TRUE(!mgr.enemies[0].active && p.ability_state.kills == 1 &&
                mgr.enemies[0].burn_time == 0, "Morte pelo fogo conta uma unica kill e apaga chamas");
    enemies_init_defaults(&mgr);
    ASSERT_TRUE(mgr.enemies[0].burn_time == 0 && mgr.enemies[0].burn_tick == 0,
                "Nova onda inicia sem fogo residual");
}

/* Testa habilidades ativas (Teto, Arauto, Guts/Berserk), spawn de onda e fogo/armadilhas */
static void test_character_abilities(void) {
    Player p; EnemyManager mgr; GameMap map;
    Weapon weapons[MAX_WEAPONS]; weapons_init_defaults(weapons);
    prepare_combat(&p,&mgr,&map,PRESET_KASANE_TETO);
    enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(p.ability_state.kills==0 && p.ability_state.zone_time==0, "Acerto sem kill nao ativa Teto");
    enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(p.ability_state.kills==1 && p.score==10 && p.ability_state.zone_time==0,
                "Kill da Teto nao cria area automaticamente");
    enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(p.ability_state.kills==1, "Inimigo morto nao conta kill repetida");
    ASSERT_TRUE(player_activate_ability(&p) && !player_activate_ability(&p) &&
                p.ability_state.cooldown==30 && p.ability_state.zone_time==10,
                "Tecla E cria area da Teto e inicia cooldown de 30s");
    ASSERT_TRUE(fabsf(player_effective_speed(&p)-p.move_speed*1.35f)<0.001f,
                "Teto recebe velocidade extra dentro da area");
    p.x=10.5f;
    ASSERT_TRUE(player_effective_speed(&p)==p.move_speed, "Teto perde bonus ao sair da area");
    ASSERT_TRUE(!player_activate_ability(&p) && p.ability_state.zone_x==2.5f,
                "Tecla E durante cooldown nao recria area");
    player_tick_ability(&p,10);
    p.x=2.5f;
    ASSERT_TRUE(p.ability_state.zone_time==0 && player_effective_speed(&p)==p.move_speed,
                "Area expira apos 10s sem deixar bonus permanente");
    player_tick_ability(&p,20);
    p.x=6.5f;
    ASSERT_TRUE(player_activate_ability(&p) && p.ability_state.zone_x==6.5f && p.ability_state.cooldown==30,
                "Tecla E reativa Teto apos cooldown");

    prepare_combat(&p,&mgr,&map,PRESET_SHADOW_HERALD);
    ASSERT_TRUE(player_activate_ability(&p) && !player_activate_ability(&p),
                "Arauto dispara uma vez e bloqueia E durante cooldown");
    enemies_update_fireball(&mgr,&map,&p,0.5f);
    ASSERT_TRUE(mgr.enemies[0].health==55 && !p.ability_state.fireball_active,
                "Bola negra causa exatamente 5 de dano sem atravessar o alvo");
    player_tick_ability(&p,29);
    ASSERT_TRUE(!player_activate_ability(&p), "Fogo negro indisponivel antes de 30s");
    player_tick_ability(&p,1);
    ASSERT_TRUE(player_activate_ability(&p), "Fogo negro recarrega em 30s");
    map.tiles[4][3]='#'; mgr.enemies[0].x=4.5f;
    enemies_update_fireball(&mgr,&map,&p,1.0f);
    ASSERT_TRUE(mgr.enemies[0].health==55 && !p.ability_state.fireball_active,
                "Bola negra colide com parede mesmo em passo longo");
    map.tiles[4][3]='.';
    player_tick_ability(&p,30); mgr.enemies[0].health=5;
    player_activate_ability(&p); enemies_update_fireball(&mgr,&map,&p,1);
    ASSERT_TRUE(!mgr.enemies[0].active && p.ability_state.kills==1,
                "Kill do projetil entra no contador uma unica vez");

    prepare_combat(&p,&mgr,&map,PRESET_GUTS);
    for(int i=0;i<4;i++) player_register_kill(&p,3.5f,4.5f);
    ASSERT_TRUE(!player_activate_ability(&p), "Guts precisa de cinco kills");
    player_register_kill(&p,3.5f,4.5f);
    int original=player_render_material(&p,0.08f,0.15f,0);
    ASSERT_TRUE(player_activate_ability(&p) && p.active_weapon==BERSERK_WEAPON &&
                p.ability_state.berserk_time==10, "Quinta kill libera Berserk por 10s");
    ASSERT_TRUE(player_render_material(&p,0.08f,0.15f,0)!=original,
                "Berserk muda visual para armadura de olhos vermelhos");
    p.ammo=0;
    ASSERT_TRUE(player_begin_attack(&p) && p.ammo==0 && !player_begin_attack(&p),
                "Espada funciona sem municao e respeita cadencia");
    mgr.enemies[0].x=5.5f;
    ASSERT_TRUE(!enemies_check_hit(&mgr,&map,&p,&weapons[0]) && mgr.enemies[0].health==60,
                "Berserk nao acerta inimigo distante");
    mgr.enemies[0].x=3.5f; mgr.enemies[0].health=250;
    enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(mgr.enemies[0].active && mgr.enemies[0].health==150 &&
                p.ability_state.kills==5,
                "Golpe Berserk causa quatro vezes o dano sem matar tanque de uma vez");
    int hp=p.health; player_take_damage(&p,10);
    ASSERT_TRUE(p.health==hp-20, "Berserk recebe dano dobrado");
    player_tick_ability(&p,10);
    ASSERT_TRUE(p.active_weapon==0 && p.ability_state.berserk_time==0 &&
                player_render_material(&p,0.08f,0.15f,0)==original && !player_activate_ability(&p),
                "Fim do Berserk restaura visual/arma e impede segundo uso");
    hp=p.health; player_take_damage(&p,10);
    ASSERT_TRUE(p.health==hp-10, "Dano recebido volta ao normal");
    prepare_combat(&p,&mgr,&map,PRESET_GUTS);
    ASSERT_TRUE(!p.ability_state.berserk_used && p.ability_state.kills==0,
                "Nova partida reseta progresso e uso do Berserk");

    CharacterRoster roster; player_roster_init(&roster);
    CharacterClass custom=player_get_preset_class(PRESET_GUTS);
    int id=player_roster_add(&roster,&custom);
    custom=player_roster_get(&roster,id); player_apply_class(&p,&custom);
    for(int i=0;i<5;i++) player_register_kill(&p,3.5f,4.5f);
    ASSERT_TRUE(p.ability==ABILITY_NONE && !player_activate_ability(&p),
                "Skin criada nao herda habilidade nem usando visual de Guts");
    prepare_combat(&p,&mgr,&map,PRESET_FUZILEIRO);
    ASSERT_TRUE(p.ability==ABILITY_NONE && !player_activate_ability(&p), "Fuzileiro continua sem habilidade");
    enemies_spawn_wave(&mgr,&map,&p);
    ASSERT_TRUE(enemies_alive(&mgr)==0, "Mapa sem caracteres de inimigo nao spawna inimigos");
    map.tiles[4][7]='Z'; map.tiles[7][11]='D'; map.tiles[12][8]='I';
    map.tiles[3][9]='M'; map.tiles[11][5]='A'; map.tiles[14][9]='B';
    enemies_spawn_wave(&mgr,&map,&p);
    int valid=enemies_alive(&mgr)==6;
    for(int i=0;i<mgr.count;i++) {
        float dx=mgr.enemies[i].x-p.x,dy=mgr.enemies[i].y-p.y;
        if(map_is_wall(&map,(int)mgr.enemies[i].x,(int)mgr.enemies[i].y)||dx*dx+dy*dy<12.25f) valid=0;
    }
    ASSERT_TRUE(valid,"Onda tem inimigos suficientes para cinco kills e spawn seguro");
    map_init_default(&map);
    map.tiles[4][5] = '^';
    map.tiles[4][6] = 'F';
    ASSERT_TRUE(map_is_wall(&map, 5, 4) == 0 && map_is_wall(&map, 6, 4) == 0,
                "Armadilha '^' e labareda 'F' sao atravessaveis");
    ASSERT_TRUE(!map_is_fire_active_at(&map, 5, 4) && map_is_fire_active_at(&map, 6, 4),
                "Armadilha '^' comeca apagada e 'F' comeca ativa");
    Player trap_p;
    player_init(&trap_p, 5.5f, 4.5f, 0.0f);
    mgr.count = 0;
    enemies_update(&mgr, &map, &trap_p, 1.0f);
    ASSERT_TRUE(map_is_fire_active_at(&map, 5, 4) && trap_p.health == 90,
                "Passar sobre '^' ativa labareda e causa 10 de dano por segundo");
    player_init(&trap_p, 6.5f, 4.5f, 0.0f);
    enemies_update(&mgr, &map, &trap_p, 1.0f);
    ASSERT_TRUE(trap_p.health == 90,
                "Contato com labareda sempre ativa 'F' causa 10 de dano por segundo");

    enemies_init_defaults(&mgr);
    mgr.count=1; mgr.enemies[0]=(Enemy){
        .active=1, .x=3.1f, .y=4.5f, .health=60, .max_health=60,
        .damage=10, .speed=0, .symbol='Z'
    };
    p.ability_state.berserk_time=1; hp=p.health;
    enemies_update(&mgr,&map,&p,0.1f);
    ASSERT_TRUE(p.health==hp-20,
                "Ataque real da IA respeita vulnerabilidade do Berserk");
}

/* Testa a passiva Guerrilheiro do Rambo (carga em 5s, consumo ao atirar e perda ao levar dano) */
static void test_guerrilla(void) {
    Player p; EnemyManager mgr; GameMap map; Weapon weapons[MAX_WEAPONS];
    weapons_init_defaults(weapons);
    prepare_combat(&p,&mgr,&map,PRESET_RAMBO);
    ASSERT_TRUE(p.ability==ABILITY_GUERRILLA && !player_activate_ability(&p),
                "Guerrilheiro e passiva automatica do Rambo");
    player_tick_ability(&p,4.9f);
    player_begin_attack(&p); enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(mgr.enemies[0].health==35, "Antes de cinco segundos tiro tem dano normal");
    p.shoot_timer=0; player_tick_ability(&p,0.11f);
    mgr.enemies[0].health=100;
    player_begin_attack(&p); enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(mgr.enemies[0].health==50 && p.ability_state.guerrilla_time==0,
                "Apos cinco segundos tiro causa dano dobrado e consome carga");
    p.shoot_timer=0; player_begin_attack(&p); enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(mgr.enemies[0].health==25, "Somente um tiro recebe o bonus");
    player_tick_ability(&p,5); player_take_damage(&p,1);
    ASSERT_TRUE(p.ability_state.guerrilla_time==0, "Sofrer dano remove a carga de Guerrilheiro");
    player_tick_ability(&p,5); p.shoot_timer=0; p.ammo=0;
    ASSERT_TRUE(!player_begin_attack(&p) && p.ability_state.guerrilla_time==5,
                "Tentativa sem municao nao consome carga");
    p.ammo=30; p.angle=PI_F;
    player_begin_attack(&p); enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(p.ability_state.guerrilla_time==0 && !p.ability_state.boosted_shot,
                "Tiro errado tambem gasta o bonus");
    p.angle=0; p.shoot_timer=0; mgr.enemies[0].health=100;
    player_begin_attack(&p); enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(mgr.enemies[0].health==75, "Bonus nao vaza para tiro posterior ao erro");
    CharacterRoster roster; player_roster_init(&roster);
    CharacterClass cls=player_get_preset_class(PRESET_RAMBO);
    int id=player_roster_add(&roster,&cls); cls=player_roster_get(&roster,id);
    player_apply_class(&p,&cls); player_tick_ability(&p,10);
    ASSERT_TRUE(p.ability==ABILITY_NONE && p.ability_state.guerrilla_time==0,
                "Skin criada com visual Rambo nao ganha Guerrilheiro");
}

/* Verifica o bonus de +25% de dano apenas dentro da area criada pela Teto */
static void test_teto_damage_bonus(void) {
    Player p; EnemyManager mgr; GameMap map;
    Weapon weapons[MAX_WEAPONS]; weapons_init_defaults(weapons);
    prepare_combat(&p,&mgr,&map,PRESET_KASANE_TETO);
    player_activate_ability(&p);
    ASSERT_TRUE(player_attack_damage(&p,100)==125, "Area da Teto concede 25% de dano extra");
    mgr.enemies[0].health=100;
    enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(mgr.enemies[0].health==69, "Tiro na area aplica dano aumentado arredondado");
    p.ability_state.zone_x=10.5f;
    enemies_check_hit(&mgr,&map,&p,&weapons[0]);
    ASSERT_TRUE(mgr.enemies[0].health==44, "Fora da area tiro volta ao dano normal");
    p.ability_state.zone_x=p.x;
    player_tick_ability(&p,TETO_ZONE_DURATION);
    ASSERT_TRUE(player_attack_damage(&p,100)==100, "Expiracao remove bonus de dano");
    p.ability_state.zone_time=10;
    p.ability=ABILITY_NONE;
    ASSERT_TRUE(player_attack_damage(&p,100)==100, "Outros personagens nao recebem bonus da Teto");
}

/* Testa kits medicos ('+', '1'-'4'), armadilhas escondidas ('^', '5'-'8') e bosses opcionais U/V/W/X */
static void test_medkit_and_traps(void) {
    printf("\nTeste: Kit medico e armadilhas com minimapa\n");
    GameMap map;
    map_init_default(&map);
    map.tiles[2][2] = '+';
    map.tiles[2][3] = '1';
    map.tiles[2][4] = '2';
    map.tiles[2][5] = '3';
    map.tiles[2][6] = '4';
    map.tiles[3][2] = '^';
    map.tiles[3][3] = '5';
    map.tiles[3][4] = '6';
    map.tiles[3][5] = '7';
    map.tiles[3][6] = '8';
    map.tiles[3][7] = '9';

    ASSERT_TRUE(map_is_wall(&map, 2, 2) == 0, "Kit medico '+' e atravessavel");
    ASSERT_TRUE(map_is_wall(&map, 3, 2) == 0, "Kit medico '1' e atravessavel");
    ASSERT_TRUE(map_is_wall(&map, 2, 3) == 0, "Armadilha '^' e atravessavel");
    ASSERT_TRUE(map_is_wall(&map, 3, 3) == 0, "Armadilha '5' e atravessavel");

    /* Versoes de minimapa como parede retornam equivalente e visibilidade no minimapa */
    ASSERT_TRUE(map_get_tile(&map, 3, 2) == '#' && map_is_visible_wall(&map, 3, 2) == 1, "Kit '1' aparece como '#' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 4, 2) == '@' && map_is_visible_wall(&map, 4, 2) == 1, "Kit '2' aparece como '@' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 5, 2) == '%' && map_is_visible_wall(&map, 5, 2) == 1, "Kit '3' aparece como '%' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 6, 2) == '*' && map_is_visible_wall(&map, 6, 2) == 1, "Kit '4' aparece como '*' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 3, 3) == '#' && map_is_visible_wall(&map, 3, 3) == 1, "Armadilha '5' aparece como '#' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 4, 3) == '@' && map_is_visible_wall(&map, 4, 3) == 1, "Armadilha '6' aparece como '@' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 5, 3) == '%' && map_is_visible_wall(&map, 5, 3) == 1, "Armadilha '7' aparece como '%' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 6, 3) == '*' && map_is_visible_wall(&map, 6, 3) == 1, "Armadilha '8' aparece como '*' no minimapa");
    ASSERT_TRUE(map_get_tile(&map, 7, 3) == '#' && map_is_visible_wall(&map, 7, 3) == 1, "Fogo '9' aparece como '#' no minimapa");
    ASSERT_TRUE(map_is_visible_wall(&map, 3, 2) == 0, "Sem chamada de minimapa, kit '1' nao e parede visivel em 3D");

    Player p;
    EnemyManager mgr;
    enemies_init_defaults(&mgr);
    player_init(&p, 2.5f, 2.5f, 0.0f);
    p.max_health = 100;
    p.health = 50;

    enemies_update(&mgr, &map, &p, 0.1f);
    ASSERT_TRUE(p.health == 70, "Passar sobre '+' pela primeira vez cura 20 de vida");
    ASSERT_TRUE(map_is_medkit_active_at(&map, 2, 2) == 0, "Kit medico '+' fica inativo apos ser usado");

    p.health = 60;
    enemies_update(&mgr, &map, &p, 0.1f);
    ASSERT_TRUE(p.health == 60, "Passar novamente sobre '+' nao cura de novo");

    p.x = 3.5f;
    enemies_update(&mgr, &map, &p, 0.1f);
    ASSERT_TRUE(p.health == 80, "Passar sobre kit '1' cura 20 de vida");

    /* Armadilha inicia oculta, ativa ao pisar e desativa apos o tempo */
    ASSERT_TRUE(map_is_fire_active_at(&map, 2, 3) == 0, "Armadilha '^' comeca desativada/oculta");
    p.x = 2.5f;
    p.y = 3.5f;
    enemies_update(&mgr, &map, &p, 0.1f);
    ASSERT_TRUE(map_is_fire_active_at(&map, 2, 3) == 1, "Armadilha '^' ativa ao passar por cima");

    p.x = 1.5f;
    p.y = 1.5f;
    int hp_after_exit = p.health;
    enemies_update(&mgr, &map, &p, 0.3f);
    ASSERT_TRUE(p.health == hp_after_exit - 3, "Mecanica de burn aplica dano por 0.5s apos sair da chama");
    enemies_update(&mgr, &map, &p, 3.0f);
    ASSERT_TRUE(map_is_fire_active_at(&map, 2, 3) == 0, "Armadilha '^' volta a desativar/nao mostrar apos expirar");
    int hp_after_burn = p.health;
    enemies_update(&mgr, &map, &p, 1.0f);
    ASSERT_TRUE(p.health == hp_after_burn, "Burn encerra apos 0.5s fora da chama");

    /* 4 caracteres reservados das arenas (U, V, W, X) -> 4 Bosses opcionais para a saida */
    map_init_default(&map);
    map.tiles[2][2] = 'U';
    map.tiles[2][3] = 'V';
    map.tiles[2][4] = 'W';
    map.tiles[2][5] = 'X';
    map.tiles[2][6] = 'E';
    ASSERT_TRUE(map_is_wall(&map, 2, 2) == 0 && map_is_wall(&map, 3, 2) == 0 &&
                map_is_wall(&map, 4, 2) == 0 && map_is_wall(&map, 5, 2) == 0,
                "Caracteres U, V, W, X sao atravessaveis como spawn de inimigo");
    enemies_spawn_wave(&mgr, &map, &p);
    int bosses_ok = (mgr.count == 4 &&
        mgr.enemies[0].type == ENEMY_BOSS_MAQUITA && mgr.enemies[0].optional_for_exit &&
        mgr.enemies[1].type == ENEMY_BOSS_REVOLVER && mgr.enemies[1].optional_for_exit &&
        mgr.enemies[2].type == ENEMY_BOSS_TOMMY && mgr.enemies[2].optional_for_exit &&
        mgr.enemies[3].type == ENEMY_BOSS_FLAME && mgr.enemies[3].optional_for_exit);
    ASSERT_TRUE(bosses_ok, "U, V, W, X spawnam como os 4 Bosses de arena opcionais para a saida");
    ASSERT_TRUE(enemies_alive(&mgr) == 4 && enemies_required_alive(&mgr) == 0,
                "Inimigos U, V, W, X vivos nao contam como obrigatorios");
    ASSERT_TRUE(map_are_enemies_alive() == 0 && map_is_wall(&map, 6, 2) == 0,
                "Saida 'E' abre mesmo com inimigos U, V, W, X vivos");

    map.tiles[3][2] = 'Z';
    enemies_spawn_wave(&mgr, &map, &p);
    ASSERT_TRUE(enemies_required_alive(&mgr) == 1 && map_are_enemies_alive() == 1 && map_is_wall(&map, 6, 2) == 1,
                "Inimigo obrigatorio 'Z' continua bloqueando a saida 'E'");
}

/* Limpa o interior do mapa deixando apenas o contorno externo de paredes */
static void clear_test_arena(GameMap *map) {
    map_init_default(map);
    for (int y = 1; y < map->height - 1; y++) {
        for (int x = 1; x < map->width - 1; x++) {
            map->tiles[y][x] = '.';
        }
    }
}

/* Testa inventario, recarga infinita da pistola, drops, pickups de mapa, orbe, pathfinding e bosses */
static void test_weapons_inventory_and_drops(void) {
    printf("\n[Teste 18] Arsenal, inventario, drops de inimigos e pickups de mapa\n");
    GameMap map;
    Player p;
    EnemyManager mgr;
    Weapon weapons[MAX_WEAPONS];
    weapons_init_defaults(weapons);
    clear_test_arena(&map);
    player_init(&p, 2.5f, 5.5f, 0.0f);

    /* Jogador comeca apenas com a Pistola (carregador 10, recarga infinita) */
    ASSERT_TRUE(p.weapon_owned[WEAPON_PISTOL] == 1 &&
                p.weapon_owned[WEAPON_SHOTGUN] == 0 &&
                p.weapon_owned[WEAPON_SNIPER] == 0 &&
                p.weapon_owned[WEAPON_MACHINEGUN] == 0 &&
                p.weapon_owned[WEAPON_FUTURISTIC] == 0,
                "Jogador inicia apenas com a Pistola no inventario");
    ASSERT_TRUE(p.weapon_mag[WEAPON_PISTOL] == 10 &&
                weapons[WEAPON_PISTOL].mag_size == 10 &&
                weapons[WEAPON_SHOTGUN].mag_size == 6 &&
                weapons[WEAPON_SNIPER].mag_size == 5 &&
                weapons[WEAPON_MACHINEGUN].mag_size == 30 &&
                weapons[WEAPON_FUTURISTIC].mag_size == 4,
                "Carregadores ajustados para cada arma (10/6/5/30/4)");

    /* Recarga infinita da Pistola */
    ASSERT_TRUE(player_begin_attack(&p) && p.weapon_mag[WEAPON_PISTOL] == 9,
                "Disparo da Pistola consome 1 bala do carregador de 10");
    ASSERT_TRUE(player_reload(&p) && p.weapon_mag[WEAPON_PISTOL] == 10,
                "Pistola recarrega com municao infinita para 10");

    /* Drop de inimigo a distancia ao morrer (0.5x a 3x carregador) */
    map.tiles[5][5] = 'K'; /* Soldado Kushan -> Sniper (mag 5, drop 3..15) */
    enemies_spawn_wave(&mgr, &map, &p);
    ASSERT_TRUE(mgr.count == 1 && mgr.enemies[0].type == ENEMY_BERSERK_KUSHAN,
                "Kushan (Sniper) spawnado no mapa de teste");
    p.shoot_timer = 0.0f;
    ASSERT_TRUE(enemies_check_hit(&mgr, &map, &p, &weapons[WEAPON_SNIPER]) == 1 &&
                mgr.enemies[0].active == 0,
                "Sniper elimina Kushan com tiro reto");
    const WeaponDrop *drops = enemies_weapon_drop_pool();
    ASSERT_TRUE(drops[0].active == 1 && drops[0].weapon == WEAPON_SNIPER &&
                drops[0].ammo >= 2 && drops[0].ammo <= 5,
                "Kushan morto derruba Sniper no chao com municao reduzida (2 a 5)");

    /* Coletar drop no chao adiciona ao inventario; pegar repetida so da municao */
    int drop_ammo = drops[0].ammo;
    p.x = drops[0].x;
    p.y = drops[0].y;
    enemies_update(&mgr, &map, &p, 0.05f);
    ASSERT_TRUE(drops[0].active == 0 && p.weapon_owned[WEAPON_SNIPER] == 1 &&
                (p.weapon_mag[WEAPON_SNIPER] + p.weapon_reserve[WEAPON_SNIPER]) == drop_ammo,
                "Passar sobre a arma derrubada adiciona Sniper ao inventario");
    ASSERT_TRUE(player_cycle_weapon(&p, 1) == 1 && p.active_weapon == WEAPON_SNIPER,
                "Troca de arma alterna para a Sniper no inventario");

    /* Pickup de mapa ('G' = Shotgun com 10x carregador = 60 balas) */
    map.tiles[5][7] = 'G';
    map_reset_weapon_pickups();
    ASSERT_TRUE(map_is_wall(&map, 7, 5) == 0 && map_is_weapon_pickup_active_at(&map, 7, 5) == 1,
                "Caractere de arma no mapa ('G') e atravessavel e ativo");
    p.x = 7.5f;
    p.y = 5.5f;
    enemies_update(&mgr, &map, &p, 0.05f);
    ASSERT_TRUE(map_is_weapon_pickup_active_at(&map, 7, 5) == 0 &&
                p.weapon_owned[WEAPON_SHOTGUN] == 1 &&
                (p.weapon_mag[WEAPON_SHOTGUN] + p.weapon_reserve[WEAPON_SHOTGUN]) == 60,
                "Pickup 'G' no mapa concede Shotgun com 10x o carregador (60 balas)");

    /* Coletar arma que ja possui apenas adiciona municao na reserva */
    int res_before = p.weapon_reserve[WEAPON_SHOTGUN];
    ASSERT_TRUE(player_pickup_weapon(&p, WEAPON_SHOTGUN, 12) == 2 &&
                p.weapon_reserve[WEAPON_SHOTGUN] == res_before + 12,
                "Pegar arma ja presente no inventario apenas adiciona municao");

    /* Arma Futurista dispara orbe teleguiado que acerta inimigo */
    clear_test_arena(&map);
    map.tiles[5][7] = 'Z';
    enemies_spawn_wave(&mgr, &map, &p);
    player_init(&p, 2.5f, 5.5f, 0.0f);
    player_pickup_weapon(&p, WEAPON_FUTURISTIC, 40);
    player_switch_weapon(&p, WEAPON_FUTURISTIC);
    ASSERT_TRUE(enemies_check_hit(&mgr, &map, &p, &weapons[WEAPON_FUTURISTIC]) == 1 &&
                enemies_player_orb_pool()[0].active == 1,
                "Arma Futurista dispara orbe teleguiado");
    for (int step = 0; step < 30; step++) {
        enemies_update(&mgr, &map, &p, 0.05f);
    }
    ASSERT_TRUE(mgr.enemies[0].active == 0,
                "Orbe teleguiado da Arma Futurista alcanca e elimina o inimigo");

    /* IA com pathfinding BFS contorna parede entre inimigo e jogador sem enroscar */
    clear_test_arena(&map);
    map.tiles[4][4] = '#';
    map.tiles[5][4] = '#';
    map.tiles[6][4] = '#';
    map.tiles[5][6] = 'Z';
    enemies_spawn_wave(&mgr, &map, &p);
    player_init(&p, 2.5f, 5.5f, 0.0f);
    for (int step = 0; step < 90; step++) {
        enemies_update(&mgr, &map, &p, 0.08f);
    }
    ASSERT_TRUE(mgr.enemies[0].x < 4.5f,
                "IA com pathfinding contorna parede sem ficar enroscada");

    /* Matar cada boss derruba sua arma no chao com 10x o carregador (igual as demais) */
    clear_test_arena(&map);
    map.tiles[5][5] = 'U';
    enemies_spawn_wave(&mgr, &map, &p);
    player_init(&p, 2.5f, 5.5f, 0.0f);
    mgr.enemies[0].health = 10;
    ASSERT_TRUE(enemies_check_hit(&mgr, &map, &p, &weapons[WEAPON_PISTOL]) == 1 &&
                mgr.enemies[0].active == 0,
                "Boss Maquita eliminado");
    const WeaponDrop *bdrops = enemies_weapon_drop_pool();
    ASSERT_TRUE(bdrops[0].active == 1 && bdrops[0].weapon == WEAPON_MAQUITA &&
                bdrops[0].ammo == 100 && p.weapon_owned[WEAPON_MAQUITA] == 0,
                "Matar Boss Maquita derruba Maquita no chao com 10x carregador (100)");
    p.x = bdrops[0].x;
    p.y = bdrops[0].y;
    enemies_update(&mgr, &map, &p, 0.05f);
    ASSERT_TRUE(bdrops[0].active == 0 && p.weapon_owned[WEAPON_MAQUITA] == 1 &&
                (p.weapon_mag[WEAPON_MAQUITA] + p.weapon_reserve[WEAPON_MAQUITA]) == 100,
                "Passar sobre a Maquita derrubada adiciona ao inventario com 100");

    clear_test_arena(&map);
    map.tiles[5][5] = 'X';
    enemies_spawn_wave(&mgr, &map, &p);
    player_init(&p, 2.5f, 5.5f, 0.0f);
    mgr.enemies[0].health = 10;
    ASSERT_TRUE(enemies_check_hit(&mgr, &map, &p, &weapons[WEAPON_PISTOL]) == 1 &&
                enemies_weapon_drop_pool()[0].active == 1 &&
                enemies_weapon_drop_pool()[0].weapon == WEAPON_FLAMETHROWER &&
                enemies_weapon_drop_pool()[0].ammo == 400,
                "Matar Boss Lanca-Chamas derruba Lanca-Chamas no chao com 10x carregador (400)");
}

/* Executa toda a suite de testes unitarios em sequencia e retorna 0 se passar tudo */
int main(void) {
    printf("============================================================\n");
    printf("          SUITE DE TESTES - SECTOR 404 (404 NF)            \n");
    printf("============================================================\n");

    test_map_default();
    test_map_file_loading();
    test_map_bounds();
    test_player_movement_and_collision();
    test_player_rotation();
    test_teto_selection();
    test_shadow_selection();
    test_guts_selection();
    test_character_visual_api();
    test_character_creator();
    test_modular_appearance();
    test_enemy_projectiles();
    test_character_abilities();
    test_shadow_burn();
    test_guerrilla();
    test_teto_damage_bonus();
    test_medkit_and_traps();
    test_weapons_inventory_and_drops();
    test_network_udp_loopback();

    printf("\n------------------------------------------------------------\n");
    printf("Resultado: %d verificacoes executadas | %d falhas\n", tests_run, tests_failed);
    printf("------------------------------------------------------------\n");

    return (tests_failed == 0) ? 0 : 1;
}
