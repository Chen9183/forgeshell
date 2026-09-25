#include <unistd.h>
#include <string.h>
extern void mp_adc(unsigned char*,unsigned char*);
extern void mp_sbc(unsigned char*,unsigned char*);
extern void mp_ngc(unsigned char*,unsigned char*);
extern void mp_ldxrb(unsigned char*,unsigned char*);
extern void mp_swp(unsigned char*,unsigned char*);
extern void mp_sbfiz(unsigned char*,unsigned char*);
extern void mp_sbfx(unsigned char*,unsigned char*);
extern void mp_extr(unsigned char*,unsigned char*);
extern void mp_bfi(unsigned char*,unsigned char*);
static const char hx[]="0123456789abcdef";
int main(void){
    struct { const char *n; void (*f)(unsigned char*,unsigned char*); } t[]={
      {"adc",mp_adc},{"sbc",mp_sbc},{"ngc",mp_ngc},{"ldxrb",mp_ldxrb},{"swp",mp_swp},
      {"sbfiz",mp_sbfiz},{"sbfx",mp_sbfx},{"extr",mp_extr},{"bfi",mp_bfi}};
    unsigned char in[64],res[64]; char out[128]; int o;
    /* 输入：0x0102030405060708 / 0xF0E0D0C0B0A09080 */
    for (int i=0;i<8;i++){ in[i]=(unsigned char)(8-i); in[8+i]=(unsigned char)(0x80|(i*0x10)); }
    for (unsigned i=0;i<sizeof t/sizeof t[0];i++){
        memset(res,0xcc,sizeof res); t[i].f(in,res);
        o=0; while(t[i].n[o]) out[o]=t[i].n[o],o++;
        out[o++]=':';
        for (int j=0;j<32;j++){ out[o++]=hx[res[j]>>4]; out[o++]=hx[res[j]&15]; }
        out[o++]='\n'; write(1,out,(size_t)o);
    }
    return 0;
}
