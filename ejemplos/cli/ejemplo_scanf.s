@ Lee dos enteros con scanf, los suma y los imprime con printf (estilo ejercicio 4b).
@   armsim-cli ejemplos/cli/ejemplo_scanf.s --input "12 30\n"      (entrada simulada)
@   armsim-cli ejemplos/cli/ejemplo_scanf.s                         (te pide los datos por teclado)
        .data
        .balign 4
pide:   .asciz "Introduce dos enteros: "
fmtin:  .asciz "%d %d"
fmtout: .asciz "%d + %d = %d\n"
a:      .word 0
b:      .word 0

        .text
        .global main
main:   push {r4, lr}
        ldr  r0, =pide
        bl   printf
        ldr  r0, =fmtin
        ldr  r1, =a               @ scanf recibe PUNTEROS a donde guardar cada valor
        ldr  r2, =b
        bl   scanf
        ldr  r1, =a
        ldr  r1, [r1]
        ldr  r2, =b
        ldr  r2, [r2]
        add  r3, r1, r2
        ldr  r0, =fmtout
        bl   printf
        mov  r0, #0
        pop  {r4, lr}
        bx   lr
