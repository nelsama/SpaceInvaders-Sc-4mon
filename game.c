/**
 * ============================================================================
 * game.c - SPACE INVADERS para el Core de Video (vc) / Monitor 6502
 * ============================================================================
 * Fase 2: la flota de invasores.
 *   - Fase 0: init de video, fondo con estrellas, nave sprite, HUD, entrada UART.
 *   - Fase 1: nave moviendose con teclado.
 *   - Fase 2 (esta): rejilla 5x11 de invasores como TILES del tilemap,
 *     animacion de 2 frames, march lateral y descenso al tocar el borde.
 *
 * CONTROLES (UART):  a/j = izquierda,  d/l = derecha,  q = salir.
 *
 * DISENO: la flota son TILES, no sprites (la OAM solo tiene 32 slots y hay
 * 55 invasores). El movimiento es por PASOS DE CELDA: cada N frames se borra
 * la rejilla vieja y se escribe la nueva. Ver README.md.
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "romapi.h"
#include "sound.h"
#include "joy.h"

/* Declaraciones adelantadas de las funciones de texto (definidas mas abajo). */
static void put_str_at(uint8_t col, uint8_t row, const char *s);
static void put_str_center(uint8_t row, const char *s);
static void clear_str_at(uint8_t col, uint8_t row, uint8_t len);

/* Rutinas de texto/HUD en ensamblador (text.s): escritura VRAM directa y
 * numeros por restas (sin division de 16 bits). */
extern void txt_put_at(uint8_t col, uint8_t row, const char *s);
extern void txt_clear_at(uint8_t col, uint8_t row, uint8_t len);
extern void txt_put_u8_1(uint8_t col, uint8_t row, uint8_t valor);
extern void txt_put_u16_4(uint8_t col, uint8_t row, uint16_t valor);

/* Aritmetica sin division de hardware (math.s): por restas. Se usan en vez de
 * / y % para que cc65 no arrastre div.o/udiv.o/umod.o. */
extern uint8_t u8_mod(uint8_t a, uint8_t b);
extern uint8_t u16_div_u8(uint16_t num, uint8_t den);

/* ===========================================================================
 * PATRONES DE FONDO (2 planos, 8x8). color = (plano1<<1) | plano0
 * =========================================================================== */

/* ---------------------------------------------------------------------------
 * COLOR DE FONDO
 * ---------------------------------------------------------------------------
 * El hardware NO pinta el color 0 del fondo (es transparente) y deja ver un
 * BG_COLOR global fijo (azul cielo), y NO hay registro para cambiarlo. La unica
 * forma de tener fondo negro es pintar los tiles con un color 1-3 de la paleta
 * y reprogramar ESE color a negro (ver setup_video).
 * Los tiles de fondo usan por tanto color 1 como "negro de fondo".
 * ------------------------------------------------------------------------- */

/* Tile 0: fondo negro solido. Todo el tile a color 1 (plano0 = 1, plano1 = 0).
 * El color 1 de la paleta de fondo se reprograma a negro en setup_video(). */
static const uint8_t tile_space_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t tile_space_p1[8] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

/* Tile 1: estrella sobre fondo negro. Base color 1 (negro) + un pixel color 3
 * (blanco) -> se ve un punto. plano0 = 1 en todo, plano1 = 1 solo en el pixel. */
static const uint8_t tile_star_p0[8]  = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t tile_star_p1[8]  = { 0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00 };

