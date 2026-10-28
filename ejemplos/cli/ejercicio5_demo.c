/* Ejercicio 5 (plantilla): main en C que llama a la rutina en ensamblador 'reproduce'.
 *
 * ADAPTA el prototipo a tu Pr2_Ejercicio5.c / a tu rutina del ejercicio 2.
 * Aquí se supone:  int reproduce(int cancion);  -> devuelve el primer valor entero del sonido.
 *
 * En la Raspberry:  as -g reproduce.s -o reproduce.o && gcc -g Pr2_Ejercicio5.c reproduce.o -o ej5 && ./ej5
 * Sin Raspberry:    armsim-cli reproduce.s --call reproduce --arg 3      (mismo R0 que imprimiría este main)
 */
#include <stdio.h>

extern int reproduce(int cancion);      /* implementada en ensamblador: argumento en R0, resultado en R0 */

int main(void) {
    int cancion = 3;                    /* canción número 3 */
    int primero = reproduce(cancion);   /* el compilador genera: MOV R0,#3 ; BL reproduce ; MOV Rx,R0 */
    printf("Cancion %d: primer valor del sonido = %d\n", cancion, primero);
    return 0;
}
