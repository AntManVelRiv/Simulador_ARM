# Programas en C sin Raspberry: `armsim-cli` + gcc

`armsim-cli` ahora acepta ficheros **`.c`**. No es un compilador de C: usa un `gcc` que genere ARM de 32 bits
(`gcc -S`, solo la fase C → ensamblador) y ejecuta el ensamblador resultante en el simulador, junto con tus `.s`.
Es el equivalente de `gcc main.c rutina.o -o prog && ./prog` de la Raspberry, pero en tu PC.

```bash
armsim-cli programa.c                      # compila con gcc y ejecuta
armsim-cli main.c reproduce.s              # main en C + rutina en ensamblador
armsim-cli programa.c --input "3 4\n"      # entrada simulada para scanf
armsim-cli programa.c -O2 --show-asm       # ver el ensamblador que genera gcc (como gcc -S)
armsim-cli programa.c --emit-asm prog.s    # guardarlo y ejecutar igualmente
armsim-cli programa.c --trace --regs       # depurar instrucción a instrucción
```

## Instalar el compilador (una sola vez)
`armsim-cli --help-gcc` lo recuerda. Ubuntu/Debian/WSL: `sudo apt install gcc-arm-linux-gnueabi`
(vale también `gcc-arm-none-eabi`; en Raspberry Pi OS de 32 bits ya sirve su gcc). Se detecta solo; si tiene otro
nombre: `--cc RUTA` o la variable `ARMSIM_CC`. **No hace falta la libc de ARM**: el simulador aporta sus cabeceras
(`stdio.h stdlib.h string.h ctype.h time.h`) y las funciones.

## Qué funciona
- Código de `-O0` a `-O3`/`-Os`; recursión, punteros, arrays, `struct`, punteros a función, `switch`, `malloc`, enteros de 8/16/32/64 bits, `/` y `%`.
- Biblioteca simulada: `printf fprintf sprintf snprintf scanf puts putchar getchar gets fgets fputs fflush strlen strcpy strncpy strcat strcmp strncmp strchr mem{cpy,move,set,cmp} abs atoi rand srand malloc calloc free toupper tolower is{digit,alpha,alnum,space,upper,lower} time exit`.
- `scanf` se comporta como el de C: solo consume lo que casa con el formato y devuelve `EOF` (-1) al acabar la entrada.
- Validado comparando con la ejecución nativa en x86 (`tests/c_vs_nativo.sh`).

## Límites (conviene saberlos)
- **Sin coma flotante** (`float`/`double`, `math.h`) ni `%f`, `%lld`, ficheros, `qsort`… Da un error de «etiqueta desconocida».
- Los nombres de librería son simulados: `rand()` no da la misma secuencia que glibc, `time()` es ficticio y `malloc` no reutiliza memoria.
- Es un simulador ARMv4: no ejecuta Thumb ni instrucciones de ARMv6/v7 (por eso se compila con `-march=armv4t -marm`).
- Los `.c` y `.s` se «enlazan» por concatenación: los nombres globales no pueden repetirse.
