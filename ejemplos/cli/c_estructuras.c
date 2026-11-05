#include <stdio.h>
#include <stdlib.h>
struct node { int v; struct node *next; };
typedef struct { int x, y; char name[8]; } pt;
int apply(int (*f)(int), int v){ return f(v); }
int dbl(int x){ return 2*x; }
int sq(int x){ return x*x; }
int main(void){
  struct node *h = NULL;
  for(int i=1;i<=5;i++){ struct node *n = malloc(sizeof *n); n->v=i*i; n->next=h; h=n; }
  int s=0; for(struct node*p=h;p;p=p->next){ printf("%d ", p->v); s+=p->v; }
  printf("= %d\n", s);
  pt p = {3, -4, "abc"}; pt q = p; q.x += 10;
  printf("%d %d %s | %d %d\n", p.x, p.y, p.name, q.x, q.y);
  printf("%d %d\n", apply(dbl,21), apply(sq,12));
  int m[3][3]; for(int i=0;i<3;i++) for(int j=0;j<3;j++) m[i][j]=i*3+j;
  int tr=0; for(int i=0;i<3;i++) tr+=m[i][i]; printf("trace=%d\n", tr);
  switch(tr){ case 12: puts("doce"); break; default: puts("otro"); }
  unsigned b = 0xF0F0; printf("%u %u %u\n", b>>4, b<<3, b ^ 0xFFFF);
  short sh = -5; unsigned char uc = 250; uc += 10; printf("%d %d\n", sh, uc);
  char buf[40]; sprintf(buf, "[%3d:%s]", 7, "ok"); puts(buf);
  return 0;
}
