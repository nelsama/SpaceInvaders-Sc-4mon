# SPACE INVADERS — Core de Vídeo (vc) para 6502 / Tang Nano 9K

Juego tipo *Space Invaders* (arcade 1978) para el computador **6502** con el
**Core de Vídeo** (`videocore-6502-cc65`) y el **monitor 6502**. Programado
principalmente en C con **cc65**, con las rutinas críticas en **ensamblador**
(`text.s`, `math.s`).

> **Origen:** este proyecto parte de `videocore-6502-cc65/examples/demo/`, pero es
> un proyecto **autónomo** con su propia configuración y copia de la biblioteca
> (`lib/vc.lib`, `config/programa.cfg`, `include/video.h`).

---

## Compilar y cargar

```bash
make CC65_HOME=D:/cc65
```

Genera `output/game.bin`. Cárgalo en el monitor:

```
LOAD GAME 0800
R 0800
```

> **Nota sobre `CC65_HOME`:** en este entorno cc65 es un ejecutable de Windows
> (`D:/cc65`). Compila desde **Git Bash / cmd** (`CC65_HOME=D:/cc65`), no desde
> WSL (WSL no puede lanzar los `.exe`).

---

## Mapa de memoria (hardware real)

Del fuente del monitor (`monitor_6502_TN-9k_16k`):

| Rango | Contenido |
|-------|-----------|
| `$0000–$3FFF` | **RAM (16 KB)** |
| `$4000–$7FFF` | VRAM |
| `$8000–$BFFF` | ROM (monitor) |
| `$C000–$C0xx` | I/O (GPIO, UART, SPI, I2C, timer) |
| `$D400–$D41F` | SID |
| `$D800–$D87F` | Vídeo |

**Dentro de la RAM:**
- `$0000–$00FF`: Zero Page (el monitor usa `$0002–$00FF`; el juego `$20–$7x`).
- `$0100–$01FF`: pila de hardware del 6502.
- **`$0800–$3F9F`**: programa (código + datos + BSS).
- **`$3FA0–$3FFD`**: pila software de cc65.
- ⚠️ **`$3FFE–$3FFF`**: flag de **AUTOBOOT del monitor**. No usar.

> El programa se carga en **`$0800`**. Cargarlo en `$0200` **NO funciona** en este
> hardware (algo del monitor usa esa zona), aunque la RAM llegue a `$3FFF`.

---

## Sonido (SID 6581, compatible C64)

El juego usa el **SID** en `$D400-$D41F` (idéntico al C64). Driver en
`sound.c` / `include/sound.h`. **Reparto de voces** (importante, están muy
justas):

| Voz | Uso |
|-----|-----|
| **1** | marcha de la flota (latido que sube de tono al acelerar) **+** explosión de la nave (nunca a la vez: la flota se pausa al morir) |
| **2** | disparo del jugador (sweep descendente) **+** explosión de invasor (ruido corto) |
| **3** | **zumbido del UFO (exclusivo)** |

> ⚠️ **Reparto delicado:** disparo y explosión de invasor comparten voz 2 (ambos
> cortos). La explosión de la nave va en la voz 1 porque al morir la marcha está
> pausada. El **UFO tiene la voz 3 para él solo** — antes compartía con la
> explosión de invasor y el zumbido se cortaba al matarlos.

Volumen master a máximo (`$D418 = 0x0F`, filtro desactivado). `snd_update()` se
llama una vez por frame para gestionar envolventes y el zumbido del UFO.

---

## Diseño del juego (decisiones fijadas)

### Invasores = **tiles** del tilemap, movimiento por **pasos de celda**

El hardware limita la OAM a **32 sprites** y **8 sprites por línea**. La flota
completa (5×11 = 55 invasores) **no cabe como sprites**. Por eso:

- **Invasores → tiles** del tilemap.
- **Nave, balas, misiles, explosiones, UFO → sprites** (OAM).

Movimiento de la flota (técnica canónica):

```
al dar el paso (cada N frames):
  1. borrar las celdas anteriores de la flota
  2. escribir las celdas nuevas (col±1)   -> "march"
  3. si toca borde: bajar 1 fila (row+1) y cambiar dirección
```

Se mantiene una **máscara de bits** de columnas vivas. La velocidad (N) disminuye
con el nivel y con los invasores vivos. La flota usa los **bordes reales**
(invasores vivos), así que si limpias una columna entera, la flota sigue hasta el
borde de la pantalla.

### UFO (nave nodriza)

Cruza la parte alta de la pantalla, encima de la flota.

- **2 sprites** contiguos (mitad izq + der con **FLIP_X**, un solo patrón).
- Aparece **al azar**, y **más seguido cuando quedan menos invasores** (como el
  arcade). El retardo se liga a `fleet_count`.
- Colisión con la bala del jugador → **+150 puntos** + explosión.
- **Zumbido** propio (voz 3) mientras cruza.

### Colisiones (software)

- **Bala ↔ invasor:** la bala es un sprite; convertir su X/Y a celda
  (`col = x/8`, `row = y/8`) y consultar la máscara de la flota.
- **Bala ↔ nave / bala ↔ UFO:** cajas AABB (`vc_box_overlap`). La caja del misil
  es **estrecha** (la barra visible mide ~2 px), para no dar falsos positivos.

### Entrada: joystick Atari + UART

Se puede jugar con **joystick Atari (DB9)** y/o con el teclado del terminal.

**Joystick** (`joy.c` / `include/joy.h`): **Puerto GPIO 1**, bits **3-7** (los
bits 0-2 los ocupa el TM1638, que **no se toca**).

