#include <string.h>
#include <stdbool.h>
static inline size_t strlcpy(char *d,const char *s,size_t n){size_t l=strlen(s); if(n){size_t c=l>=n?n-1:l; memcpy(d,s,c); d[c]=0;} return l;}
static inline size_t strlcat(char *d,const char *s,size_t n){size_t dl=strlen(d); if(dl>=n) return dl+strlen(s); size_t sl=strlen(s); size_t c=sl<n-dl-1?sl:n-dl-1; memcpy(d+dl,s,c); d[dl+c]=0; return dl+sl;}
#define localtime_r(t,r) (*(r) = *localtime(t), (r))
#include <ctype.h>
static inline char *strcasestr(const char *h, const char *n){ size_t l=strlen(n); if(!l) return (char*)h; for(; *h; h++){ size_t i=0; while(i<l && h[i] && tolower((unsigned char)h[i])==tolower((unsigned char)n[i])) i++; if(i==l) return (char*)h; } return 0; }
