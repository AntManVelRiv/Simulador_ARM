/* Programa C normal, sin Raspberry:  armsim-cli ejemplos/cli/c_hola.c   (ver docs/C_CON_GCC.md) */
#include <stdio.h>

int main(void) {
    int a = 20, b = 22;
    printf("Hola desde C: %d + %d = %d\n", a, b, a + b);
    return 0;
}
