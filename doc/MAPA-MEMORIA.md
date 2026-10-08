# Mapa de memoria — SPACE INVADERS (6502 / Core de Vídeo)

> Generado a partir de `output/game.map` (linker) y del listado de ensamblador
> (`build/game.lst`, compilado con `-l`). Los tamaños de función son la
> diferencia entre direcciones consecutivas dentro del segmento `CODE` de
> `game.o`. Todos los valores son en **bytes decimales** salvo que lleven `$`.

---

## 1. Mapa de la RAM del sistema

El programa se carga en `$0800` y todo el binario + BSS vive entre `$0800` y
`$3DFF`. El **stack software de cc65** crece hacia abajo desde **`$3DFF`**.

```
Direccion        Uso                                   Tamano
-----------      --------------------------            --------
$0000-$001F      (reservado monitor / ROM API)
$0020-$0065      ZEROPAGE  (driver video + runtime)     70 B
$0800-$0846      STARTUP   (startup.s)                  71 B
$0847-$3A69      CODE      (codigo + libreria)       12835 B
$3A6A-$3CEA      RODATA    (tablas y strings)           641 B
$3CEB-$3CED      DATA      (datos inicializados)          3 B
$3CEE-$3DDB      BSS       (datos sin inicializar)       238 B
$3DDB-$3DFF      ** holgura / stack cc65 **                36 B  <-- MUY JUSTO
$3E00-$3FFF      (area STACK declarada, sin usar en la practica)
```

> ⚠️ **ALERTA:** el stack software arranca en `$3DFF` y crece hacia abajo. El
> BSS termina en `$3DDB`, asi que solo quedan **~36 bytes** de stack antes de
> pisar el BSS. Esto es **peligroso**: si en algun momento se anidan llamadas
> profundas, el stack corrompe variables. Es la causa #1 a sospechar de
> comportamientos raros aleatorios. Ver §6 (como ampliarlo).

---

## 2. Desglose por MODULO (del `.map`)

| Modulo | CODE | RODATA | BSS | ZEROPAGE | Notas |
|--------|-----:|-------:|----:|---------:|-------|
| `game.o`    | 8652 | 641 | 231 |  –  | toda la logica del juego |
| `sound.o`   |  906 |  –  |   7 |  –  | driver SID 6581 |
| `joy.o`     |  115 |  –  |  –  |  –  | lectura joystick Atari |
| `startup.o` |   71 |  –  |  –  |  6  | arranque cc65 -> jmp $8000 |
| `vc.lib(video.o)`   | 1022 | – | – | 20 | nucleo VRAM/OAM/paleta (asm) |
| `vc.lib(collide.o)` |  170 | – | – | 18 | colision AABB (asm) |
| `vc.lib(gfx.o)`     |  952 | – | – | –  | capa alta (sprites, texto, cajas) |
| `none.lib` (56 modulos) | **1018** | – | – | – | runtime C de cc65 |

**Reparto del CODE (~12.9 KB):** `game.o` 8652 B (67%) + `vc.lib` 2144 B
(17%) + `none.lib` 1018 B (8%) + `sound.o`/`joy.o`/`startup.o` ~1092 B (8%).

> **`none.lib` NO es el gigante.** Son **56 modulos pequenos** (5-79 B c/u) que
> cc65 inserta automaticamente: `popa`/`pusha`/`pushax` (paso de argumentos),
> `incsp1..8`/`decsp1..8` (ajuste de stack de llamadas), `aslax*`/`shrax3`
> (AX*n), y aritmetica de 16 bits (`div`/`udiv`/`umod`/`mul8`/`shl`/`shr`).
> La mayoria son **imprescindibles** (pegamento de cc65). Solo se pueden
> recortar evitando ciertas operaciones: ver §7.

---

## 3. Tamaño por FUNCION de `game.o` (ordenado, mayor primero)

CODE de `game.o` = **8652 B**. Funciones mas grandes primero:

