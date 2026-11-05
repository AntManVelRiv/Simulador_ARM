#include <stdio.h>
int main(void){
  int a, b, n=0, total=0;
  char nombre[20];
  printf("Nombre: "); scanf("%19s", nombre);
  printf("Dos numeros: "); scanf("%d", &a); scanf("%d", &b);
  printf("Hola %s: %d + %d = %d\n", nombre, a, b, a+b);
  int x;
  while (scanf("%d", &x) == 1) { total += x; n++; }
  printf("leidos %d, total %d\n", n, total);
  return n;
}
