/**
 * ============================================================================
 * sound.c - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * Acceso directo a los registros del SID en $D400-$D41F.
 *
 * FORMATO DE REGISTROS (por voz, base = $D400 + 7*voz):
 *   FREQ_LO  = base+0   frecuencia, bits 7:0
 *   FREQ_HI  = base+1   frecuencia, bits 15:8
 *   PW_LO    = base+2   ancho de pulso, bits 7:0
 *   PW_HI    = base+3   ancho de pulso, bits 11:8
 *   CTRL     = base+4   bit0 gate | bit1 sync | bit2 ring | bit3 test |
 *                       bits7:4 waveform (pulse/tri/saw/noise)
 *   AD       = base+5   ataque (nibble alto) / decay (nibble bajo)
 *   SR       = base+6   sustain (nibble alto) / release (nibble bajo)
 *
 *   $D418 MODE_VOL  bits3:0 = volumen master (0-15)
 *
 * MODELO DE SONIDO: cada efecto es "percutivo" (ataque rapido + decay/release),
 * asi que en cada frame comprobamos si la nota sigue viva con un temporizador
 * y, al agotarse, bajamos el GATE (dejando que el release la apague).
 * ============================================================================
 */

#include <stdint.h>
#include "sound.h"

/* Registros del SID (escritura directa). */
#define SID_V1      0xD400
#define SID_V2      0xD407
#define SID_V3      0xD40E
#define SID_MODE_VOL (*(volatile uint8_t *)0xD418)

/* Formas de onda (bits 7:4 de CTRL). */
#define WF_TRI      0x10
#define WF_SAW      0x20
#define WF_PULSE    0x40
#define WF_NOISE    0x80
#define GATE        0x01

/* Duraciones (en frames) de los efectos con temporizador. */
#define SHOOT_FRAMES  10   /* disparo del jugador */
#define MARCH_FRAMES   6   /* latido de la marcha (percutivo) */
#define INVADER_FRAMES 10  /* explosion de invasor */
#define SHIP_FRAMES    30  /* explosion de la nave */

/* ===========================================================================
 * ESCRITURA DE REGISTROS
 * =========================================================================== */

static void sid_wr(uint16_t addr, uint8_t v) {
    *(volatile uint8_t *)addr = v;
}

static void sid_freq(uint16_t base, uint16_t f) {
    sid_wr(base + 0, (uint8_t)(f & 0xFF));
    sid_wr(base + 1, (uint8_t)(f >> 8));
}

static void sid_pw(uint16_t base, uint16_t pw) {
    sid_wr(base + 2, (uint8_t)(pw & 0xFF));
    sid_wr(base + 3, (uint8_t)(pw >> 8));
}

/* ===========================================================================
 * ESTADO (un temporizador por voz; 0 = libre)
 * =========================================================================== */
static uint8_t  t_v1;          /* marcha */
static uint8_t  t_v2;          /* disparo / nave */
static uint8_t  t_v3;          /* explosion invasor / bono */
static uint16_t shoot_freq;    /* frecuencia actual del sweep del disparo */
static uint8_t  v2_is_ship;    /* 1 = la voz 2 suena la nave (no el disparo) */
static uint8_t  march_on;      /* 1 = la voz 1 (marcha) esta sonando */
static uint8_t  ufo_on;        /* 1 = el zumbido del UFO esta sonando (voz 3) */
static uint8_t  ufo_phase;     /* oscilador del tono del UFO (0..) */

/* ===========================================================================
 * API
 * =========================================================================== */

void snd_init(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    sid_wr(SID_V1 + 5, 0); sid_wr(SID_V1 + 6, 0);
    sid_wr(SID_V2 + 5, 0); sid_wr(SID_V2 + 6, 0);
    sid_wr(SID_V3 + 5, 0); sid_wr(SID_V3 + 6, 0);
    SID_MODE_VOL = 0x0F;       /* filtro off + volumen master maximo */
    t_v1 = t_v2 = t_v3 = 0;
    shoot_freq = 0;
    v2_is_ship = 0;
    march_on   = 0;
}

void snd_silence(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    t_v1 = t_v2 = t_v3 = 0;
    march_on = 0;
    ufo_on   = 0;
}

/* --- Zumbido del UFO (voz 3) -------------------------------------------
 * Tono OSCILANTE caracteristico: una onda triangular/pulso cuya frecuencia
 * sube y baja ciclicamente. Suena mientras el UFO cruza.
 * ----------------------------------------------------------------------- */
void snd_ufo_start(void) {
    sid_wr(SID_V3 + 5, 0x08);      /* ataque 0, decay 8 */
    sid_wr(SID_V3 + 6, 0xC0);      /* sustain alto, release 0 */
    sid_pw(SID_V3, 0x0800);
    sid_freq(SID_V3, 0x2000);
    sid_wr(SID_V3 + 4, WF_TRI | GATE);
    ufo_on    = 1;
    ufo_phase = 0;
    t_v3      = 0;                 /* el zumbido NO usa el timer percutivo */
}

void snd_ufo_stop(void) {
    if (ufo_on) {
        sid_wr(SID_V3 + 4, WF_TRI);   /* gate OFF -> release */
        ufo_on = 0;
    }
}

/* Marcha de la flota: latido GRAVE (el "thump" del arcade).
 * speed = 1 (rapido) .. 8 (lento). Mas rapido -> algo mas agudo.
 *
 * IMPORTANTE: si la voz 1 YA esta sonando, NO se reinicia la nota: solo se
 * cambia la frecuencia. Reiniciar la envolvente en cada paso (cuando la flota
 * va rapido, cada 2 frames) cortaba el volumen. Con sustain alto la nota se
 * mantiene y solo "sube" al acelerar. */