| Bytes | Funcion | Comentario |
|------:|---------|------------|
|  828 | `setup_video` | init de video, patrones, paletas, fondo ~~candidata a optimizar~~ |
|  398 | `play_game` | bucle principal de partida |
|  393 | `update_enemy_bullets` | movimiento misiles + colisiones |
|  387 | `update_bullets` | movimiento bala jugador + colisiones |
|  372 | `fleet_step` | paso de la flota (lateral/descenso) |
|  301 | `fleet_draw` | redibuja la rejilla de invasores |
|  284 | `enemy_fire` | elige columna y dispara misil |
|  280 | `shields_draw` | dibuja los 4 escudos |
|  276 | `shield_hit_cell` | dano celda a celda en escudos |
|  273 | `read_input` | joystick + UART |
|  265 | `wait_start` | banners inicio / game over |
|  259 | `setup_bullets` | reset de estado |
|  214 | `main` | arranque y bucle de partidas |
|  208 | `fleet_step_delay` | tabla de velocidad |
|  203 | `hiscore_add` | insercion en tabla top-5 |
|  182 | `show_hiscores` | pinta la tabla de mejores |
|  179 | `fire_bullet` | dispara bala del jugador |
|  167 | `fmt_score` | formatea 4 digitos |
|  166 | `cell_has_invader` | ¿celda ocupada por invasor? |
|  162 | `shields_init` | reinicia escudos |
|  157 | `update_explosions` | anima explosiones |
|  141 | `put_score` | HUD score |
|  140 | `kill_invader` | elimina invasor + puntos |
|  132 | `reset_board` | limpia tablero al iniciar partida |
|  132 | `fleet_erase` | borra rejilla de invasores |
|  112 | `fleet_right_logical_col` | borde derecho real |
|  110 | `clear_projectiles` | apaga balas/misiles/explosiones |
|  106 | `fleet_left_logical_col` | borde izquierdo real |
|  104 | `ship_hit` | nave golpeada |
|  100 | `put_str_at` | texto en (col,row) |
|   96 | `lowest_alive_in_col` | invasor mas bajo por columna |
|   91 | `spawn_explosion` | crea explosion |
|   88 | `fleet_would_hit_edge` | ¿tocara borde? |
|   84 | `bg_star_set` | marca estrella en bitmap |
|   83 | `setup_fleet` | reinicia flota |
|   80 | `clear_screen` | limpia pantalla (game over) |
|   76 | `clear_str_at` | borra texto |
|   75 | `clear_start_screen` | borra banner inicio |
|   73 | `fleet_bottom_logical_row` | fila mas baja con vivos |
|   71 | `bg_star_at` | lee estrella del bitmap |
|   65 | `rng_next` | generador pseudoaleatorio |
|   60 | `put_str_center` | texto centrado |
|   59 | `shield_block_tile` | selecciona tile del escudo |
|   58 | `update_ship_blink` | parpadeo de respawn |
|   55 | `put_lives` | HUD vidas |
|   55 | `put_level` | HUD nivel |
|   54 | `setup_ship` | coloca la nave |
|   53 | `invader_tile` | tile segun fila/anima |
|   53 | `bg_at` | tile de fondo en (col,row) |
|   44 | `put_hiscore` | HUD high score |
|   41 | `fleet_right_cell` | columna fisica derecha |
|   40 | `str_len` | longitud de cadena |
|   37 | `cell_restore` | restaura celda de fondo |
|   33 | `fleet_left_cell` | columna fisica izquierda |
|   33 | `fleet_bottom_cell` | fila fisica inferior |
|   30 | `invader_points` | puntos por fila |
|   18 | `shield_col` | columna de cada escudo |
|   16 | `hiscore_top` | mejor puntuacion actual |
|    8 | (varias etiquetas/literales sin `.proc`) | |

---

## 4. BSS de `game.o` (231 B) — datos sin inicializar

| Simbolo | Bytes | Que es |
|---------|------:|--------|
| `bg_stars[]` | 150 | bitmap de estrellas (40x30 bits) |
| `fleet_alive[5]` | 10 | bitmask de invasores por fila (uint16) |
| `hiscores[5]` | 10 | tabla top-5 (uint16) |
| `b_x[3]`, `b_y[3]`, `b_alive[3]` | 9 | balas del jugador |
| `e_x[2]`, `e_y[2]`, `e_alive[2]` | 6 | misiles enemigos |
| `x_x[2]`, `x_y[2]`, `x_t[2]` | 6 | explosiones |
| `shield_alive[4][3]` | 12 | bitmasks de escudos |
| variables sueltas | ~28 | score, lives, blink, fleet_*, level, rng, etc. |

