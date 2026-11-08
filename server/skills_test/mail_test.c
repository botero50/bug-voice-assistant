#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../../firmware/main/local_mail.c"
void settings_copy(bug_settings_t *s){ (void)s; }
void ls_say(local_answer_t*a,int c){} void ls_say_number(local_answer_t*a,int n){} void ls_finish(local_answer_t*a,const char*r,const char*s){}
char *ls_http_get(const char *u, bool s){ return 0; }
int main(void){
 const char *resp =
 "* 12 EXISTS\r\n* 10 FETCH (FLAGS (\Seen) BODY[HEADER.FIELDS (FROM SUBJECT DATE)] {143}\r\n"
 "From: =?UTF-8?B?QW5hIEfDs21leg==?= <ana@example.com>\r\nSubject: =?ISO-8859-1?Q?Reuni=F3n_de_ma=F1ana?=\r\nDate: Fri, 6 Nov 2026 09:15:00 -0500\r\n\r\n)\r\n"
 "* 11 FETCH (FLAGS () BODY[HEADER.FIELDS (FROM SUBJECT DATE)] {136}\r\n"
 "From: Banco <noreply@banco.co>\r\nSubject: Tu extracto est=?UTF-8?Q?=C3=A1?= listo\r\n  y es largo\r\nDate: Sun, 8 Nov 2026 14:05:00 +0000\r\n\r\n)\r\n"
 "A3 OK done\r\n";
 parse_fetch(resp);
 for(int i=0;i<mail_count;i++) printf("%d | %s | %s | %s | unread=%d\n",i,mails[i].from,mails[i].subject,mails[i].when,mails[i].unread);
 printf("unread %d\n",unread_count);
}
