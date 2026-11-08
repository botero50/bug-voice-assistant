void settings_copy(void *s){ (void)s; }
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "local_skills_internal.h"
void ls_say(local_answer_t *a, int c){ if(a->clip_count<LOCAL_MAX_CLIPS) a->clips[a->clip_count++]=c; }
void ls_say_number(local_answer_t *a, int n){ ls_say(a, 1000+n); }
void ls_finish(local_answer_t *a,const char*r,const char*s){ strcpy(a->reply,r); strcpy(a->screen,s?s:""); }
char *ls_http_get(const char *url, bool secure){ (void)url;(void)secure; FILE *f=fopen("t.ics","rb"); if(!f) return NULL; char *b=malloc(20000); size_t n=fread(b,1,19999,f); b[n]=0; fclose(f); return b; }
int main(void){ local_calendar_set_urls("http://x/t.ics"); local_answer_t a; memset(&a,0,sizeof a);
 int ok=local_calendar_try(" que tengo hoy ",&a); printf("%d | %s | %s | clips %d\n%s\n",ok,a.reply,a.screen,a.clip_count,a.json?a.json:"(no json)"); }
