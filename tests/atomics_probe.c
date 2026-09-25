#include <unistd.h>
#include <string.h>
extern void at_bic_stlxr(unsigned char*, unsigned char*);
extern void at_orr_stlxr(unsigned char*, unsigned char*);
extern void at_add_stlxr(unsigned char*, unsigned char*);
extern void at_cas_loop(unsigned char*, unsigned char*);
static const char hx[]="0123456789abcdef";
int main(void){
    struct { const char *n; void (*f)(unsigned char*,unsigned char*); } t[] = {
      {"bic_stlxr",at_bic_stlxr},{"orr_stlxr",at_orr_stlxr},
      {"add_stlxr",at_add_stlxr},{"cas_loop",at_cas_loop}};
    unsigned char in[64], res[64]; char out[128]; int o;
    for (unsigned i=0;i<4;i++){
        memset(in,0,sizeof in); in[0]=0x55;          /* 0x55: bit0,2,4,6 */
        memset(res,0xcc,sizeof res);
        t[i].f(in,res);
        o=0; while(t[i].n[o]) out[o]=t[i].n[o], o++;
        out[o++]=':';
        for (int j=0;j<16;j++){ out[o++]=hx[res[j]>>4]; out[o++]=hx[res[j]&15]; }
        out[o++]=' '; out[o++]='i'; out[o++]='n'; out[o++]='=';
        for (int j=0;j<8;j++){ out[o++]=hx[in[j]>>4]; out[o++]=hx[in[j]&15]; }
        out[o++]='\n'; write(1,out,(size_t)o);
    }
    return 0;
}
