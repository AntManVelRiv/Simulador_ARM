// model/Examples.cpp
#include "Examples.h"

const std::vector<ExampleDef>& examples() {
    static const std::vector<ExampleDef> E = {
 {"a = b + c - d", R"ASM(@ a = b + c - d    (a en R1, b en R2, c en R3, d en R4)
        MOV  R2, #10
        MOV  R3, #7
        MOV  R4, #3
        ADD  R1, R2, R3     @ R1 = b + c
        SUB  R1, R1, R4     @ R1 = (b + c) - d
        SWI  0x11
)ASM"},
 {"Lógicas y desplazamientos", R"ASM(        LDR  R7, =0x0FAFF0C3
        LDR  R3, =0xB4D6B268
        MOV  R4, #9
        AND  R1, R7, R3
        ORR  R2, R7, R3
        EOR  R5, R7, R3
        BIC  R6, R7, R3
        MVN  R8, R7
        LSL  R9, R7, #10
        LSR  R10, R3, #7
        ASR  R11, R3, #7
        LSL  R12, R7, R4
        SWI  0x11
)ASM"},
 {"Flags y ejecución condicional", R"ASM(        MOV    R3, #9
        MOV    R4, #9
        LDR    R7, =0x0FAFF0C3
        CMP    R3, R4          @ Z = 1
        ADDEQ  R1, R4, R3      @ se ejecuta
        CMP    R7, R3
        ADDEQ  R2, R4, R3      @ NO se ejecuta
        SUBS   R5, R3, R4
        SWI    0x11
)ASM"},
 {"Bucle for (suma 0..9)", R"ASM(        MOV  R0, #0         @ sum
        MOV  R1, #0         @ i
FOR:    CMP  R1, #10
        BGE  FIN
        ADD  R0, R0, R1
        ADD  R1, R1, #1
        B    FOR
FIN:    SWI  0x11
)ASM"},
 {"Array ×8 con post-indexación", R"ASM(        .data
array:  .word 1, 2, 3, 4, 5, 6
        .text
        LDR  R0, =array
        ADD  R1, R0, #24
LOOP:   CMP  R0, R1
        BGE  FIN
        LDR  R2, [R0]
        MOV  R2, R2, LSL #3
        STR  R2, [R0], #4
        B    LOOP
FIN:    SWI  0x11
)ASM"},
 {"Bytes: LDRB / LDRSB", R"ASM(        .data
T:      .byte 1, 2, 3, 4, 5, 6, 7, 8
out:    .space 8
neg:    .byte 0xF0
        .text
        LDR   R0, =out
        LDR   R1, =T
        MOV   R2, #0
FOR:    CMP   R2, #8
        BGE   FIN
        LDRB  R3, [R1, R2]
        ADD   R3, R3, #20
        STRB  R3, [R0, R2]
        ADD   R2, R2, #1
        B     FOR
FIN:    LDR   R4, =neg
        LDRB  R5, [R4]        @ 0x000000F0
        LDRSB R6, [R4]        @ 0xFFFFFFF0
        SWI   0x11
)ASM"},
 {"Little-endian", R"ASM(        .data
dato:   .space 4
        .text
        LDR   R0, =dato
        LDR   R1, =0x0A0B0C0D
        STR   R1, [R0]
        LDRB  R2, [R0]
        LDRB  R3, [R0, #1]
        LDRB  R4, [R0, #2]
        LDRB  R5, [R0, #3]
        SWI   0x11
)ASM"},
 {"Función con BL y pila", R"ASM(main:   MOV  R0, #2
        MOV  R1, #3
        MOV  R2, #4
        MOV  R3, #5
        BL   DIFFOFSUMS
        MOV  R5, R0          @ (2+3)-(4+5) = -4
        SWI  0x11
DIFFOFSUMS: PUSH {R4, R8, R9}
        ADD  R8, R0, R1
        ADD  R9, R2, R3
        SUB  R4, R8, R9
        MOV  R0, R4
        POP  {R4, R8, R9}
        MOV  PC, LR
)ASM"},
 {"Factorial recursivo", R"ASM(main:   MOV  R0, #5
        BL   FACTORIAL
        MOV  R4, R0          @ 5! = 120
        SWI  0x11
FACTORIAL: PUSH {R0, LR}
        CMP  R0, #1
        BGT  ELSE
        MOV  R0, #1
        ADD  SP, SP, #8
        MOV  PC, LR
ELSE:   SUB  R0, R0, #1
        BL   FACTORIAL
        POP  {R1, LR}
        MUL  R0, R1, R0
        MOV  PC, LR
)ASM"},
 {"Memoria en direcciones propias (.org)", R"ASM(        .data
        .org 0x3000
tabla:  .word 10, 20, 30, 40
        .text
        LDR  R0, =tabla       @ 0x3000
        LDR  R1, [R0, #4]
        LDR  R2, =0x00010000  @ otra zona de memoria cualquiera
        MOV  R3, #99
        STR  R3, [R2]
        LDR  R4, [R2]
        SWI  0x11
)ASM"},
 {"Consola: leer y sumar dos enteros", R"ASM(@ Lee dos enteros de la consola y escribe su suma.
        LDR   R0, =msg1
        SWI   0x24            @ escribe cadena
        SWI   0x30            @ lee un entero -> R0
        MOV   R4, R0          @ guarda el primer numero

        LDR   R0, =msg2
        SWI   0x24
        SWI   0x30
        MOV   R5, R0          @ segundo numero

        ADD   R6, R4, R5
        LDR   R0, =msg3
        SWI   0x24
        MOV   R0, R6
        SWI   0x21            @ escribe entero con signo
        SWI   0x25            @ salto de linea
        SWI   0x11
        .data
msg1:   .asciz "Primer numero: "
msg2:   .asciz "Segundo numero: "
msg3:   .asciz "Suma = "
)ASM"},
 {"Consola: leer el nombre y saludar", R"ASM(        LDR   R0, =prompt
        SWI   0x24
        LDR   R1, =buf        @ direccion del buffer
        MOV   R2, #32         @ tamano maximo
        SWI   0x32            @ lee una linea -> [R1], longitud en R0
        LDR   R0, =hola
        SWI   0x24
        LDR   R0, =buf
        SWI   0x24            @ escribe el nombre leido
        SWI   0x25
        SWI   0x11
        .data
prompt: .asciz "Como te llamas? "
hola:   .asciz "Hola, "
buf:    .space 32
)ASM"},
 {"Multiplicación de 64 bits", R"ASM(        LDR    R0, =0x80000000
        MOV    R1, #2
        MUL    R4, R0, R1         @ solo 32 bits bajos
        SMULL  R2, R3, R0, R1     @ R3:R2 = resultado completo
        SWI    0x11
)ASM"},
    };
    return E;
}
