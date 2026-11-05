#!/bin/sh
# Compara la salida de programas C ejecutados en el simulador (gcc ARM -> simulador) con la del mismo programa
# compilado de forma nativa con la libc real. Uso:  tests/c_vs_nativo.sh build/armsim-cli programa1.c [programa2.c ...]
# Los programas que leen datos deben tener un fichero con el mismo nombre y extension .in
# (cuidado: el orden de evaluacion de argumentos de funcion no esta especificado en C y difiere entre x86 y ARM).
SIM="$1"; shift; fail=0
for c in "$@"; do
  b="${c%.c}"; gcc -w "$c" -o "$b.nativo" 2>/dev/null || gcc -w -m32 "$c" -o "$b.nativo" || { echo "no compila nativo: $c"; continue; }
  if [ -f "$b.in" ]; then "$b.nativo" < "$b.in" > "$b.esperado"; inp="$(sed ':a;N;$!ba;s/\n/\\n/g' "$b.in")\\n"; else "$b.nativo" > "$b.esperado" </dev/null; inp=""; fi
  for O in -O0 -O1 -O2 -Os; do
    "$SIM" "$c" $O --input "$inp" > "$b.sim" 2>/dev/null
    if cmp -s "$b.esperado" "$b.sim"; then echo "OK    $c $O"; else echo "FALLO $c $O"; fail=1; fi
  done
  rm -f "$b.nativo" "$b.esperado" "$b.sim"
done
exit $fail
