#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include "local_skills_internal.h"
bool ls_translate(const char *f,const char *t,const char *x,char *out,size_t n){ snprintf(out,n,"[%s>%s %s]",f,t,x); return true; }
char *ls_article_json(const char *a,const char *b,const char *c){ char *j=malloc(900); snprintf(j,900,"{title:%s|text:%s}",a,b); return j; }
bool local_weather_brief(char *o,size_t n){ snprintf(o,n,"Afuera hay 21 grados."); return true; }
bool local_calendar_brief(char *o,size_t n){ return false; }
void ls_speak_lang(local_answer_t *a,const char *t,const char *l){ strcpy(a->speak,t); strcpy(a->speak_lang,l); }
void ls_speak(local_answer_t *a,const char *t){ strcpy(a->speak,t); a->speak_lang[0]=0; }
#define BOOK_PATCH
#include "local_more_host.c"
void ls_say(local_answer_t*a,int c){ if(a->clip_count<LOCAL_MAX_CLIPS) a->clips[a->clip_count++]=c; }
void ls_say_number(local_answer_t*a,int n){}
void ls_finish(local_answer_t*a,const char*r,const char*s){ strcpy(a->reply,r); strcpy(a->screen,s?s:""); }
int main(void){
 const char *t[][2]={{"¿Cómo se dice casa en inglés?"," como se dice casa en ingles "},{"Traduce buenos días al francés"," traduce buenos dias al frances "},
  {"traduce al inglés dónde está el baño"," traduce al ingles donde esta el bano "},{"Buenos días"," buenos dias "},
  {"lee el libro cien años"," lee el libro cien anos "},{"sigue leyendo"," sigue leyendo "},{"repite la página"," repite la pagina libro "},{"mis libros"," mis libros "},{"qué hora es"," que hora es "}};
 for(int i=0;i<9;i++){ local_answer_t a; memset(&a,0,sizeof a); int ok=local_more_try(t[i][0],t[i][1],&a); printf("%-40s %d | reply=%s | lang=%s | speak=%.90s | screen=%s\n",t[i][0],ok,a.reply,a.speak_lang,a.speak,a.screen); }
}
