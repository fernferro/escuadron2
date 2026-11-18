/* Trucos (passcodes) del juego, para prenderlos desde las opciones sin escribir el codigo.
 *
 * Como funcionan en el juego (fn_7FD6D338, pantalla Options > Passcodes): el codigo de 8 letras
 * se pasa 1000 veces por SHA-1 (fn_7FD7F244) y se busca en una tabla de 71 resultados
 * (0x7FDE2350). Cada codigo valido prende un bit en una de dos palabras de la partida
 * (estructura 0x7FE1C4A0): +0x654 (0x7FE1CAF4) y +0x658 (0x7FE1CAF8). Los de dos partes
 * (primero una clave al azar y despues una palabra) terminan en el mismo lugar.
 * Vidas infinitas y blanco y negro no se guardan en la tarjeta; el resto si, cuando el juego guarda.
 *
 * Aca: si un truco esta prendido en ajustes.ini, el runtime mantiene su bit prendido en cada
 * cuadro (video.c). Apagado no se toca nada (queda como este en la partida). */
#ifndef TRUCOS_H
#define TRUCOS_H

#define TRUCOS_A 0x7FE1CAF4u   /* +0x654 */
#define TRUCOS_B 0x7FE1CAF8u   /* +0x658 */

typedef struct {
    const char* clave;     /* en ajustes.ini: truco_<clave> = 1 */
    const char* nombre;
    const char* desc;
    unsigned palabra;      /* TRUCOS_A o TRUCOS_B */
    unsigned mascara;
    const char* codigo;    /* el passcode original (para mostrarlo) */
} Truco;

static const Truco k_trucos[] = {
    { "vidas", "INFINITE LIVES", "You never run out of lives. Not saved to the memory card.", TRUCOS_A, 0x8, "JPVI?IJC + RSBFNRL" },
    { "mejoras", "ALL UPGRADES", "Every ship technology upgrade (advanced missiles, shields, targeting computer...).", TRUCOS_A, 0x2, "AYZB!RCL + WRKFORIT" },
    { "as", "ACE MODE", "The hardest difficulty in the game: enemies are much tougher.", TRUCOS_B, 0x40, "U!?!VWZC + GIVEITUP" },
    { "blanco_negro", "BLACK AND WHITE", "The whole game in black and white, like an old movie. Not saved to the memory card.", TRUCOS_A, 0x8000, "LIONHEAD" },
    { "clasicas", "ORIGINAL MISSIONS", "The missions of the first game in the series (arcade version of the first 10).", TRUCOS_A, 0x1, "!??QWTTJ + CLASSIC" },
    { "huida", "BONUS MISSION: THE ESCAPE", "Unlocks the bonus mission of the escape aboard the freighter.", TRUCOS_A, 0x100000, "PYST?OOO + DUCKSHOT" },
    { "asteroides", "BONUS MISSION: ASTEROID FIELD", "Unlocks the asteroid field bonus mission.", TRUCOS_A, 0x10000, "TVLYBBXL + NOWAR!!!" },
    { "otro_bando", "BONUS MISSION: THE OTHER SIDE", "Unlocks the bonus mission played on the enemy side.", TRUCOS_A, 0x40000, "AZTBOHII + OUTCAST!" },
    { "revancha", "BONUS MISSION: THE REVENGE", "Unlocks the other bonus mission on the enemy side.", TRUCOS_A, 0x80000, "OGGRWPDG + EEKEEK!" },
    { "resistencia", "BONUS MISSION: ENDURANCE", "Unlocks the endurance bonus mission (endless waves).", TRUCOS_A, 0x20000, "?WCYBRTC + ??MBC???" },
    { "carguero", "SHIP: THE FREIGHTER", "The heroes' freighter in the hangar.", TRUCOS_B, 0x4000, "MVPQIU?A + OH!BUDDY" },
    { "caza_plateado", "SHIP: SILVER FIGHTER", "The silver fighter from the prequels in the hangar.", TRUCOS_B, 0x400, "CDYXF!?Q + ASEPONE!" },
    { "caza_enemigo", "SHIP: ENEMY FIGHTER", "The enemy side's basic fighter in the hangar.", TRUCOS_B, 0x20000, "ZT?!RGBA + DISPSBLE" },
    { "caza_villano", "SHIP: VILLAIN'S FIGHTER", "The main villain's ship in the hangar.", TRUCOS_B, 0x10000, "NYM!UUOK + BLKHLMT!" },
    { "cazarrecompensas", "SHIP: BOUNTY HUNTER", "The bounty hunter's ship in the hangar.", TRUCOS_B, 0x8000, "PZ?APBSY + IRONSHIP" },
    { "cazarrecompensas2", "SHIP: BOUNTY HUNTER (VERSION 2)", "The other version of that ship (also needs the previous one).", TRUCOS_A, 0x200000, "VV?GXRYP + CNOOQ!ZR + JFETTSHP" },
    { "lanzadera", "SHIP: ENEMY SHUTTLE", "The enemy side's shuttle (only in that side's two bonus missions).", TRUCOS_B, 0x40000, "AJHH!?JY + BUSTOUR" },
    { "auto", "SHIP: THE SECRET CAR", "A flying car from the 80s. Yes, really.", TRUCOS_B, 0x200000, "!ZUVIEL! + !BENZIN!" },
    { "galería", "EXTRAS: ART GALLERY", "The art gallery in Special Features.", TRUCOS_B, 0x10, "EXHIBIT!" },
    { "sonido", "EXTRAS: SOUND TEST", "The sound test (all the music) in Special Features.", TRUCOS_B, 0x20, "COMPOSER" },
    { "documental", "EXTRAS: MAKING OF", "The \"Making of\" documentary in Special Features.", TRUCOS_B, 0x2, "?INSIDER" },
    { "comentarios", "EXTRAS: DIRECTOR'S COMMENTARY", "The creators' commentary in Special Features.", TRUCOS_B, 0x8, "BLAHBLAH" },
    { "créditos", "EXTRAS: CREDITS", "The credits in Special Features.", TRUCOS_B, 0x1, "THATSME!" },
};
#define NTRUCOS ((int)(sizeof k_trucos / sizeof k_trucos[0]))

#endif