| Señal | Pin | Bit |
|-------|-----|-----|
| right | 40 | 3 |
| left  | 34 | 4 |
| down  | 33 | 5 |
| up    | 30 | 6 |
| fire  | 29 | 7 |

Registros: `$C000` (datos), `$C002` (config: 1 = entrada). `joy_init()` hace
**read-modify-write** de `$C002` para no pisar los bits del TM1638. Ver
`doc/JOYSTICK-INTEGRACION.md` para el mini-manual.

> **Polaridad:** el joystick Atari es activo en bajo (0 = pulsado).

**Teclado (UART):** en paralelo al joystick.

| Tecla | Acción |
|-------|--------|
| `a` / `j` | mover a la izquierda |
| `d` / `l` | mover a la derecha |
| `space`   | disparar |
| `r`       | (en GAME OVER) jugar de nuevo |
| `q`       | salir al monitor |

### Reglas

- **3 vidas.** Al recibir un impacto, la nave reaparece **donde murió** con un
  parpadeo de invulnerabilidad (~90 frames).
- **Puntuación:** 30 / 20 / 10 según el tipo de invasor (arriba vale más).
- **Escudos:** 4 búnkeres destructibles. Tanto tus balas como las enemigas los
  desgastan celda a celda.
- **Niveles:** al limpiar la flota sube el nivel y la flota marcha más rápido
  (los escudos se reparan al empezar cada nivel).
- **Derrota:** sin vidas, o si la flota alcanza la altura de la nave.
- **Fin de partida:** al perder se limpia la pantalla y se muestra `GAME OVER`,
  la última puntuación y la **tabla de las 5 mejores**; espera `SPACE`/`FIRE`
  (jugar de nuevo) o `Q` (salir al monitor).
- **High scores:** tabla de las 5 mejores puntuaciones, **solo en RAM** (se pierde
  al apagar). La mejor se muestra siempre en el HUD (`HI`), y la tabla completa
  al perder. No se guarda en SD.

---

## Presupuesto de recursos

| Recurso | Límite | Uso |
|---|---|---|
| Sprites (OAM) | 32 | nave 1 + balas 3 + misiles 2 + explosiones 2 + UFO 2 = **10** |
| Sprites por línea | **8** | máx. ~6 → OK (ver nota) |
| Patrones de sprite | 64 | nave, bala, misil, explosión, UFO = **5** |
| Patrones de fondo | 256 (fuente `$20`-`$7F`) | invasores 3×2 + escudos 5 + fondo 3 ≈ 14 |
| RAM | `$0800-$3F9F` | muy justo — ver nota |

> **Límite de 8 sprites por línea (⚠️ importante):** el core dibuja como máximo
> **8 sprites por línea**; si se supera, **descarta alguno** (flag `OVERFLOW`).
> Los pools de misiles y explosiones se limitan a **2 cada uno**: peor caso
> 2 misiles + 2 explosiones + 1 bala + 1 nave = **6**. Con más pools, los misiles
> desaparecían (hacían daño pero no se veían).

> **RAM justa:** el binario está cerca del límite. Para liberar RAM se usó:
> - **Fondo comprimido:** bitmap de estrellas (`bg_stars[]`, ~150 B) en vez de
>   `bg_map` (1200 B).
> - **Texto/HUD y divisiones en ASM** (`text.s`, `math.s`): eliminó
>   `div.o`/`udiv.o`/`umod.o` del runtime (`none.lib`).
> - Si hace falta más RAM, la siguiente candidata es `update_ufo` (~1.7 KB en C).

---

## Módulos

| Archivo | Lenguaje | Qué hace |
|---|---|---|
| `game.c` | C | lógica del juego |
| `sound.c` | C | driver SID 6581 |
| `joy.c` | C | lectura joystick Atari |
| `text.s` | ASM | texto y HUD (escritura VRAM directa, números por restas) |
| `math.s` | ASM | `u8_mod`, `u16_div_u8` (sin división de hardware) |
| `startup.s` | ASM | arranque cc65 |

## Presupuesto OAM (slots usados)

```
0     nave
1-3   balas del jugador
4-5   misiles enemigos
6-7   UFO (izq + der)
8-9   explosiones
```

---

## Estructura

```
SpaceInvaders/
├── makefile            build autónomo
├── game.c              lógica del juego
├── sound.c             driver SID
├── joy.c               joystick Atari
├── text.s              texto/HUD en ASM
├── math.s              aritmética sin división (ASM)
├── startup.s           arranque cc65
├── include/
│   ├── video.h         API del Core de Vídeo (copia de la lib)
│   ├── romapi.h        ROM API del monitor
│   ├── sound.h         API del driver SID
│   └── joy.h           API del joystick
├── config/
│   └── programa.cfg    config del linker (juego)
├── doc/
│   ├── VIDEO-LIB.md            manual de la biblioteca
│   ├── MAPA-MEMORIA.md         mapa de memoria detallado
│   ├── JOYSTICK-INTEGRACION.md mini-manual del joystick
│   └── text.s.md               diseño del módulo de texto ASM
├── lib/
│   └── vc.lib          copia de la biblioteca
├── build/              objetos
└── output/
    ├── game.bin        binario final
    └── game.map        mapa de memoria
```

### Actualizar la biblioteca

```bash
cp ../videocore-6502-cc65/output/vc.lib   lib/vc.lib
cp ../videocore-6502-cc65/src/video.h     include/video.h
cp ../videocore-6502-cc65/config/programa.cfg config/programa.cfg
```

> **Versión de la API:** el juego está alineado con la biblioteca `vc`
> correspondiente al **Manual del Core v2.7** (constantes neutras
> `VC_BGPAL_0..3`, `VC_SPPAL_0..3`, `VC_TINTA(paleta)`).
