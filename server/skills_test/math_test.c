#include <stdio.h>
#include <string.h>
#include "local_skills_internal.h"
void ls_say(local_answer_t *a, int c){ if(a->clip_count<LOCAL_MAX_CLIPS) a->clips[a->clip_count++]=c; }
void ls_say_number(local_answer_t *a, int n){ ls_say(a, 1000+n); }
void ls_finish(local_answer_t *a,const char*r,const char*s){ strcpy(a->reply,r); strcpy(a->screen,s?s:""); }
int main(void){
  const char *t[]={" cuanto es 12 por 5 "," cuanto es doce mas treinta y cinco "," cuanto es 100 dividido 8 "," cuanto es 5 dividido por 0 ",
   " cuanto es dos mil quinientos menos 3000 "," cuanto es 1500 por 1000 "," cuantos kilometros son 10 millas "," cuantos grados fahrenheit son 20 celsius ",
   " convierte 70 grados fahrenheit a celsius "," cuantas libras son 5 kilos "," que hora es "," cuanto es 7 minutos "," cuanto cuesta el dolar ",NULL};
  for(int i=0;t[i];i++){ local_answer_t a; memset(&a,0,sizeof a); int ok=local_math_try(t[i],&a); printf("%-50s %s | %s | %d clips\n",t[i],ok?"OK":"no",a.reply,a.clip_count);}
}
