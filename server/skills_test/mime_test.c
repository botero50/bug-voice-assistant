#include <stdio.h>
#include <string.h>
#include <time.h>
#include "local_skills_internal.h"
#include "../../firmware/main/local_mail.c"
void settings_copy(bug_settings_t *s){ (void)s; }
void ls_say(local_answer_t*a,int c){} void ls_say_number(local_answer_t*a,int n){} void ls_finish(local_answer_t*a,const char*r,const char*s){}
char *ls_http_get(const char *u, bool s){ return 0; }
void ls_speak(local_answer_t *a,const char *t){}
static void run(const char *name, const char *headers, const char *body){
  char out[360]; bool html=false; bool ok=decode_part(headers,strlen(headers),body,strlen(body),0,out,sizeof out,&html);
  printf("%s: %d html=%d | %s\n",name,ok,html,out);
}
int main(void){
 run("qp-alt",
  "Content-Type: multipart/alternative; boundary=\"XYZ\"\r\nContent-Transfer-Encoding: 7bit\r\n",
  "This is a multi-part message\r\n--XYZ\r\nContent-Type: text/plain; charset=iso-8859-1\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nHola Ana,=0D\r\nTu reuni=F3n de ma=F1ana es a las 9. Gracias=\r\n por todo.\r\n> texto citado\r\n--XYZ\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<p>Hola <b>Ana</b></p>\r\n--XYZ--\r\n");
 run("base64",
  "Content-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: base64\r\n",
  "SG9sYSwgZXN0ZSBlcyB1biBjb3JyZW8gZW4gYmFzZTY0LiBTYWx1ZG9zIGRlc2RlIE1hY29uZG8u\r\n");
 run("html-only",
  "Content-Type: text/html; charset=utf-8\r\n",
  "<html><style>p{color:red}</style><body><p>Tu <b>pedido</b> sali&oacute; &amp; llega ma&ntilde;ana.</p><br>Gracias</body></html>");
 run("mixed",
  "Content-Type: multipart/mixed; boundary=B1\r\n",
  "--B1\r\nContent-Type: multipart/alternative; boundary=B2\r\n\r\n--B2\r\nContent-Type: text/plain\r\n\r\nTexto simple dentro de mixed.\r\n--B2--\r\n--B1\r\nContent-Type: application/pdf\r\nContent-Transfer-Encoding: base64\r\n\r\nJVBERi0xLjQK\r\n--B1--\r\n");
}
