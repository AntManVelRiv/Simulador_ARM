@ Equivalente a "Pr2_Ejemplo1.s" con la llamada a printf DESCOMENTADA (ejercicio 4a).
@ Desde la raíz del proyecto:   armsim-cli ejemplos/cli/ejemplo_printf.s
@ En una Raspberry Pi real:     as -g f.s -o f.o && gcc -g f.o -o f && ./f
        .data
        .balign 4
cadena: .asciz "Hola desde ensamblador: R1 = %d, R2 = %d, suma = %d\n"

        .text
        .global main
main:   push {lr}                 @ guardamos LR: printf lo va a modificar
        mov  r1, #20
        mov  r2, #22
        add  r3, r1, r2
        ldr  r0, =cadena          @ R0 = dirección del formato
        bl   printf               @ printf(cadena, r1, r2, r3)  (argumentos en R1-R3)
        mov  r0, #0               @ valor de retorno de main
        pop  {lr}
        bx   lr
