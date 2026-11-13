#include <math.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "config.h"
#include "map.h"
#include "network.h"
#include "player.h"

static int tests_run = 0;
static int tests_failed = 0;

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

static void test_map_default(void) {
    printf("\nTeste 1: Inicializacao do mapa padrao\n");
    GameMap map;
    map_init_default(&map);

    ASSERT_TRUE(map.width == MAP_WIDTH, "Largura do mapa igual a MAP_WIDTH");
    ASSERT_TRUE(map.height == MAP_HEIGHT, "Altura do mapa igual a MAP_HEIGHT");
    ASSERT_TRUE(map_is_wall(&map, 0, 0) == 1, "Canto (0,0) e parede");
    ASSERT_TRUE(map_is_wall(&map, 1, 1) == 0, "Posicao interna (1,1) e livre");
}

static void test_map_file_loading(void) {
    printf("\nTeste 2: Carregamento de mapa via arquivo\n");
    GameMap map;

    int ok_missing = map_load_from_file(&map, "assets/maps/nao_existe.txt");
    ASSERT_TRUE(ok_missing == 0, "Retorna 0 para arquivo inexistente");

    int ok_valid = map_load_from_file(&map, "assets/maps/level1.txt");
    ASSERT_TRUE(ok_valid == 1, "Carrega assets/maps/level1.txt com sucesso");
    ASSERT_TRUE(map_get_tile(&map, 0, 0) == '#', "Borda (0,0) do level1.txt e '#'");
    ASSERT_TRUE(map_is_wall(&map, 3, 2) == 1, "Obstaculo em (3,2) e parede");
    ASSERT_TRUE(map_is_wall(&map, 2, 1) == 0, "Corredor em (2,1) e livre");
}

static void test_map_bounds(void) {
    printf("\nTeste 3: Seguranca de limites do mapa\n");
    GameMap map;
    map_init_default(&map);

    ASSERT_TRUE(map_is_wall(&map, -1, 5) == 1, "Coordenada X negativa tratada como parede");
    ASSERT_TRUE(map_is_wall(&map, 5, -1) == 1, "Coordenada Y negativa tratada como parede");
    ASSERT_TRUE(map_is_wall(&map, MAP_WIDTH, 5) == 1, "Coordenada X >= width tratada como parede");
    ASSERT_TRUE(map_is_wall(&map, 5, MAP_HEIGHT) == 1, "Coordenada Y >= height tratada como parede");
}

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

static void test_player_rotation(void) {
    printf("\nTeste 5: Rotacao e normalizacao de angulo\n");
    Player p;
    player_init(&p, 2.5f, 2.5f, 0.0f);

    player_rotate(&p, -0.5f);
    ASSERT_TRUE(p.angle >= 0.0f && p.angle < 2.0f * PI_F, "Angulo negativo normalizado para [0, 2*PI)");

    player_rotate(&p, 3.0f * PI_F);
    ASSERT_TRUE(p.angle >= 0.0f && p.angle < 2.0f * PI_F, "Angulo grande normalizado para [0, 2*PI)");
}

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

int main(void) {
    printf("============================================================\n");
    printf("          SUITE DE TESTES - ASCII DOOM EM C (A2)           \n");
    printf("============================================================\n");

    test_map_default();
    test_map_file_loading();
    test_map_bounds();
    test_player_movement_and_collision();
    test_player_rotation();
    test_network_udp_loopback();

    printf("\n------------------------------------------------------------\n");
    printf("Resultado: %d verificacoes executadas | %d falhas\n", tests_run, tests_failed);
    printf("------------------------------------------------------------\n");

    return (tests_failed == 0) ? 0 : 1;
}
