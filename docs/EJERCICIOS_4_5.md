# Ejercicios 4 y 5 sin Raspberry Pi

El simulador ya no necesita ventana ni Raspberry para esto: `armsim-cli` (o `armsim --run ...`) hace el papel de
`as` + `gcc` + `./programa`. No usa SDL ni OpenGL, así que se compila en cualquier PC:

```bash
cmake -B build -DARMSIM_BUILD_GUI=OFF && cmake --build build --target armsim-cli
./build/armsim-cli --help
```

> **Importante:** no tengo `Pr2_Ejemplo1.s`, `Pr2_Ejemplo2.s` ni `Pr2_Ejercicio5.c` de tu práctica. Los ficheros de
> `ejemplos/cli/` son *equivalentes escritos por mí* para probar el procedimiento. Con tus ficheros reales el comando
> es idéntico: solo cambia el nombre.

## Ejercicio 4a — `printf` desde ensamblador

En la Raspberry:
```bash
as -g Pr2_Ejemplo1.s -o Pr2_Ejemplo1.o
gcc -g Pr2_Ejemplo1.o -o Pr2_Ejemplo1
./Pr2_Ejemplo1
```
En el simulador (un solo paso, sin Raspberry):
```bash
armsim-cli Pr2_Ejemplo1.s            # con la llamada a printf y el .asciz descomentados
armsim-cli Pr2_Ejemplo1.s --trace    # además, cada instrucción ejecutada
armsim-cli Pr2_Ejemplo1.s --regs     # y los registros finales
```
Ejemplo incluido: `armsim-cli ejemplos/cli/ejemplo_printf.s` → `Hola desde ensamblador: R1 = 20, R2 = 22, suma = 42`.

**Qué explicar sobre `printf`** (para el apartado «describa lo que observa»):

- `printf` no es una instrucción del procesador, es una función de la librería C (`stdio.h`). Por eso se enlaza con
  `gcc` y no solo con `as`/`ld`: `gcc` añade el código de arranque (que llama a `main`) y la librería C.
- Se invoca con `BL printf`. Convenio de llamada ARM (AAPCS): **R0** = dirección de la cadena de formato (terminada
  en 0, la que declara `.asciz`), **R1–R3** = los argumentos que sustituyen a `%d`, `%s`, `%c`... (del cuarto en
  adelante van en la pila). El valor devuelto (nº de caracteres escritos) queda en **R0**.
- `BL` modifica **LR**, así que `main` debe guardarlo (`push {lr}`) y recuperarlo (`pop {lr}`) antes de `bx lr`;
  si no, `main` nunca vuelve al código de arranque.
- `printf` escribe en la salida estándar con búfer: aparece en pantalla al final de la línea (`\n`).

## Ejercicio 4b — Ejemplo 2 con otras entradas

```bash
armsim-cli Pr2_Ejemplo2.s                         # pide los datos por teclado, como ./Pr2_Ejemplo2
armsim-cli Pr2_Ejemplo2.s --input "12 30\n"       # entrada simulada: reproducible y fácil de documentar
```
Salidas reales de `ejemplos/cli/ejemplo_scanf.s` con distintas entradas (haz las capturas con tu Ejemplo 2):

| Entrada | Salida |
|---|---|
| `12 30` | `12 + 30 = 42` |
| `-5 3` | `-5 + 3 = -2` |
| `100 -250` | `100 + -250 = -150` |
| `2147483647 1` | `2147483647 + 1 = -2147483648` (desbordamiento de 32 bits con signo) |

`scanf` recibe **punteros** (R1, R2...) a las variables donde guardar lo leído; por eso el código usa `ldr r1,=a`
antes de llamarlo y vuelve a cargar el valor con `ldr r1,[r1]`.

## Ejercicio 5 — `main` en C que llama a `reproduce` (ensamblador)

El código C está en `ejemplos/cli/ejercicio5_demo.c`: declara `extern int reproduce(int cancion);`, llama con `3`
e imprime el valor devuelto. El compilador genera `MOV R0,#3 / BL reproduce / MOV Rx,R0`: el argumento viaja en
**R0** y el resultado vuelve en **R0**. Eso es exactamente lo que reproduce `--call`:

```bash
armsim-cli reproduce.s --call reproduce --arg 3
# Valor devuelto (R0) = ...
```
Argumentos admitidos: `42`, `0x1F`, `'A'` (enteros), `s:texto` (cadena), `w:1,2,3` (array de enteros de 32 bits) y
`b:1,2,3` (array de bytes). Las cadenas/arrays se copian a memoria y se pasa su puntero, como haría C.

Con la rutina de demostración: `armsim-cli ejemplos/cli/reproduce_demo.s --call reproduce --arg 3` → `300`.

**Con un `main` real en C:** si tienes un gcc cruzado ARM, `armsim-cli Pr2_Ejercicio5.c reproduce.s` compila el C con gcc y lo ejecuta
junto al ensamblador (ver [C_CON_GCC.md](C_CON_GCC.md)). Sin gcc, `--call` sustituye al `main` de C ejecutando solo la parte ensamblador.
**Para adaptar el apartado exacto de tu práctica necesito `Pr2_Ejercicio5.c` y el `.s` de `reproduce`** (el prototipo
real: ¿recibe el número de canción, un puntero a un array, una longitud...?).

## Qué soporta el ensamblador para estos ficheros
`.global/.section/.align/.balign/.hword/.comm/.type/.size/.func...` (los últimos se ignoran), ejecución desde `main`
aunque haya subrutinas antes, `BL printf/scanf/puts/gets/strlen/putchar/getchar/atoi/exit`, y `printf` con anchura y
relleno (`%5d`, `%03d`, `%-8s`, `%x`, `%X`, `%o`, `%c`, `%p`).
