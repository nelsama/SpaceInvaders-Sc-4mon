/**
 * ============================================================================
 * sound.h - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * El SID esta mapeado en $D400-$D41F, igual que en el Commodore 64.
 *
 * Mapa de registros por voz (offset 0, 7, 14):
 *   +0 FREQ_LO      +1 FREQ_HI      +2 PW_LO    +3 PW_HI
 *   +4 CTRL (gate/sync/ring/test/waveform)
 *   +5 ATTACK/DECAY (+6 SUSTAIN/RELEASE)
 * $D418 = MODE/VOL (bits 3:0 = volumen master)
 *
 * Voces asignadas:
 *   Voz 1: marcha de la flota (latido que acelera).
 *   Voz 2: disparo del jugador y nave destruida.
 *   Voz 3: explosion de invasor (ruido).
 * ============================================================================
 */

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Inicializa el SID: silencia las 3 voces, volumen master al maximo. */
void snd_init(void);

/* Efectos. Todos son no bloqueantes: disparan la voz y suenan solos. */
void snd_march(uint8_t speed);   /* latido de la flota; speed = 1(rapido)..8(lento) */
void snd_shoot(void);            /* disparo del jugador */
void snd_invader_die(void);      /* explosion corta al matar un invasor */
void snd_ship_die(void);         /* explosion larga al perder la nave */
void snd_game_over(void);        /* invasion/derrota (flota llega abajo) */

/* Apaga todas las voces (al salir o cambiar de estado). */
void snd_silence(void);

/* Zumbido del UFO: arranca/para el tono OSCILANTE que suena mientras el UFO
 * cruza la pantalla (voz 3). snd_ufo_tick() se llama 1x/frame para hacer que
 * el tono "vibre" (como el sonido clasico del platillo). */
void snd_ufo_start(void);
void snd_ufo_stop(void);

/* Tick por frame: gestiona las envolventes de los efectos de un disparo. */
void snd_update(void);

#endif /* SOUND_H */
