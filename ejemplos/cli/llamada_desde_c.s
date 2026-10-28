@ Rutina ensamblador pensada para ser llamada DESDE C (como 'reproduce' en el ejercicio 5).
@ Prototipo C equivalente:   int suma_array(const int *v, int n);
@ Convenio AAPCS: v -> R0, n -> R1, resultado -> R0.  Sin interfaz:
@   armsim-cli ejemplos/cli/llamada_desde_c.s --call suma_array --arg w:5,10,15,20 --arg 4
        .text
        .global suma_array
suma_array:
        mov  r2, #0               @ acumulador
        mov  r3, #0               @ índice
bucle:  cmp  r3, r1
        bge  fin
        ldr  r12, [r0, r3, lsl #2]
        add  r2, r2, r12
        add  r3, r3, #1
        b    bucle
fin:    mov  r0, r2               @ el resultado de una función C va en R0
        bx   lr