void snd_march(uint8_t speed) {
    uint16_t f;

    if (speed < 1) speed = 1;
    if (speed > 8) speed = 8;

    /* 0x0380 (~54 Hz) a 0x0900 (~135 Hz): zona grave. */
    f = (uint16_t)(0x0380 + (uint16_t)(8 - speed) * 0x00B0);

    if (march_on) {
        /* Ya suena: solo cambia el tono (no reinicia la envolvente). */
        sid_freq(SID_V1, f);
        t_v1 = MARCH_FRAMES;
        return;
    }

    /* Primera nota (o reinicio tras el release): arranca con sustain alto. */
    sid_wr(SID_V1 + 5, 0x0A);      /* ataque 0, decay A */
    sid_wr(SID_V1 + 6, 0xD0);      /* sustain D (alto), release 0 */
    sid_pw(SID_V1, 0x0400);        /* pulso ~25% */
    sid_freq(SID_V1, f);
    sid_wr(SID_V1 + 4, WF_PULSE | GATE);  /* arranca */
    march_on = 1;
    t_v1 = MARCH_FRAMES;
}

void snd_shoot(void) {
    /* El disparo usa la voz 2. Puede pisar la explosion de invasor (tambien
     * voz 2): ambos son cortos y es aceptable. */
    sid_wr(SID_V2 + 5, 0x04);      /* ataque 0, decay 4 */
    sid_wr(SID_V2 + 6, 0x00);
    sid_pw(SID_V2, 0x0800);        /* pulso ~50% */
    sid_freq(SID_V2, 0x7000);      /* agudo */
    sid_wr(SID_V2 + 4, WF_PULSE | GATE);

    shoot_freq = 0x7000;
    v2_is_ship  = 0;               /* 0 = voz 2 con sweep (disparo) */
    t_v2        = SHOOT_FRAMES;
}

void snd_invader_die(void) {
    /* Ruido corto en la VOZ 2 (comparte con el disparo; ambos son cortos).
     * El UFO tiene la voz 3 EXCLUSIVA, asi que matar invasores ya NO corta el
     * zumbido del platillo. */
    sid_wr(SID_V2 + 5, 0x0A);      /* ataque 0, decay A */
    sid_wr(SID_V2 + 6, 0x00);
    sid_freq(SID_V2, 0x5000);
    sid_wr(SID_V2 + 4, WF_NOISE | GATE);
    v2_is_ship = 1;                /* reutilizado: 1 = voz 2 sin sweep (ruido) */
    t_v2       = INVADER_FRAMES;
}

void snd_ship_die(void) {
    /* Ruido grave y largo en la VOZ 1 (la marcha). Durante la muerte la flota
     * esta pausada, asi que la voz 1 esta libre; asi NO choca con el disparo
     * (voz 2). Al respawn, snd_march() reinicia la marcha y pisa esta. */
    sid_wr(SID_V1 + 5, 0x08);      /* ataque 0, decay 8 */
    sid_wr(SID_V1 + 6, 0x00);
    sid_freq(SID_V1, 0x1800);
    sid_wr(SID_V1 + 4, WF_NOISE | GATE);
    march_on   = 0;                /* la marcha ya no suena (la piso la nave) */
    t_v1       = SHIP_FRAMES;
    v2_is_ship = 0;                /* ya no usamos la voz 2 para la nave */
}

/* Invasion / derrota: barrido GRAVE y largo en la voz 3 (ruido), que suena
 * mientras los invasores "toman" la pantalla. Se usa cuando la flota llega
 * abajo. Dura todo el mensaje de GAME OVER. */
void snd_game_over(void) {
    sid_wr(SID_V3 + 5, 0x0A);          /* ataque 0, decay A (largo) */
    sid_wr(SID_V3 + 6, 0x00);
    sid_freq(SID_V3, 0x0A00);          /* grave */
    sid_wr(SID_V3 + 4, WF_NOISE | GATE);
    t_v3 = 120;                        /* ~2 s */
}

/* Un "tick" por frame: gestiona las notas percutivas y el sweep del disparo. */
void snd_update(void) {
    /* Voz 1 (marcha): al agotar el timer, corta la nota (release) y marca
     * march_on=0, para que la proxima snd_march() la rearranque limpia. */
    if (t_v1) {
        t_v1--;
        if (t_v1 == 0) {
            sid_wr(SID_V1 + 4, WF_PULSE);   /* gate OFF -> release */
            march_on = 0;
        }
    }

    /* Voz 2: disparo con sweep descendente, o explosion de nave. */
    if (t_v2) {
        t_v2--;
        if (t_v2 == 0) {
            sid_wr(SID_V2 + 4, v2_is_ship ? 0 : WF_PULSE);
            v2_is_ship = 0;
        } else if (!v2_is_ship) {
            /* Sweep: baja la frecuencia para el efecto "laser". */
            if (shoot_freq > 0x0A00) shoot_freq = (uint16_t)(shoot_freq - 0x0700);
            sid_freq(SID_V2, shoot_freq);
        }
    }

    /* Voz 3: el zumbido del UFO (si suena) tiene PRIORIDAD; si no, explosion. */
    if (ufo_on) {
        /* Tono "vibrante": sube/baja la frecuencia via el nibble alto. */
        ufo_phase = (uint8_t)((ufo_phase + 1) & 0x07);
        {
            uint8_t p = ufo_phase;
            if (p > 4) p = (uint8_t)(8 - p);   /* 0..4 -> 0..4 */
            sid_wr(SID_V3 + 1, (uint8_t)(0x18 + p * 3));  /* freq_hi oscila */
        }
    } else if (t_v3) {
        t_v3--;
        if (t_v3 == 0) sid_wr(SID_V3 + 4, 0);
    }
}
