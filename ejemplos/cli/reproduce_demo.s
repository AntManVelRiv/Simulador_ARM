@ DEMOSTRACIÓN (no es la rutina de tu práctica): una "reproduce" mínima para probar el circuito C <-> ensamblador.
@ Prototipo C:   int reproduce(int cancion);   // devuelve el 1.er valor entero del sonido de esa canción
@ Cada canción es una lista de enteros terminada en -1. Sin Raspberry:
@   armsim-cli ejemplos/cli/reproduce_demo.s --call reproduce --arg 3
        .data
        .balign 4
cancion1: .word 100, 110, 120, -1
cancion2: .word 200, 210, 220, -1
cancion3: .word 300, 310, 320, 330, -1
tabla:    .word cancion1, cancion2, cancion3

        .text
        .global reproduce
reproduce:
        sub  r0, r0, #1           @ canción 1..3 -> índice 0..2
        ldr  r1, =tabla
        ldr  r1, [r1, r0, lsl #2] @ r1 = dirección de la canción elegida
        ldr  r0, [r1]             @ r0 = primer valor (valor de retorno)
        bx   lr