> El `bg_stars[]` (150 B) es lo mas grande. Antes era `bg_map[40x30]` = **1200 B**;
> se comprimio a bitmap para liberar RAM (ver README).

`sound.o` BSS = 7 B. `joy.o` no tiene BSS.

---

## 5. RODATA de `game.o` (641 B) — tablas y constantes

| Simbolo | Bytes | Que es |
|---------|------:|--------|
| `stars[]` | 70 | tabla de estrellas (35 parejas x,y) |
| `shield_shape` | 12 | forma del escudo (filas × columnas) |
| sprites nave/bala/misil/explosion (8 tablas) | 64 | dibujos 8x8 (2 planos c/u) |
| tiles escudo (5 bloques × 2 planos) | 80 | dibujos de los 5 bloques de escudo |
| tiles invasores (6 patrones + plano0) | 56 | 3 tipos × 2 frames |
| tiles fondo (space/star/floor) | 48 | 3 tiles × 2 planos |
| strings de texto (banner, HUD, etc.) | ~250 | cadenas literales |

---

## 6. Como ampliar la RAM (si se necesita)

El margen actual es de **~36 bytes**, insano. Opciones:

### A. Bajar `__STACKSIZE__` no sirve
`startup.s` fija `c_sp` a `$3DFF` **a mano**, ignorando `__STACKSIZE__`. El stack
real empieza ahi siempre.

### B. Reducir el area STACK declarada (desaprovechada)
En `config/programa.cfg`, el segmento `STACK: $3E00-$3FFF` **no se usa** para el
stack de cc65. Se puede **eliminar** y **extender RAM hasta `$3F7F`**, dejando
128 B de stack real (de `$3F7F` a `$3FFF`). Pero **hay que cambiar tambien
`startup.s`** para que `c_sp` arranque en `$3F7F` en vez de `$3DFF`, y ajustar
`__STACKSTART__`.

### C. Comprimir mas datos
- `stars[]` (70 B RODATA) → generar proceduralmente.
- `setup_video` (828 B) → dividir en funciones mas pequenas / compartir bucles.

---

## 7. Recomendaciones de optimizacion

Por orden de beneficio/riesgo:

1. **Ampliar stack (opcion B).** No es "optimizar tamano" sino **evitar
   corrupcion**. Es lo mas importante ahora.

2. **CODE: dividir funciones gigantes.** `setup_video` (828), `play_game`
   (398), `update_enemy_bullets` (393), `update_bullets` (387) son buenas
   candidatas. cc65 puede reutilizar mejor el codigo si las funciones son
   cortas. Riesgo bajo, ganancia moderada.

3. **Unificar `put_score`/`put_lives`/`put_level`/`put_hiscore`.** Repiten
   patron: un helper `put_num(col,row,valor,digitos)` ahorraria ~150-200 B de
   CODE **y ademas** podria eliminar el modulo `div.o`/`udiv.o`/`umod.o` si se
   usan restas en vez de `/1000`, `/100`, `/10`.

4. **Recortar `none.lib` (limitado).** No se puede quitar entero (es el
   pegamento de cc65). El unico recorte claro: **evitar divisiones de 16 bits**
   (`div.o`+`udiv.o`+`umod.o`, ~100-150 B) reescribiendo `fmt_score`,
   `put_score` y `fleet_step_delay` con restas sucesivas o tablas. Ganancia
   ~150-250 B como maximo. **No es el jackpot** que parece: `none.lib` son solo
   1018 B y su mayoria (popa/pusha/incsp/decsp) es imprescindible.

5. **`bg_stars[]`:** ya optimizado (150 B). No tocar.

6. **Tablas de sprites/tiles (RODATA, ~250 B):** solo comprimibles generandolas
   por codigo; ganancia pequena, complejidad alta.

---

*Fin del mapa. Regenerar los tamanos por funcion con:*

```bash
D:/cc65/bin/cl65.exe -c -t none -O --cpu 6502 -I include -l build/game.lst -o build/game.o game.c
grep -E "\.proc\s+_[A-Za-z][A-Za-z0-9_]*:" build/game.lst
```