/* Tile 2: suelo solido, color 2 (plano1). */
static const uint8_t tile_floor_p0[8] = { 0,0,0,0,0,0,0,0 };
static const uint8_t tile_floor_p1[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* ---------------------------------------------------------------------------
 * ESCUDO (bunker) en forma de ARCO / U INVERTIDA (4x4 tiles).
 * ------------------------------------------------------------------------- */

/* Tiles de fondo basicos */
#define TILE_EMPTY   0
#define TILE_STAR    1
#define TILE_FLOOR   2
/* Escudo: 5 bloques consecutivos desde TILE_SHIELD. */
#define TILE_SHIELD      3   /* bloque 1: esquina sup-izq */
#define TILE_SHIELD_B2   4   /* bloque 2: esquina sup-der */
#define TILE_SHIELD_B3   5   /* bloque 3: pata inf-izq */
#define TILE_SHIELD_B4   6   /* bloque 4: pata inf-der */
#define TILE_SHIELD_B5   7   /* bloque 5: lleno */
#define TILE_INV_BASE 0x10  /* $10..$15 = 3 invasores x 2 frames */

/* Dimensiones del escudo (celdas) */
#define SHIELD_W     4
#define SHIELD_H     3

/* Bloque 1: esquina superior-izquierda. */
static const uint8_t sh_b1_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t sh_b1_p1[8] = { 0x0F,0x1F,0x3F,0x7F,0xFF,0xFF,0xFF,0xFF };
/* Bloque 2: esquina superior-derecha. */
static const uint8_t sh_b2_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t sh_b2_p1[8] = { 0xF0,0xF8,0xFC,0xFE,0xFF,0xFF,0xFF,0xFF };
/* Bloque 3: pata inferior-izquierda (recorta hacia la derecha, forma la U). */
static const uint8_t sh_b3_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t sh_b3_p1[8] = { 0xFF,0xFF,0xFF,0xFE,0xF8,0xF0,0xE0,0xC0 };
/* Bloque 4: pata inferior-derecha. */
static const uint8_t sh_b4_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t sh_b4_p1[8] = { 0xFF,0xFF,0xFF,0x7F,0x1F,0x0F,0x07,0x03 };
/* Bloque 5: lleno. */
static const uint8_t sh_b5_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t sh_b5_p1[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* Plantilla de forma del escudo (4 x 3). Cada celda indica que bloque usar:
 *   1 5 5 2
 *   5 3 4 5
 *   5 0 0 5   <- las dos celdas centrales inferiores quedan VACIAS (la U) */
static const uint8_t shield_shape[SHIELD_H][SHIELD_W] = {
    { 1, 5, 5, 2 },
    { 5, 3, 4, 5 },
    { 5, 0, 0, 5 }
};

/* ---------------------------------------------------------------------------
 * INVASORES (6 tiles: 3 tipos x 2 frames). Color 3 (iguales en ambos planos).
 * Se cargan en los tiles $10..$15 (fuera del rango de la fuente $20-$7F).
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * INVASORES (6 tiles: 3 tipos x 2 frames).
 * Codificacion: color = (plano1<<1)|plano0. Queremos arte = color 3 (blanco)
 * y fondo = color 1 (negro, ya reprogramado). Por eso:
 *     plano0 = 0xFF SIEMPRE  -> el bit 0 del color vale 1 (evita el color 0,
 *                               que seria transparente y mostraria BG_COLOR)
 *     plano1 = el dibujo      -> sube a color 3 donde hay pixel
 * Se cargan en los tiles $10..$15 (fuera del rango de la fuente $20-$7F).
 * ------------------------------------------------------------------------- */
static const uint8_t inv_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* Tipo A - "calamar" (fila de arriba). 2 frames. */
static const uint8_t inv_a0_p1[8] = { 0x18,0x3C,0x7E,0xDB,0xFF,0xFF,0x24,0x5A };
static const uint8_t inv_a1_p1[8] = { 0x18,0x3C,0x7E,0xDB,0xFF,0xDB,0x24,0x42 };

/* Tipo B - "cangrejo" (filas 2 y 3). 2 frames. */
static const uint8_t inv_b0_p1[8] = { 0x42,0x24,0x3C,0x5A,0xFF,0xFF,0xA5,0x24 };
static const uint8_t inv_b1_p1[8] = { 0x42,0x24,0xBD,0xDB,0xFF,0x7E,0x24,0x42 };

/* Tipo C - "pulpo" (filas de abajo). 2 frames. */
static const uint8_t inv_c0_p1[8] = { 0x3C,0x7E,0xFF,0xDB,0xFF,0x5A,0x81,0x42 };
static const uint8_t inv_c1_p1[8] = { 0x3C,0x7E,0xFF,0xDB,0xFF,0x5A,0x42,0x81 };

/* ===========================================================================
 * ESTRELLAS DEL FONDO
 * ===========================================================================
 * Posiciones fijas (x, y) en celdas del mapa, elegidas a mano para que el
 * cielo se vea aleatorio y bien repartido. Una tabla es mas simple, rapida y
 * predecible que calcular un hash en el 6502, y evita patrones lineales
 * (diagonales) que aparecen con formulas del tipo (x*a + y*b) % m.
 * Se evitan las filas del HUD (0-2 y 27-29) y las del suelo.
 * =========================================================================== */
typedef struct { uint8_t x, y; } star_t;
static const star_t stars[] = {
    { 4, 5}, {18, 4}, {33, 6}, {47, 5}, {58, 7},
    {10, 9}, {25, 8}, {40,10}, {55, 9},
    {2,13}, {15,12}, {29,14}, {44,12}, {60,13},
    {7,17}, {21,16}, {36,18}, {51,17},
    {12,21}, {27,20}, {42,22}, {57,21},
    {5,25}, {19,24}, {34,26}, {49,25}, {62,24},
    {23, 3}, {38, 7}, {8, 19}, {53, 3},
    {31, 11}, {16, 23}, {45, 15}, {11, 7}
};
#define NSTARS ((uint8_t)(sizeof(stars) / sizeof(stars[0])))

/* ===========================================================================
 * PATRONES DE SPRITE (2 planos, 8x8)
 * =========================================================================== */

/* Nave del jugador: 8x8 a 2x -> 16x16 en pantalla. Blanca (color 3).
 * Rejilla:
 *      ........      margen
 *      ...##...      canon
 *      ...##...
 *      ..####..      aletas
 *      .######.
 *      ########      base ancha
 *      ########
 *      ..#..#..      patas
 * En sprites el color 0 es transparente, asi que plano0 = plano1 = dibujo. */
static const uint8_t ship_p0[8] = { 0x00,0x18,0x18,0x3C,0x7E,0xFF,0xFF,0x24 };
static const uint8_t ship_p1[8] = { 0x00,0x18,0x18,0x3C,0x7E,0xFF,0xFF,0x24 };

/* Bala: barra vertical de 2x6 centrada, color 3 (blanca). */
static const uint8_t bullet_p0[8] = { 0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00 };
static const uint8_t bullet_p1[8] = { 0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00 };

/* Bala enemiga: barra vertical de 2x4, con punta. Color 3 (blanca). */
static const uint8_t ebullet_p0[8] = { 0x00,0x18,0x18,0x18,0x18,0x3C,0x00,0x00 };
static const uint8_t ebullet_p1[8] = { 0x00,0x18,0x18,0x18,0x18,0x3C,0x00,0x00 };

/* Explosion: estrella/racimo que se ensancha. Color 3 (naranja en paleta GREEN). */
static const uint8_t explode_p0[8] = { 0x91,0x42,0x24,0x19,0x98,0x24,0x42,0x89 };
static const uint8_t explode_p1[8] = { 0x91,0x42,0x24,0x19,0x98,0x24,0x42,0x89 };

/* UFO (nave nodriza): mitad IZQUIERDA del platillo (16x8 en total, con 2
 * sprites lado a lado). El lado derecho se dibuja con el MISMO patron y
 * FLIP_X (VC_SPR_FLIP_X), para ahorrar un patron. Color 3 (blanca). */
static const uint8_t ufo_p0[8] = { 0x00,0x07,0x7F,0xFF,0xBB,0xFF,0x71,0x20 };
static const uint8_t ufo_p1[8] = { 0x00,0x07,0x7F,0xFF,0xBB,0xFF,0x71,0x20 };

/* ===========================================================================
 * CONSTANTES DEL JUEGO
 * =========================================================================== */

/* Sprites (OAM) */
#define SPR_SHIP     0      /* slot de OAM de la nave */
#define SPR_BULLET0  1      /* primera bala del pool del jugador */
#define NBULLETS     3      /* slots de bala del jugador (solo 1 activa a la vez) */
#define SPR_PAT_BULLET 1    /* patron de sprite de la bala del jugador */
#define SPR_PAT_EBULLET 2   /* patron de sprite de la bala enemiga */
#define SPR_PAT_EXPLODE 3   /* patron de sprite de la explosion */
#define SPR_PAT_UFO    4    /* patron de sprite del UFO (mitad izquierda) */

/* UFO (nave nodriza): 2 sprites contiguos en el OAM (izq + der con FLIP_X).
 * El slot 6 estaba libre (0 nave, 1-3 balas, 4-5 misiles, 7-8 explosiones). */
#define SPR_UFO       6     /* primer slot del UFO (6 = izq, 6+1 = der) */
#define UFO_Y         27    /* fila (px) por la que cruza (3 px bajo el HUD) */
#define UFO_SPEED     1     /* px por frame */
#define UFO_W         16    /* ancho en pantalla (2 sprites de 8) */
#define UFO_POINTS    150   /* puntos al destruir el UFO */

/* Explosiones (efimeros: dibujan unos frames y se apagan) */
#define SPR_EXPLODE0  7     /* primera explosion (slots 7..8) */
#define NEXPLOSIONS   2     /* explosiones simultaneas */
#define EXPLODE_FRAMES 12   /* frames que dura una explosion */

/* Nave */
#define SHIP_W       16     /* ancho en pantalla (8x8 a 2x) */
#define SHIP_Y       200    /* fila (px) donde vive la nave */
#define SHIP_SPEED   3      /* px por frame */
#define SHIP_MIN_X   8
#define SHIP_MAX_X   (VC_SCREEN_W - 8 - SHIP_W)

/* Escudos (bunkers): NUM_SHIELDS bases de 4x4 celdas.
 * Las dimensiones (SHIELD_W/H) y los tiles estan definidos arriba, junto a la
 * plantilla de forma (shield_shape). Cada celda viva se guarda en un bitmask
 * por fila, como la flota. */
#define NUM_SHIELDS  4      /* 4 bases, como el arcade original */
#define SHIELD_Y     20     /* fila (del mapa) de la parte SUPERIOR del escudo (3 filas: 20-22) */
#define SHIELD_X0    3      /* columna del primer escudo */
#define SHIELD_GAP   9      /* separacion entre escudos (en celdas) */

/* Flota: rejilla 5 filas x 11 columnas de invasores.
 * Los invasores se colocan cada FLEET_STRIDE celdas (horizontal) y
 * FLEET_ROW_STRIDE celdas (vertical) para que no queden pegados: 1 celda de
 * hueco entre ellos en ambos ejes. */
#define FLEET_COLS   11
#define FLEET_ROWS   5
#define FLEET_STRIDE 2          /* celdas por invasor en horizontal */
#define FLEET_ROW_STRIDE 2      /* celdas por invasor en vertical    */
#define FLEET_LEFT0  2          /* columna inicial (en celdas del mapa) */
#define FLEET_TOP0   5          /* fila inicial (deja la fila 3 libre para el UFO) */
#define FLEET_WIDTH  (FLEET_COLS * FLEET_STRIDE - (FLEET_STRIDE - 1))
#define FLEET_HEIGHT (FLEET_ROWS * FLEET_ROW_STRIDE - (FLEET_ROW_STRIDE - 1))

/* Fondo estatico (suelo/estrellas) en RAM, para restaurar las celdas que la
 * flota o las balas pisen. Solo se necesita la zona VISIBLE (40x30), no las
 * 64 columnas del mapa, asi que se guarda solo esa parte (ahorra ~850 B). */
#define BG_COLS      VC_SCREEN_COLS   /* 40 */
#define BG_ROWS      VC_SCREEN_ROWS   /* 30 */

/* Paso de la flota en celdas por movimiento */
#define FLEET_ROW_DESC 1    /* filas que baja al tocar borde */

/* Velocidad: frames entre pasos (baja -> mas rapido).
 *   - Nivel 1 empieza en 48 frames/paso.
 *   - Cada nivel nuevo arranca mas rapido que el anterior (-8 frames).
 *   - Dentro del nivel, la flota ACELERA conforme quedan menos invasores
 *     (ver tabla en fleet_step_delay). El final se acentua en niveles altos,
 *     cuya base ya es menor. */
#define FLEET_STEP_FRAMES 48    /* retardo inicial del nivel 1 */
#define FLEET_STEP_MIN    3     /* retardo minimo absoluto (seguridad VBLANK) */
#define FLEET_STEP_DEC    8     /* frames menos por cada nivel superado */
#define INVADER_FRAMES    2     /* frames de animacion */

/* Aceleracion dentro del nivel: el retardo baja PROGRESIVAMENTE segun los
 * invasores vivos (suelo ~18% del base del nivel; ver tabla en
 * fleet_step_delay). El minimo absoluto es FLEET_STEP_MIN. */

/* Balas */
#define BULLET_W      8     /* ancho logico de la bala (px) */
#define BULLET_H      8     /* alto logico (px) */
#define BULLET_SPEED  6     /* px por frame hacia arriba */
#define BULLET_Y_MIN  16    /* se apaga al llegar aqui (zona HUD/margen) */

/* Balas enemigas.
 * LIMITE DEL HARDWARE: el core dibuja como maximo 8 sprites por linea de
 * barrido; si se supera, descarta alguno (OVERFLOW). Por eso este pool se
 * limita a 2 (en el peor caso, una linea puede tener: 2 misiles + 2
 * explosiones + 1 bala del jugador + 1 nave = 6, con margen bajo 8).
 * Con 3 misiles + 4 explosiones se llegaba a 9 y los misiles desaparecian
 * (hacian dano pero no se veian). */
#define SPR_EBULLET0  4     /* primera bala enemiga (slots 4..5) */
#define NEBULLETS     2     /* balas enemigas simultaneas (<= 8 sprites/linea) */
#define EBULLET_SPEED 3     /* px por frame hacia abajo */
#define EBULLET_Y_MAX 232   /* se apaga al llegar aqui (suelo) */
#define ENEMY_FIRE_MIN 40   /* frames minimos entre disparos enemigos */
#define ENEMY_FIRE_MAX 90   /* frames maximos entre disparos enemigos */

/* Jugador */
#define SHIP_LIVES    3     /* vidas iniciales */
#define RESPAWN_BLINK 90    /* frames de invulnerabilidad/parpadeo tras revivir */
#define DEATH_PAUSE   120   /* frames de PAUSA tras morir (~2 s) antes de revivir */
#define FIRE_COOLDOWN 30    /* frames minimos entre disparos (~0.5 s) */

/* ===========================================================================
 * ESTADO
 * =========================================================================== */
static uint16_t ship_x;
static uint8_t  quit_flag;

/* Flota. fleet_alive[row] = bitmask de columnas vivas (bit c = columna c,
 * 11 columnas -> hace falta un entero de 16 bits). */
static uint16_t fleet_alive[FLEET_ROWS];
static uint8_t  fleet_col;      /* columna actual de la esquina izquierda */
static uint8_t  fleet_row;      /* fila actual de la esquina superior */
static int8_t   fleet_dx;       /* +1 derecha, -1 izquierda */
static uint8_t  fleet_frame;    /* 0/1: frame de animacion */
static uint8_t  fleet_tick;     /* contador de frames hasta el proximo paso */
static uint8_t  fleet_step_frames; /* frames entre pasos (baja con el nivel) */
static uint8_t  fleet_count;    /* invasores vivos */
static uint8_t  level;          /* nivel actual (1..) */

/* Fondo estatico del mundo. Para ahorrar RAM NO se guarda un byte por celda:
 * el fondo es negro salvo las estrellas (pocas) y el suelo (fila conocida). Se
 * guarda solo un BITMAP de estrellas (1 bit por celda = BG_ROWS*BG_COLS/8 B).
 * El suelo se deduce por fila. Asi se pasa de 1200 B a ~150 B de RAM. */
#define BG_STAR_BYTES ((BG_ROWS * BG_COLS + 7) / 8)
static uint8_t  bg_stars[BG_STAR_BYTES];

/* Balas del jugador: arrays paralelos. 1 = viva. */
static uint16_t b_x[NBULLETS];
static uint8_t  b_y[NBULLETS];
static uint8_t  b_alive[NBULLETS];

/* Balas enemigas: arrays paralelos. 1 = viva. */
static uint16_t e_x[NEBULLETS];
static uint8_t  e_y[NEBULLETS];
static uint8_t  e_alive[NEBULLETS];
static uint8_t  enemy_fire_tick;   /* frames hasta el proximo disparo enemigo */

/* Explosiones: posicion + temporizador restante. */
static uint16_t x_x[NEXPLOSIONS];
static uint8_t  x_y[NEXPLOSIONS];
static uint8_t  x_t[NEXPLOSIONS];  /* frames restantes (0 = inactiva) */
static uint8_t  x_big[NEXPLOSIONS]; /* 1 = explosion a 2x (la nave es 16x16) */

/* Escudos: por cada escudo, un bitmask por fila (bit c = celda viva).
 * La posicion de cada escudo es fija y se calcula con shield_col(). */
static uint8_t  shield_alive[NUM_SHIELDS][SHIELD_H];

/* UFO (nave nodriza). ufo_active = 1 mientras cruza la pantalla.
 * ufo_x = X (9 bits); ufo_dir = +1 derecha / -1 izquierda; ufo_wait = frames
 * que faltan para la proxima aparicion. */
static uint8_t  ufo_active;
static uint16_t ufo_x;
static int8_t   ufo_dir;
static uint8_t  ufo_wait;
static uint8_t  ufo_div;      /* divisor: cuenta 8 frames por unidad de ufo_wait */

/* Puntuacion, vidas y estado */
static uint16_t score;
static uint8_t  lives;
static uint8_t  blink;             /* >0: nave parpadeando/invulnerable */
static uint8_t  fire_cooldown;     /* >0: frames que faltan para poder disparar */
static uint8_t  death_pause;       /* >0: pausa tras morir (sin nave ni accion) */
static uint8_t  game_over;         /* 1 = fin de partida */

/* Tabla de mejores puntuaciones (solo RAM: se pierde al apagar). Ordenada de
 * mayor a menor; hiscore_n = entradas validas (0..HISCORE_N). */
#define HISCORE_N 5
static uint16_t hiscores[HISCORE_N];
static uint8_t  hiscore_n;

/* Generador pseudoaleatorio simple (LCG) para elegir columnas enemigas. */
static uint16_t rng_state;

static uint8_t rng_next(void) {
    /* xorshift de 8 bits evitando el cero */
    rng_state ^= (uint16_t)(rng_state << 7);
    rng_state ^= (uint16_t)(rng_state >> 9);
    rng_state ^= (uint16_t)(rng_state << 8);
    return (uint8_t)(rng_state & 0xFF);
}

/* ===========================================================================
 * ENTRADA (UART, no bloqueante) -- capa aislada
 * ===========================================================================
 * Teclas:  a/A o j/J = izquierda,  d/D o l/L = derecha,
 *          space = disparar,  q/Q = salir.
 * =========================================================================== */

/* Crea la bala del jugador. Como el arcade original, SOLO puede haber UNA
 * bala en vuelo: si ya hay una viva, no se dispara. Devuelve 1 si disparo. */
static uint8_t fire_bullet(void) {
    uint8_t i;

    if (fire_cooldown) return 0;         /* espera entre disparos */

    for (i = 0; i < NBULLETS; i++) {
        if (b_alive[i]) return 0;        /* ya hay una bala en vuelo */
    }

    /* Usa el primer slot (con NBULLETS=1 siempre es el 0, pero se deja
     * generico por si se amplia el pool). */
    for (i = 0; i < NBULLETS; i++) {
        b_x[i]     = (uint16_t)(ship_x + (SHIP_W / 2) - (BULLET_W / 2));
        b_y[i]     = (uint8_t)(SHIP_Y - BULLET_H);
        b_alive[i] = 1;
        fire_cooldown = FIRE_COOLDOWN;   /* no permitir otra hasta pasar N frames */
        vc_sprite_move((uint8_t)(SPR_BULLET0 + i), b_x[i], b_y[i], VC_SPPAL_1);
        vc_oam_put((uint8_t)(SPR_BULLET0 + i), VC_OAM_TILE, SPR_PAT_BULLET);
        snd_shoot();
        return 1;
    }
    return 0;
}

static void read_input(void) {
    int8_t move = 0;
    uint8_t j;

    /* --- Joystick Atari (Puerto 1) --- */
    j = joy_read();
    if (j & JOY_A_LEFT)  move = -SHIP_SPEED;
    if (j & JOY_A_RIGHT) move =  SHIP_SPEED;

    /* Disparo: AUTO si el boton esta pulsado, pero con un pequeno retardo
     * (fire_cooldown) para que no salga una rafaga al morir la bala anterior
     * (p.ej. al impactar los propios escudos). */
    if (j & JOY_A_FIRE) fire_bullet();

    /* --- UART (teclado del terminal), ademas del joystick --- */
    while (rom_uart_rx_ready()) {
        char c = rom_uart_getc();
        switch (c) {
            case 'a': case 'A': case 'j': case 'J':
                move = -SHIP_SPEED; break;
            case 'd': case 'D': case 'l': case 'L':
                move = SHIP_SPEED; break;
            case ' ':
                fire_bullet(); break;
            case 'q': case 'Q':
                quit_flag = 1; break;
            default:
                break;   /* otras teclas se ignoran */
        }
    }

    if (move) {
        int16_t nx = (int16_t)(ship_x + move);
        if (nx < SHIP_MIN_X) { nx = SHIP_MIN_X; }
        if (nx > SHIP_MAX_X) { nx = SHIP_MAX_X; }
        ship_x = (uint16_t)nx;
        vc_sprite_move(SPR_SHIP, ship_x, SHIP_Y, VC_SPPAL_3 | VC_SPR_SCALE2X);
        vc_oam_put(SPR_SHIP, VC_OAM_TILE, SPR_SHIP);
    }
}

/* ===========================================================================
 * FONDO ESTATICO
 * ===========================================================================
 * bg_stars[] es un bitmap: bit 1 = hay estrella. El suelo se deduce de la fila.
 * La flota y las balas escriben por encima; al abandonar una celda se restaura
 * el fondo calculandolo.
 * ========================================================================= */

/* Indice de bit de la celda (col,row) en bg_stars[]. */
#define BG_BIT(col, row) ((uint16_t)(row) * BG_COLS + (col))

/* 1 si la celda (col,row) tiene estrella. */
static uint8_t bg_star_at(uint8_t col, uint8_t row) {
    uint16_t b = BG_BIT(col, row);
    return (uint8_t)((bg_stars[b >> 3] >> (b & 7)) & 1u);
}

/* Marca (col,row) como estrella. */
static void bg_star_set(uint8_t col, uint8_t row) {
    uint16_t b = BG_BIT(col, row);
    bg_stars[b >> 3] |= (uint8_t)(1u << (b & 7));
}

/* Tile del fondo estatico en (col,row): suelo, estrella o negro. */
static uint8_t bg_at(uint8_t col, uint8_t row) {
    if (row >= BG_ROWS) return TILE_EMPTY;
    if (row == BG_ROWS - 1) return TILE_FLOOR;
    return bg_star_at(col, row) ? TILE_STAR : TILE_EMPTY;
}

/* Restaura en el tilemap la celda (col,row) con el fondo estatico. */
static void cell_restore(uint8_t col, uint8_t row) {
    vc_put_cell(col, row, bg_at(col, row));
}

/* Version RAPIDA de escritura de celda: escribe el tile en (col,row) SIN la
 * llamada de vc_put_cell (que es lenta: jsr + popa x2 + calc_cell). Se usa en
 * los bucles de la flota, que tocan cientos de celdas por frame y necesitan
 * caber en el VBLANK (sino la flota PARPADEA al moverse). */
#define PUT_CELL_FAST(col, row, tile) \
    VC_WRITE(VC_AREA_TILEMAP, VC_CELL((col), (row)), (tile))

/* Restaura la celda (col,row) con el fondo estatico, version rapida. */
#define RESTORE_CELL_FAST(col, row) \
    VC_WRITE(VC_AREA_TILEMAP, VC_CELL((col), (row)), bg_at((col), (row)))

/* ===========================================================================
 * FLOTA: dibujo y movimiento
 * =========================================================================== */

/* Tile de un invasor segun su fila (tipo) y el frame de animacion actual. */
static uint8_t invader_tile(uint8_t row, uint8_t frame) {
    /* fila 0 = tipo A, filas 1-2 = tipo B, filas 3-4 = tipo C */
    uint8_t type;
    if (row == 0)            type = 0;
    else if (row <= 2)       type = 1;
    else                     type = 2;
    return (uint8_t)(TILE_INV_BASE + type * INVADER_FRAMES + frame);
}

/* Columna de la celda de la columna c de la flota (con hueco entre invasores). */
#define FLEET_CELL_COL(c) ((uint8_t)(fleet_col + (c) * FLEET_STRIDE))
/* Fila de la celda de la fila r de la flota (con hueco entre filas). */
#define FLEET_CELL_ROW(r) ((uint8_t)(fleet_row + (r) * FLEET_ROW_STRIDE))

/* Devuelve la fila LOGICA mas baja de la rejilla que TIENE invasores vivos
 * (0..FLEET_ROWS-1), o -1 si no queda ninguno. */
static int8_t fleet_bottom_logical_row(void) {
    int8_t r;

    for (r = (int8_t)(FLEET_ROWS - 1); r >= 0; r--) {
        if (fleet_alive[r] != 0) return r;
    }
    return -1;
}

/* Devuelve la fila FISICA (celda del mapa) del invasor VIVO mas bajo, o
 * fleet_row si no queda ninguno. Es la ultima fila que hay que tocar. */
static uint8_t fleet_bottom_cell(void) {
    int8_t lr = fleet_bottom_logical_row();

    if (lr < 0) return fleet_row;
    return (uint8_t)(fleet_row + lr * FLEET_ROW_STRIDE);
}

/* Columna LOGICA mas a la izquierda con algun invasor vivo (0..FLEET_COLS-1),
 * o -1 si no queda ninguno. Mira la mascara, no el ancho fijo de la rejilla. */
static int8_t fleet_left_logical_col(void) {
    uint8_t c, r;

    for (c = 0; c < FLEET_COLS; c++) {
        for (r = 0; r < FLEET_ROWS; r++) {
            if (fleet_alive[r] & (uint16_t)(1u << c)) return (int8_t)c;
        }
    }
    return -1;
}

/* Columna LOGICA mas a la derecha con algun invasor vivo, o -1 si no queda. */
static int8_t fleet_right_logical_col(void) {
    int8_t c;
    uint8_t r;

    for (c = (int8_t)(FLEET_COLS - 1); c >= 0; c--) {
        for (r = 0; r < FLEET_ROWS; r++) {
            if (fleet_alive[r] & (uint16_t)(1u << (uint8_t)c)) return c;
        }
    }
    return -1;
}

/* Columna FISICA (celda del mapa) del invasor vivo mas a la izquierda, o
 * fleet_col si no queda ninguno. */
static uint8_t fleet_left_cell(void) {
    int8_t lc = fleet_left_logical_col();

    if (lc < 0) return fleet_col;
    return (uint8_t)(fleet_col + lc * FLEET_STRIDE);
}

/* Columna FISICA del invasor vivo mas a la derecha, o la ultima de la rejilla. */
static uint8_t fleet_right_cell(void) {
    int8_t rc = fleet_right_logical_col();

    if (rc < 0) return (uint8_t)(fleet_col + FLEET_WIDTH - 1);
    return (uint8_t)(fleet_col + rc * FLEET_STRIDE);
}

/* Escribe en el tilemap la rejilla de invasores en su posicion actual.
 * Solo recorre las filas hasta el invasor vivo mas bajo (las filas de abajo
 * pueden estar ya vacias: no hay que tocarlas).
 * Usa escritura RAPIDA (VC_WRITE). */
static void fleet_draw(void) {
    uint8_t r, i;
    int8_t  lr = fleet_bottom_logical_row();

    if (lr < 0) return;   /* no hay invasores */

    for (r = 0; r <= (uint8_t)lr; r++) {
        uint16_t alive = fleet_alive[r];
        uint8_t  tile  = invader_tile(r, fleet_frame);
        uint8_t  row   = FLEET_CELL_ROW(r);

        /* i = celda relativa dentro del ancho de la flota (0..FLEET_WIDTH-1).
         * Con FLEET_STRIDE=2, un invasor ocupa las celdas PARES (i&1 == 0) y su
         * columna logica es i>>1. Se usa mascara/shift (rapido) en vez de %% y /. */
        for (i = 0; i < FLEET_WIDTH; i++) {
            uint8_t col = (uint8_t)(fleet_col + i);
            if (((i & 1u) == 0) && (alive & (1u << (i >> 1)))) {
                PUT_CELL_FAST(col, row, tile);
            } else {
                RESTORE_CELL_FAST(col, row);
            }
        }
    }
}

/* Borra la rejilla de invasores: restaura el fondo estatico hasta el invasor
 * vivo mas bajo (NO toca las filas de abajo ya vacias, que si no barrierian los
 * ESCUDOS al descender). */
static void fleet_erase(void) {
    uint8_t r, i;
    uint8_t bottom = fleet_bottom_cell();

    for (r = fleet_row; r <= bottom; r++) {
        for (i = 0; i < FLEET_WIDTH; i++) {
            RESTORE_CELL_FAST((uint8_t)(fleet_col + i), r);
        }
    }
}

/* Devuelve 1 si mover la flota dx columnas la sacaria del area jugable.
 * Usa los bordes REALES (invasores vivos), no el ancho fijo de la rejilla: si
 * se limpia la columna izquierda/derecha entera, la flota puede llegar hasta el
 * borde de la pantalla. */
static uint8_t fleet_would_hit_edge(int8_t dx) {
    uint8_t left  = (uint8_t)(fleet_left_cell() + dx);
    uint8_t right = (uint8_t)(fleet_right_cell() + dx);
    /* Margen de 1 celda a cada lado; la columna 0 se reserva (pipeline). */
    return ((int8_t)left < 1 || right > (VC_SCREEN_COLS - 2));
}

/* Retardo actual entre pasos de la flota, en frames.
 * Combina dos factores:
 *   - fleet_step_frames: dificultad base del NIVEL (baja al subir de nivel).
 *   - invasores vivos: ACELERA algo conforme quedan menos (como el arcade).
 *
 * La aceleracion intra-nivel es PROGRESIVA y perceptible: el suelo es ~18% del
 * base del nivel, de modo que con pocos invasores la flota es claramente mas
 * rapida (desafio), sin llegar a ser imposible. La velocidad brutal se reserva
 * para NIVELES ALTOS: como fleet_step_frames baja en cada nivel, el 18% de un
 * nivel alto ya es muy rapido.
 *
 *   bucket:  0    1    2    3    4    5    6    7    8    9   10   11
 *   vivos:  55+  50   45   38   31   24   18   13    9    6    3    1
 *   %base: 100   92   84   75   66   57   48   40   33   27   22   18   (1/100)
 */
static uint8_t fleet_step_delay(void) {
    uint16_t d;
    uint8_t  bucket;

    if (fleet_count == 0) return FLEET_STEP_MIN;

    /* Bucket segun invasores vivos (0 = muchos, 11 = casi ninguno). */
    if      (fleet_count >= 55) bucket = 0;
    else if (fleet_count >= 50) bucket = 1;
    else if (fleet_count >= 45) bucket = 2;
    else if (fleet_count >= 38) bucket = 3;
    else if (fleet_count >= 31) bucket = 4;
    else if (fleet_count >= 24) bucket = 5;
    else if (fleet_count >= 18) bucket = 6;
    else if (fleet_count >= 13) bucket = 7;
    else if (fleet_count >= 9)  bucket = 8;
    else if (fleet_count >= 6)  bucket = 9;
    else if (fleet_count >= 3)  bucket = 10;
    else                        bucket = 11;

    {
        static const uint8_t pct[12] = {
            100, 92, 84, 75, 66, 57, 48, 40, 33, 27, 22, 18
        };
        d = u16_div_u8((uint16_t)fleet_step_frames * pct[bucket], 100);
    }

    /* Minimo absoluto (seguridad de VBLANK). Con pocos invasores el redibujado
     * es barato, asi que se puede bajar bastante. */
    if (d < FLEET_STEP_MIN) d = FLEET_STEP_MIN;

    return (uint8_t)d;
}

/* Avanza la flota: un paso lateral o un descenso si toca borde. */
static void fleet_step(void) {
    /* Latido de la marcha (SID voz 1). La "velocidad" (1=rapido..8=lento)
     * se deriva del retardo actual: mas lento -> tono mas grave. */
    {
        uint8_t sp = (uint8_t)(fleet_step_delay() >> 3);   /* 4->0, 48->6 */
        snd_march((uint8_t)(1 + (sp > 7 ? 7 : sp)));       /* 1..7 */
    }

    /* Alterna el frame de animacion ANTES de dibujar, para que el paso
     * muestre el nuevo frame (marcha animada). */
    fleet_frame ^= 1;

    if (fleet_would_hit_edge(fleet_dx)) {
        /* Toca borde: BAJA una fila y cambia de direccion. */
        uint8_t top = fleet_row;

        fleet_erase();

        fleet_row = (uint8_t)(fleet_row + FLEET_ROW_DESC);
        fleet_dx  = (int8_t)-fleet_dx;

        {
            uint8_t i;
            for (i = 0; i < FLEET_WIDTH; i++) {
                RESTORE_CELL_FAST((uint8_t)(fleet_col + i), top);
            }
        }

        /* Game over cuando el invasor VIVO mas bajo alcanza la NAVE. */
        if ((uint8_t)(fleet_bottom_cell() * 8 + 7) >= SHIP_Y) {
            game_over = 1;
            snd_game_over();
        }

        fleet_draw();
    } else {
        /* Paso lateral: OPTIMIZADO. Solo se borra la COLUMNA que la flota
         * abandona (y solo hasta el invasor vivo mas bajo), y luego se redibuja
         * la rejilla ya desplazada. Asi el trabajo por paso baja mucho y cabe
         * en el VBLANK (sin parpadeo). */
        {
            uint8_t r, bottom = fleet_bottom_cell();
            uint8_t old_edge = (fleet_dx > 0)
                             ? fleet_col
                             : (uint8_t)(fleet_col + FLEET_WIDTH - 1);
            for (r = fleet_row; r <= bottom; r++) {
                RESTORE_CELL_FAST(old_edge, r);
            }
        }
        fleet_col = (uint8_t)(fleet_col + fleet_dx);
        fleet_draw();
    }
}

/* ===========================================================================
 * BALAS
 * =========================================================================== */

/* Declarada antes de su uso en update_enemy_bullets(). */
static void ship_hit(void);
static uint16_t invader_points(uint8_t r);
static void spawn_explosion(uint16_t x, uint8_t y, uint8_t frames, uint8_t big);
static uint8_t shield_hit_cell(uint8_t col, uint8_t row);

/* Devuelve 1 si la celda (col,row) contiene un invasor de la flota. */
static uint8_t cell_has_invader(uint8_t col, uint8_t row) {
    uint8_t r, c;

    if (row < fleet_row || row >= (uint8_t)(fleet_row + FLEET_HEIGHT)) return 0;
    if (col < fleet_col || col >= (uint8_t)(fleet_col + FLEET_WIDTH)) return 0;

    r = (uint8_t)(row - fleet_row);
    c = (uint8_t)(col - fleet_col);
    /* Solo las filas/columnas multiplo del stride tienen invasor. */
    if (r & 1u) return 0;                     /* r % 2 (FLEET_ROW_STRIDE=2) */
    if (c & 1u) return 0;                     /* c % 2 (FLEET_STRIDE=2)     */
    return (fleet_alive[r >> 1] >> (c >> 1)) & 1u;
}

/* Mata al invasor de la celda (col,row): limpia su bit y redibuja la celda. */
static void kill_invader(uint8_t col, uint8_t row) {
    uint8_t r = (uint8_t)((row - fleet_row) >> 1);   /* / FLEET_ROW_STRIDE(2) */
    uint8_t c = (uint8_t)((col - fleet_col) >> 1);   /* / FLEET_STRIDE(2)     */

    fleet_alive[r] &= (uint16_t)~(1u << c);
    cell_restore(col, row);
    snd_invader_die();
    if (fleet_count) fleet_count--;
}

/* Mueve las balas y resuelve colisiones bala<->invasor. */
static void update_bullets(void) {
    uint8_t i;

    for (i = 0; i < NBULLETS; i++) {
        uint8_t slot = (uint8_t)(SPR_BULLET0 + i);

        if (!b_alive[i]) continue;

        /* La bala sube; si sale por arriba, se apaga. */
        if (b_y[i] <= BULLET_Y_MIN) {
            b_alive[i] = 0;
            vc_sprite_disable(slot);
            continue;
        }
        b_y[i] = (uint8_t)(b_y[i] - BULLET_SPEED);

        /* Colision bala<->invasor: convierte la punta de la bala a celda. */
        {
            uint8_t col = (uint8_t)((b_x[i] + (BULLET_W / 2)) >> 3);
            uint8_t row = (uint8_t)(b_y[i] >> 3);

            if (cell_has_invader(col, row)) {
                /* Puntos segun la fila del invasor (se deduce del row y fleet_row). */
                uint8_t fr = (uint8_t)((row - fleet_row) >> 1);  /* / STRIDE(2) */
                score = (uint16_t)(score + invader_points(fr));
                spawn_explosion((uint16_t)(col * 8), (uint8_t)(row * 8),
                                EXPLODE_FRAMES, 0);
                kill_invader(col, row);
                b_alive[i] = 0;
                vc_sprite_disable(slot);
                continue;
            }
            /* La bala tambien puede chocar con un escudo (lo destruye). */
            if (shield_hit_cell(col, row)) {
                b_alive[i] = 0;
                vc_sprite_disable(slot);
                continue;
            }
        }

        vc_sprite_move(slot, b_x[i], b_y[i], VC_SPPAL_1);
        vc_oam_put(slot, VC_OAM_TILE, SPR_PAT_BULLET);
    }
}

/* Puntos que vale un invasor segun su fila (los de arriba valen mas). */
static uint16_t invader_points(uint8_t r) {
    if (r == 0) return 30;          /* tipo A (calamar) */
    if (r <= 2) return 20;          /* tipo B (cangrejo) */
    return 10;                      /* tipo C (pulpo) */
}

/* ===========================================================================
 * EXPLOSIONES
 * ===========================================================================
 * Efecto visual efimero: un sprite que aparece en el punto del impacto y se
 * apaga tras EXPLODE_FRAMES. Pool de NEXPLOSIONS.
 * =========================================================================== */

/* Lanza una explosion en (x,y) que dura 'frames'. Si no hay slot libre, no
 * hace nada. Para la nave se usa una duracion larga (= la pausa de muerte) y
 * big=1, para dibujarla a 2x (proporcional a la nave de 16x16). */
static void spawn_explosion(uint16_t x, uint8_t y, uint8_t frames, uint8_t big) {
    uint8_t i;

    for (i = 0; i < NEXPLOSIONS; i++) {
        if (x_t[i] == 0) {
            x_x[i] = x;
            x_y[i] = y;
            x_t[i] = frames;
            x_big[i] = big;
            return;
        }
    }
}

/* Avanza el temporizador de las explosiones y actualiza sus sprites. */
static void update_explosions(void) {
    uint8_t i;

    for (i = 0; i < NEXPLOSIONS; i++) {
        uint8_t slot = (uint8_t)(SPR_EXPLODE0 + i);

        if (x_t[i] == 0) continue;   /* inactiva */

        x_t[i]--;
        if (x_t[i] == 0) {
            vc_sprite_disable(slot);
        } else if (x_big[i]) {
            /* Explosion de la nave: 2x (16x16), centrada en su posicion. */
            vc_sprite_move(slot, x_x[i], x_y[i], VC_SPPAL_3 | VC_SPR_SCALE2X);
            vc_oam_put(slot, VC_OAM_TILE, SPR_PAT_EXPLODE);
        } else {
            vc_sprite_move(slot, x_x[i], x_y[i], VC_SPPAL_3);
            vc_oam_put(slot, VC_OAM_TILE, SPR_PAT_EXPLODE);
        }
    }
}

/* ===========================================================================
 * UFO (nave nodriza)
 * ===========================================================================
 * Cruza periodicamente la parte alta (encima de la flota). Si le da la bala
 * del jugador, otorga UFO_POINTS. Se dibuja con 2 sprites contiguos: la mitad
 * izquierda (patron normal) y la derecha (mismo patron con FLIP_X).
 * ========================================================================= */

/* Dibuja (o mueve) los 2 sprites del UFO en su X actual. */
static void ufo_draw(void) {
    uint8_t flags_l = VC_SPPAL_3;
    uint8_t flags_r = (uint8_t)(VC_SPPAL_3 | VC_SPR_FLIP_X);

    vc_sprite_move(SPR_UFO, ufo_x, UFO_Y, flags_l);
    vc_oam_put(SPR_UFO, VC_OAM_TILE, SPR_PAT_UFO);
    vc_sprite_move((uint8_t)(SPR_UFO + 1), (uint16_t)(ufo_x + 8), UFO_Y, flags_r);
    vc_oam_put((uint8_t)(SPR_UFO + 1), VC_OAM_TILE, SPR_PAT_UFO);
}

/* Oculta los 2 sprites del UFO. */
static void ufo_hide(void) {
    vc_sprite_disable(SPR_UFO);
    vc_sprite_disable((uint8_t)(SPR_UFO + 1));
}

/* Programa la proxima aparicion del UFO. Como el arcade, se liga a los
 * invasores VIVOS: con muchos invasores espera bastante; con pocos, aparece
 * mas seguido. ufo_wait va en UNIDADES DE 8 FRAMES (asi un byte cubre hasta
 * 255*8 = 2040 frames ~= 34 s). */
static void ufo_schedule(void) {
    uint8_t base;

    /* Base segun invasores vivos: menos invasores -> aparece antes (arcade). */
    base = (fleet_count >= 25) ? 180 : ((fleet_count >= 12) ? 110 : 64);
    ufo_wait = (uint8_t)(base + u8_mod(rng_next(), 40));
    ufo_div  = 8;              /* unidad de 8 frames */
}

/* Aparece el UFO: sale por un borde al azar y cruza hacia el otro. */
static void ufo_spawn(void) {
    if (rng_next() & 1) {
        ufo_dir = 1;
        ufo_x   = 0;                      /* desde la izquierda */
    } else {
        ufo_dir = -1;
        ufo_x   = (uint16_t)(VC_SCREEN_W - UFO_W);   /* desde la derecha */
    }
    ufo_active = 1;
    ufo_draw();
    snd_ufo_start();     /* arranca el zumbido del platillo */
}

/* Inicializa el estado del UFO (al empezar partida/nivel). */
static void ufo_init(void) {
    ufo_active = 0;
    ufo_x      = 0;
    ufo_dir    = 1;
    ufo_hide();
    ufo_schedule();
}

/* Actualiza el UFO cada frame: aparicion, movimiento y colision con la bala. */
static void update_ufo(void) {
    if (!ufo_active) {
        /* Cuenta en unidades de 8 frames (ufo_wait llega a ~200). */
        if (--ufo_div == 0) {
            ufo_div = 8;
            if (ufo_wait) ufo_wait--;
            else          ufo_spawn();
        }
        return;
    }

    /* Mover 1 px. Se comprueba el borde ANTES de salir de rango para no
     * necesitar aritmetica con signo (ufo_x es uint16, X de 9 bits). */
    if (ufo_dir > 0) {
        if (ufo_x >= (uint16_t)(VC_SCREEN_W - 1)) {
            ufo_active = 0; ufo_hide(); snd_ufo_stop(); ufo_schedule(); return;
        }
        ufo_x++;
    } else {
        if (ufo_x == 0) {
            ufo_active = 0; ufo_hide(); snd_ufo_stop(); ufo_schedule(); return;
        }
        ufo_x--;
    }
    ufo_draw();

    /* Colision con la bala del jugador: comparacion directa de rangos. */
    {
        uint8_t i;
        for (i = 0; i < NBULLETS; i++) {
            if (!b_alive[i]) continue;
            if (b_x[i] + 8 <= ufo_x) continue;
            if (b_x[i] >= ufo_x + UFO_W) continue;
            if (b_y[i] + 8 <= UFO_Y) continue;
            if (b_y[i] >= UFO_Y + 8) continue;
            score = (uint16_t)(score + UFO_POINTS);
            spawn_explosion(ufo_x, UFO_Y, EXPLODE_FRAMES, 1);
            b_alive[i] = 0;
            vc_sprite_disable((uint8_t)(SPR_BULLET0 + i));
            ufo_active = 0;
            ufo_hide();
            snd_ufo_stop();
            snd_invader_die();
            ufo_schedule();
            return;
        }
    }
}

/* ===========================================================================
 * ESCUDOS (bunkers)
 * ===========================================================================
 * NUM_SHIELDS bloques de tiles destructibles. Cada celda viva tiene un bit en
 * shield_alive[]. Una bala (de cualquier bando) que toca una celda viva la
 * destruye. El escudo no se puede reparar.
 * =========================================================================== */

/* Columna (del mapa) de la esquina izquierda del escudo s. */
static uint8_t shield_col(uint8_t s) {
    return (uint8_t)(SHIELD_X0 + s * SHIELD_GAP);
}

/* Devuelve el tile correspondiente a un bloque de la plantilla (1..5). */
static uint8_t shield_block_tile(uint8_t blk) {
    switch (blk) {
        case 1: return TILE_SHIELD;      /* esquina sup-izq */
        case 2: return TILE_SHIELD_B2;   /* esquina sup-der */
        case 3: return TILE_SHIELD_B3;   /* pata inf-izq   */
        case 4: return TILE_SHIELD_B4;   /* pata inf-der   */
        case 5: return TILE_SHIELD_B5;   /* lleno          */
        default: return 0;               /* 0 = vacio      */
    }
}

/* Dibuja en el tilemap las celdas vivas de todos los escudos.
 * La forma la da shield_shape[]; shield_alive[] marca lo que sigue en pie. */
static void shields_draw(void) {
    uint8_t s, r, c;

    for (s = 0; s < NUM_SHIELDS; s++) {
        uint8_t col0 = shield_col(s);
        for (r = 0; r < SHIELD_H; r++) {
            uint8_t row = (uint8_t)(SHIELD_Y + r);
            uint8_t alive = shield_alive[s][r];
            for (c = 0; c < SHIELD_W; c++) {
                uint8_t blk = shield_shape[r][c];
                uint8_t col = (uint8_t)(col0 + c);

                if (blk != 0 && (alive & (1u << c))) {
                    vc_put_cell(col, row, shield_block_tile(blk));
                    /* Tinta verde: paleta 2 (vegetacion), color 3 = verde.
                     * NO usar paleta 3: su color 3 es la entrada 15, que ES
                     * BG_COLOR (se pinta del color del fondo, no verde). */
                    vc_set_cell_attr(col, row, VC_BGPAL_2, 0);
                } else {
                    /* Hueco de la forma o celda destruida: restaura el fondo. */
                    cell_restore(col, row);
                    vc_set_cell_attr(col, row, VC_BGPAL_0, 0);
                }
            }
        }
    }
}

/* Inicializa los escudos: solo las celdas de la forma estan vivas. */
static void shields_init(void) {
    uint8_t s, r, c;

    for (s = 0; s < NUM_SHIELDS; s++) {
        for (r = 0; r < SHIELD_H; r++) {
            uint8_t m = 0;
            for (c = 0; c < SHIELD_W; c++) {
                if (shield_shape[r][c] != 0) m |= (uint8_t)(1u << c);
            }
            shield_alive[s][r] = m;
        }
    }
    shields_draw();
}

/* Si la celda (col,row) es un escudo vivo, lo destruye y devuelve 1.
 * Si no, devuelve 0. */
static uint8_t shield_hit_cell(uint8_t col, uint8_t row) {
    uint8_t s, r, c;

    if (row < SHIELD_Y || row >= (uint8_t)(SHIELD_Y + SHIELD_H)) return 0;

    for (s = 0; s < NUM_SHIELDS; s++) {
        uint8_t col0 = shield_col(s);
        if (col < col0 || col >= (uint8_t)(col0 + SHIELD_W)) continue;

        r = (uint8_t)(row - SHIELD_Y);
        c = (uint8_t)(col - col0);
        if (shield_alive[s][r] & (1u << c)) {
            shield_alive[s][r] &= (uint8_t)~(1u << c);
            vc_put_cell(col, row, TILE_EMPTY);   /* deja negro */
            vc_set_cell_attr(col, row, VC_BGPAL_0, 0);  /* attr por defecto */
            return 1;
        }
        return 0;
    }
    return 0;
}

/* ===========================================================================
 * BALAS ENEMIGAS
 * ===========================================================================
 * Un invasor dispara: se elige una columna viva al azar y, de esa columna, el
 * invasor VIVO mas bajo (el mas cercano a la nave). La bala baja hasta el suelo.
 * =========================================================================== */

/* Busca la fila viva mas baja de la columna c. Devuelve 1 si la encontro y
 * deja el indice de fila en *out_row. */
static uint8_t lowest_alive_in_col(uint8_t c, uint8_t *out_row) {
    int8_t r;

    for (r = (int8_t)(FLEET_ROWS - 1); r >= 0; r--) {
        if ((fleet_alive[r] >> c) & 1u) {
            *out_row = (uint8_t)r;
            return 1;
        }
    }
    return 0;
}

/* Intenta disparar una bala enemiga desde la flota. Devuelve 1 si disparó. */
static uint8_t enemy_fire(void) {
    uint8_t i, c, tries;
    uint8_t r = 0;

    /* Busca un slot libre. */
    for (i = 0; i < NEBULLETS; i++) {
        if (!e_alive[i]) break;
    }
    if (i == NEBULLETS) return 0;

    /* Prueba columnas al azar hasta encontrar una con invasores vivos. */
    for (tries = 0; tries < FLEET_COLS; tries++) {
        c = u8_mod(rng_next(), FLEET_COLS);
        if (lowest_alive_in_col(c, &r)) {
            /* Celda del invasor que dispara. */
            uint8_t col = FLEET_CELL_COL(c);
            uint8_t row = FLEET_CELL_ROW(r);

            e_x[i]     = (uint16_t)(col * 8 + 4 - (BULLET_W / 2));
            e_y[i]     = (uint8_t)((row + 1) * 8);
            e_alive[i] = 1;
            vc_sprite_move((uint8_t)(SPR_EBULLET0 + i), e_x[i], e_y[i],
                           VC_SPPAL_2);
            vc_oam_put((uint8_t)(SPR_EBULLET0 + i), VC_OAM_TILE, SPR_PAT_EBULLET);
            return 1;
        }
    }
    return 0;
}

/* Mueve las balas enemigas y comprueba colision con la nave. */
static void update_enemy_bullets(void) {
    uint8_t i;

    for (i = 0; i < NEBULLETS; i++) {
        uint8_t slot = (uint8_t)(SPR_EBULLET0 + i);

        if (!e_alive[i]) continue;

        /* Baja; si llega al suelo, se apaga. */
        if (e_y[i] >= EBULLET_Y_MAX) {
            e_alive[i] = 0;
            vc_sprite_disable(slot);
            continue;
        }
        e_y[i] = (uint8_t)(e_y[i] + EBULLET_SPEED);

        /* Colision bala enemiga <-> nave: solo si la nave no es invulnerable.
         * La caja del misil es ESTRECHA (la barra visible ocupa ~2px centrada
         * en el tile 8x8); usar una caja de 8px daria falsos positivos. */
        if (!blink) {
            vc_box_t bb, sb;
            bb.x = (uint16_t)(e_x[i] + 3);
            bb.y = (uint16_t)(e_y[i] + 1);
            bb.w = 2;
            bb.h = 5;                              /* alto de la barra del misil */
            vc_box_from_sprite(&sb, ship_x, SHIP_Y, SHIP_W);
            if (vc_box_overlap(&bb, &sb)) {
                e_alive[i] = 0;
                vc_sprite_disable(slot);
                ship_hit();
                continue;
            }
        }

        /* La bala enemiga tambien destruye escudos. */
        {
            uint8_t col = (uint8_t)((e_x[i] + (BULLET_W / 2)) >> 3);
            uint8_t row = (uint8_t)(e_y[i] >> 3);
            if (shield_hit_cell(col, row)) {
                e_alive[i] = 0;
                vc_sprite_disable(slot);
                continue;
            }
        }

        vc_sprite_move(slot, e_x[i], e_y[i], VC_SPPAL_2);
        vc_oam_put(slot, VC_OAM_TILE, SPR_PAT_EBULLET);
    }
}

/* Columnas del HUD (fila 2). Se usan en todos los sitios para que el valor de
 * cada marcador se actualice en la MISMA columna donde se dibujo al inicio. */
#define HUD_SCORE_COL   8
#define HUD_LIVES_COL   24
#define HUD_LEVEL_COL   31
#define HUD_HI_COL      36

/* ===========================================================================
 * JUGADOR: vidas y golpes
 * =========================================================================== */

/* Sube el marcador de vidas en el HUD (col,row). */
static void put_lives(uint8_t col, uint8_t row);

/* La nave recibe un impacto: pierde una vida y empieza un respawn con parpadeo.
 * Si no quedan vidas, fin de partida. */
static void ship_hit(void) {
    /* Explosion LARGA (a 2x) en la posicion de la nave (dura toda la pausa de
     * muerte) + sonido de nave destruida. */
    spawn_explosion(ship_x, SHIP_Y, DEATH_PAUSE, 1);
    snd_ship_die();

    /* Oculta la nave y aparca las balas enemigas vivas. */
    vc_sprite_disable(SPR_SHIP);
    {
        uint8_t i;
        for (i = 0; i < NEBULLETS; i++) {
            e_alive[i] = 0;
            vc_sprite_disable((uint8_t)(SPR_EBULLET0 + i));
        }
    }

    if (lives) lives--;
    put_lives(HUD_LIVES_COL, 2);

    if (lives == 0) {
        game_over = 1;
        return;
    }

    /* La nave reaparece DONDE ESTABA (como el arcade original): no se recentra.
     * Antes de revivir hay una PAUSA (death_pause) durante la cual no hay nave y
     * el juego se queda quieto, para que se note la muerte. */
    death_pause = DEATH_PAUSE;
}

/* ===========================================================================
 * SETUP
 * =========================================================================== */
static void setup_video(void) {
    uint8_t x, y;

    vc_wait_ready();
    vc_wait_vblank();
    vc_clear_vram();

    /* FONDO NEGRO: BG_COLOR es la entrada 15 de la paleta de fondo (manual §4.3).
     * Es el color que se ve donde el tile tiene color 0 (transparente), como los
     * huecos de la fuente de texto. Poniendolo negro, el texto y los tiles se ven
     * sobre negro sin necesitar fuentes propias. Se hace en VBLANK. */
    vc_set_bgcolor(VC_RGB444(0x0, 0x0, 0x0));

    /* Paleta 0 del fondo: la usan los tiles de juego (fondo negro = color 1) y
     * la fuente de texto (tinta = color 3 blanco). */
    vc_pal_set_bg(0, 1, VC_RGB444(0x0, 0x0, 0x0));   /* color 1 = negro  */
    vc_pal_set_bg(0, 2, VC_RGB444(0x0, 0xC, 0xF));   /* color 2 = cian   */
    vc_pal_set_bg(0, 3, VC_RGB444(0xF, 0xF, 0xF));   /* color 3 = blanco */

    /* Paleta 2 (la usan los escudos y el texto): color 3 = verde (tinta), y
     * color 1 = NEGRO. Los tiles del escudo usan plano0=0xFF, asi que sus
     * esquinas/arcos (que debian ser "huecos") quedan en color 1; poniendolo
     * negro se funden con el fondo en vez de verse verdes. */
    vc_pal_set_bg(2, 1, VC_RGB444(0x0, 0x0, 0x0));   /* color 1 = negro  */
    vc_pal_set_bg(2, 3, VC_RGB444(0x0, 0xA, 0x0));   /* color 3 = verde  */

    /* --- Carga de patrones por tabla (compacto en vez de 20 llamadas) ---
     * Cada entrada: { destino (tile o patron), plano0, plano1 }.
     * Cargar por bucle ahorra el codigo repetido de cc65 (pusha/pushax x3
     * por llamada). */
    {
        static const struct { uint8_t dst; const uint8_t *p0; const uint8_t *p1; } bgpats[] = {
            { TILE_EMPTY,     tile_space_p0, tile_space_p1 },
            { TILE_STAR,      tile_star_p0,  tile_star_p1  },
            { TILE_FLOOR,     tile_floor_p0, tile_floor_p1 },
            { TILE_SHIELD,    sh_b1_p0, sh_b1_p1 },
            { TILE_SHIELD_B2, sh_b2_p0, sh_b2_p1 },
            { TILE_SHIELD_B3, sh_b3_p0, sh_b3_p1 },
            { TILE_SHIELD_B4, sh_b4_p0, sh_b4_p1 },
            { TILE_SHIELD_B5, sh_b5_p0, sh_b5_p1 },
            { TILE_INV_BASE + 0, inv_p0, inv_a0_p1 },
            { TILE_INV_BASE + 1, inv_p0, inv_a1_p1 },
            { TILE_INV_BASE + 2, inv_p0, inv_b0_p1 },
            { TILE_INV_BASE + 3, inv_p0, inv_b1_p1 },
            { TILE_INV_BASE + 4, inv_p0, inv_c0_p1 },
            { TILE_INV_BASE + 5, inv_p0, inv_c1_p1 },
        };
        static const struct { uint8_t dst; const uint8_t *p0; const uint8_t *p1; } sprpats[] = {
            { SPR_SHIP,         ship_p0,    ship_p1    },
            { SPR_PAT_BULLET,   bullet_p0,  bullet_p1  },
            { SPR_PAT_EBULLET,  ebullet_p0, ebullet_p1 },
            { SPR_PAT_EXPLODE,  explode_p0, explode_p1 },
            { SPR_PAT_UFO,      ufo_p0,     ufo_p1     },
        };
        uint8_t i;
        for (i = 0; i < (uint8_t)(sizeof(bgpats) / sizeof(bgpats[0])); i++) {
            vc_load_bg_pattern(bgpats[i].dst, bgpats[i].p0, bgpats[i].p1);
        }
        for (i = 0; i < (uint8_t)(sizeof(sprpats) / sizeof(sprpats[0])); i++) {
            vc_load_spr_pattern(sprpats[i].dst, sprpats[i].p0, sprpats[i].p1);
        }
    }

    /* Mundo: fondo negro (tile 0) + estrellas + suelo. Las estrellas se marcan
     * en el bitmap bg_stars[] (RAM); el suelo se deduce de la fila. Luego se
     * vuelca la zona visible al tilemap, y asi se puede restaurar al pisarla. */
    {
        uint8_t i;
        for (i = 0; i < BG_STAR_BYTES; i++) bg_stars[i] = 0;
        for (i = 0; i < NSTARS; i++) {
            uint8_t sx = stars[i].x;
            uint8_t sy = stars[i].y;
            if (sx < BG_COLS && sy < BG_ROWS) {
                bg_star_set(sx, sy);
            }
        }
    }

    /* Volcado del fondo a la zona visible del tilemap + atributo del suelo. */
    for (y = 0; y < BG_ROWS; y++) {
        for (x = 0; x < BG_COLS; x++) {
            vc_put_cell(x, y, bg_at(x, y));
        }
    }
    for (x = 0; x < BG_COLS; x++) {
        vc_set_cell_attr(x, BG_ROWS - 1, VC_BGPAL_1, VC_ATTR_SOLID);
    }
    /* El resto del tilemap (columnas 40..63 y filas 30..31) NO hace falta
     * tocarlo: vc_clear_vram() ya lo dejo a TILE_EMPTY(0), y como el juego no
     * usa scroll, esas celdas nunca se ven. (Antes habia 2 bucles aqui.) */

    /* HUD: banda superior fija (filas 0-2) y banda inferior fija (filas 27-29). */
    vc_set_raster(24, 216);
    vc_set_band2_scroll(0, 0);
    vc_set_band3_scroll(0, 0);

    put_str_center(1, "SPACE INVADERS");
    put_str_at(1, 28, "a/d mover SPACE dispara q salir");
}

static void setup_ship(void) {
    vc_sprite_t s;
    ship_x = 152;
    s.x_lo  = (uint8_t)ship_x;
    s.y     = (uint8_t)SHIP_Y;
    s.tile  = SPR_SHIP;
    s.flags = VC_SPPAL_3 | VC_SPR_SCALE2X;
    s.coll  = VC_COLL_CENTER;
    vc_sprite_set(SPR_SHIP, &s);
}

/* Inicializa la flota: todos vivos, esquina en (FLEET_LEFT0, FLEET_TOP0). */
static void setup_fleet(void) {
    uint8_t r;

    for (r = 0; r < FLEET_ROWS; r++) {
        /* 11 columnas -> bits 0..10 */
        fleet_alive[r] = (uint16_t)((1u << FLEET_COLS) - 1u);
    }

    fleet_col   = FLEET_LEFT0;
    fleet_row   = FLEET_TOP0;
    fleet_dx    = 1;
    fleet_frame = 0;
    fleet_tick  = fleet_step_frames;
    fleet_count = (uint8_t)(FLEET_ROWS * FLEET_COLS);

    fleet_draw();
}

/* Inicializa las balas (todas apagadas), vidas y puntuacion. */
static void setup_bullets(void) {
    uint8_t i;

    for (i = 0; i < NBULLETS; i++) {
        b_x[i]     = 0;
        b_y[i]     = 0;
        b_alive[i] = 0;
        vc_sprite_disable((uint8_t)(SPR_BULLET0 + i));
    }
    for (i = 0; i < NEBULLETS; i++) {
        e_x[i]     = 0;
        e_y[i]     = 0;
        e_alive[i] = 0;
        vc_sprite_disable((uint8_t)(SPR_EBULLET0 + i));
    }
    for (i = 0; i < NEXPLOSIONS; i++) {
        x_x[i] = 0;
        x_y[i] = 0;
        x_t[i] = 0;
        x_big[i] = 0;
        vc_sprite_disable((uint8_t)(SPR_EXPLODE0 + i));
    }
    score           = 0;
    lives           = SHIP_LIVES;
    blink           = 0;
    fire_cooldown   = 0;
    death_pause     = 0;
    game_over       = 0;
    enemy_fire_tick = ENEMY_FIRE_MIN;
    rng_state       = 0x2A5E;   /* semilla fija (reproducible) */
    level           = 1;
    fleet_step_frames = FLEET_STEP_FRAMES;
    ufo_init();
}

/* ===========================================================================
 * TEXTO
 * ===========================================================================
 * Usamos la fuente del SISTEMA (tile = ASCII). Sus huecos son color 0
 * (transparente) y muestran BG_COLOR, que hemos puesto NEGRO (§4.3), asi que
 * el texto se ve sobre negro sin fuente propia.
 *
 * COLOR: la fuente pinta SIEMPRE el color 3 de la paleta de la celda, asi que
 * el color de la letra lo elige la PALETA. Usamos la paleta 2 (vegetacion),
 * cuyo color 3 por defecto es VERDE. Para cambiar el color de todo el texto,
 * basta cambiar TEXTO_PAL de aqui abajo. */
#define TEXTO_PAL  VC_BGPAL_2   /* paleta 2: color 3 = verde */

/* Texto: implementado en text.s (ASM, escritura VRAM directa). Estas envolturas
 * mantienen la interfaz usada por el resto de game.c. */

/* Escribe una cadena con la fuente del sistema, con salto a 40 columnas. */
static void put_str_at(uint8_t col, uint8_t row, const char *s) {
    txt_put_at(col, row, s);
}

/* Longitud de una cadena (sin <string.h>). */
static uint8_t str_len(const char *s) {
    uint8_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Escribe una cadena CENTRADA horizontalmente (40 columnas). */
static void put_str_center(uint8_t row, const char *s) {
    uint8_t len = str_len(s);
    uint8_t col = (uint8_t)((VC_SCREEN_COLS - len) >> 1);   /* /2 = shift */
    put_str_at(col, row, s);
}

/* Borra una zona con espacios. */
static void clear_str_at(uint8_t col, uint8_t row, uint8_t len) {
    txt_clear_at(col, row, len);
}

/* ===========================================================================
 * HUD
 * =========================================================================== */

/* Escribe un numero de 4 digitos en (col,row). */
static void put_score(uint8_t col, uint8_t row) {
    txt_put_u16_4(col, row, score);
}

/* Escribe las vidas como un digito en (col,row). */
static void put_lives(uint8_t col, uint8_t row) {
    txt_put_u8_1(col, row, lives);
}

/* Sube el numero de nivel en el HUD (col,row). */
static void put_level(uint8_t col, uint8_t row) {
    txt_put_u8_1(col, row, level);
}

/* ===========================================================================
 * HIGH SCORES (solo RAM)
 * ===========================================================================
 * Tabla de HISCORE_N mejores puntuaciones, de mayor a menor. Se muestra el
 * mejor durante el juego (HUD) y los 5 en la pantalla de GAME OVER. */

/* Mejor puntuacion actual (0 si la tabla esta vacia). */
static uint16_t hiscore_top(void) {
    return hiscore_n ? hiscores[0] : 0;
}

/* Muestra la mejor puntuacion en el HUD (col,row). */
static void put_hiscore(uint8_t col, uint8_t row) {
    txt_put_u16_4(col, row, hiscore_top());
}

/* Inserta una puntuacion en la tabla, manteniendola ordenada y de tamano N. */
static void hiscore_add(uint16_t v) {
    uint8_t i, j;

    if (v == 0) return;

    /* Busca la posicion de insercion (primera entrada menor que v). */
    for (i = 0; i < hiscore_n; i++) {
        if (v > hiscores[i]) break;
    }
    if (i >= HISCORE_N) return;      /* no entra: peor que todas */

    /* Desplaza hacia abajo lo que quede por debajo. */
    j = (hiscore_n < HISCORE_N) ? hiscore_n : (HISCORE_N - 1);
    while (j > i) {
        hiscores[j] = hiscores[j - 1];
        j--;
    }
    hiscores[i] = v;
    if (hiscore_n < HISCORE_N) hiscore_n++;
}

/* Muestra la tabla completa de 5 mejores centrada, en la fila 'row'. */
static void show_hiscores(uint8_t row) {
    uint8_t i;

    put_str_center(row, "MEJORES PUNTUACIONES");
    for (i = 0; i < HISCORE_N; i++) {
        /* Columna de la linea centrada: cada linea son 6 caracteres "N. 0000"
         * (o "N. ----" si no hay entrada). */
        uint8_t line_row = (uint8_t)(row + 2 + i * 2);
        uint8_t c = (uint8_t)((VC_SCREEN_COLS - 6) >> 1);   /* /2 = shift */
        char pfx[4];                 /* "N. " + terminador nulo */
        pfx[0] = (char)('1' + i);
        pfx[1] = '.';
        pfx[2] = ' ';
        pfx[3] = 0;
        put_str_at(c, line_row, pfx);
        if (i < hiscore_n) {
            txt_put_u16_4((uint8_t)(c + 3), line_row, hiscores[i]);
        } else {
            put_str_at((uint8_t)(c + 3), line_row, "----");
        }
    }
}

/* Actualiza el parpadeo de invulnerabilidad y redibuja la nave.
 * Durante blink la nave alterna visible/oculta para avisar al jugador. */
static void update_ship_blink(void) {
    if (blink) {
        blink--;
        if (blink & 4) {
            vc_sprite_disable(SPR_SHIP);
        } else {
            vc_sprite_move(SPR_SHIP, ship_x, SHIP_Y, VC_SPPAL_3 | VC_SPR_SCALE2X);
            vc_oam_put(SPR_SHIP, VC_OAM_TILE, SPR_SHIP);
        }
    }
}

/* Apaga todas las balas (del jugador y enemigas) y las explosiones.
 * Se usa al cambiar de nivel para no arrastrar restos del nivel anterior. */
static void clear_projectiles(void) {
    uint8_t i;

    for (i = 0; i < NBULLETS; i++) {
        b_alive[i] = 0;
        vc_sprite_disable((uint8_t)(SPR_BULLET0 + i));
    }
    for (i = 0; i < NEBULLETS; i++) {
        e_alive[i] = 0;
        vc_sprite_disable((uint8_t)(SPR_EBULLET0 + i));
    }
    for (i = 0; i < NEXPLOSIONS; i++) {
        x_t[i] = 0;
        x_big[i] = 0;
        vc_sprite_disable((uint8_t)(SPR_EXPLODE0 + i));
    }
    /* El UFO tambien desaparece al cambiar de nivel/limpiar el tablero. */
    ufo_active = 0;
    ufo_hide();
    snd_ufo_stop();
}

/* Limpia el tablero de juego (flota, escudos, proyectiles y naves) para
 * empezar una partida nueva desde cero. No toca el HUD. */
static void reset_board(void) {
    /* Borra las celdas que usan la flota y los escudos restaurando el fondo. */
    uint8_t x, y;

    clear_projectiles();

    for (y = 0; y < BG_ROWS; y++) {
        for (x = 0; x < BG_COLS; x++) {
            vc_put_cell(x, y, bg_at(x, y));
            /* Restaura tambien el atributo (los escudos lo cambiaban). */
            vc_set_cell_attr(x, y, VC_BGPAL_0, 0);
        }
    }
    /* Vuelve a marcar el suelo como solido. */
    for (x = 0; x < BG_COLS; x++) {
        vc_set_cell_attr(x, BG_ROWS - 1, VC_BGPAL_1, VC_ATTR_SOLID);
    }
}

/* ===========================================================================
 * PARTIDA
 * ===========================================================================
 * Juega una partida completa (todos los niveles) hasta perder o salir con 'q'.
 * Devuelve 1 si el jugador perdio (game over) o 0 si salio con 'q'.
 * =========================================================================== */
static uint8_t play_game(void) {
    setup_bullets();      /* vidas, score, nivel 1, velocidad */
    setup_ship();
    snd_init();
    vc_wait_vblank();
    reset_board();
    vc_wait_vblank_end();
    quit_flag = 0;

    /* Titulo e instrucciones (se redibujan en cada partida: reset_board los borra). */
    put_str_center(1, "SPACE INVADERS");
    put_str_at(1, 28, "a/d mover SPACE dispara q salir");

    put_str_at(1, 2, "SCORE");
    put_str_at(18, 2, "LIVES");
    put_str_at(28, 2, "LV");
    put_str_at(33, 2, "HI");
    put_score(HUD_SCORE_COL, 2);
    put_lives(HUD_LIVES_COL, 2);
    put_level(HUD_LEVEL_COL, 2);
    put_hiscore(HUD_HI_COL, 2);

    /* Bucle de niveles: cada vez que se limpia la flota, sube de nivel y la
     * nueva flota marcha mas rapido. Se sale al perder (game over) o con 'q'. */
    while (!quit_flag && !game_over) {
        setup_fleet();
        shields_init();

        /* Bucle de un nivel: una iteracion = un frame. */
        while (!quit_flag && !game_over && fleet_count) {
            vc_wait_vblank();

            if (death_pause) {
                /* PAUSA tras morir: el juego se queda quieto (ni flota ni balas
                 * se mueven) mientras se ve la explosion en el sitio de la nave.
                 * Al terminar, empieza el parpadeo de invulnerabilidad. */
                update_explosions();
                snd_update();

                death_pause--;
                if (death_pause == 0) {
                    blink = RESPAWN_BLINK;   /* revive parpadeando */
                }

                vc_wait_vblank_end();
                continue;                    /* salta toda la logica del frame */
            }

            read_input();
            update_bullets();

            /* Disparo enemigo: cada ENEMY_FIRE_* frames, si quedan invasores. */
            if (--enemy_fire_tick == 0) {
                enemy_fire();
                enemy_fire_tick = (uint8_t)(ENEMY_FIRE_MIN +
                                  u8_mod(rng_next(),
                                         (ENEMY_FIRE_MAX - ENEMY_FIRE_MIN + 1)));
            }
            update_enemy_bullets();
            update_ship_blink();
            update_explosions();
            update_ufo();
            if (fire_cooldown) fire_cooldown--;
            snd_update();      /* gestiona las notas percutivas del SID */

            /* La flota da un paso cada fleet_step_delay() frames. El retardo
             * se recalcula cada vez: baja conforme quedan menos invasores. */
            if (--fleet_tick == 0) {
                fleet_tick = fleet_step_delay();
                fleet_step();
            }

            put_score(HUD_SCORE_COL, 2);
            vc_wait_vblank_end();
        }

        /* Nivel superado: sube nivel y acelera la flota (hasta un minimo). */
        if (!quit_flag && !game_over) {
            level++;
            if (fleet_step_frames > (FLEET_STEP_MIN + FLEET_STEP_DEC)) {
                fleet_step_frames = (uint8_t)(fleet_step_frames - FLEET_STEP_DEC);
            } else {
                fleet_step_frames = FLEET_STEP_MIN;
            }
            clear_projectiles();
            put_level(HUD_LEVEL_COL, 2);
            rom_uart_puts("\r\nNivel superado!\r\n");
        }
    }

    /* Anota la puntuacion en la tabla de mejores (si supera a alguna). */
    hiscore_add(score);

    return quit_flag ? 0 : 1;   /* 1 = perdio (game over) */
}

/* Limpia toda la pantalla visible: tilemap a TILE_EMPTY (fondo negro) y
 * atributo por defecto (paleta 0). No toca patrones ni OAM. */
static void clear_screen(void) {
    uint8_t x, y;

    for (y = 0; y < VC_SCREEN_ROWS; y++) {
        for (x = 0; x < VC_SCREEN_COLS; x++) {
            vc_put_cell(x, y, TILE_EMPTY);
            vc_set_cell_attr(x, y, VC_BGPAL_0, 0);
        }
    }
}

/* ===========================================================================
 * PANTALLA DE INICIO / GAME OVER
 * ===========================================================================
 * Muestra un mensaje y ESPERA a que el jugador pulse SPACE (teclado) o FIRE
 * (joystick) para empezar. 'q' sale al monitor.
 *   mode = 0 -> banner de inicio (titulo + instrucciones)
 *   mode = 1 -> pantalla de GAME OVER con la tabla de mejores puntuaciones
 * Devuelve 1 para jugar, 0 para salir.
 * =========================================================================== */
static uint8_t wait_start(uint8_t mode) {
    uint8_t first = 1;

    for (;;) {
        char c;

        vc_wait_vblank();

        if (mode == 1) {
            /* GAME OVER: limpiar la pantalla del juego y pintar la tabla una
             * sola vez (no hace falta redibujar cada frame). */
            if (first) {
                clear_screen();
                vc_clear_oam();   /* quita la explosion/restos de sprites del juego */
                put_str_center(3, "GAME OVER");
                put_str_center(6, "ULTIMA PUNTUACION");
                /* 4 digitos centrados en 40 columnas: col (40-4)/2 = 18. */
                txt_put_u16_4(18, 7, score);
                show_hiscores(10);
                put_str_center(28, "PULSA SPACE o FIRE");
            }
        } else {
            put_str_center(12, "SPACE INVADERS");
            put_str_center(15, "PULSA SPACE o FIRE");
            put_str_center(16, "para empezar");
            clear_str_at(1, 28, 40);
            put_str_center(28, "Q = salir al monitor");
        }

        snd_update();      /* deja terminar los efectos (p.ej. derrota) */
        vc_wait_vblank_end();
        first = 0;

        /* FIRE del joystick o SPACE del teclado -> empezar. */
        if (joy_read() & JOY_A_FIRE) return 1;
        if (rom_uart_rx_ready()) {
            c = rom_uart_getc();
            if (c == ' ' || c == 'r' || c == 'R') return 1;
            if (c == 'q' || c == 'Q') return 0;
        }
    }
}

/* Limpia la pantalla de inicio/game over (los textos y los numeros). */
static void clear_start_screen(void) {
    clear_str_at(10, 13, 11);
    clear_str_at(11, 12, 14);
    clear_str_at(6, 15, 18);
    clear_str_at(9, 16, 12);
    clear_str_at(1, 28, 40);
}

/* ===========================================================================
 * MAIN
 * =========================================================================== */
int main(void) {
    uint8_t play;

    rom_uart_puts("\r\nSPACE INVADERS - Core de Video\r\n");
    rom_uart_puts("SPACE/FIRE = empezar, a/d = mover, q = salir.\r\n");

    setup_video();
    snd_init();
    joy_init();      /* Puerto 1: bits 3-7 como entrada (joystick) */

    hiscore_n = 0;   /* tabla de mejores vacia (solo RAM) */

    /* Banner de inicio: espera SPACE/FIRE antes de la primera partida. */
    if (!wait_start(0)) {
        play = 0;
    } else {
        play = 1;
    }

    while (play) {
        clear_start_screen();

        if (play_game()) {
            /* Perdio: se deja sonar el efecto de derrota un momento (tail) y
             * luego se muestra el GAME OVER esperando tecla. */
            uint8_t t;
            for (t = 0; t < 90; t++) {     /* ~1.5 s de tail */
                vc_wait_vblank();
                snd_update();
                vc_wait_vblank_end();
            }
            play = wait_start(1);     /* 1 = reintentar, 0 = salir */
            /* Si se reintenta, reset_board borra la tabla al iniciar partida. */
        } else {
            play = 0;                 /* salio con 'q' durante la partida */
        }
    }

    /* Salida limpia al monitor: limpiar la pantalla y apagar el video. */
    snd_silence();
    vc_wait_vblank();
    clear_screen();           /* borra el tilemap visible (fondo negro) */
    vc_clear_oam();
    vc_set_scroll_x(0);
    vc_set_scroll_y(0);
    rom_uart_puts("\r\nSaliendo al monitor...\r\n");
    return 0;
}
