#include <unistd.h>
#include <string.h>
extern void rv_rev(unsigned char*,unsigned char*);
extern void rv_rev32(unsigned char*,unsigned char*);
extern void rv_rev_w(unsigned char*,unsigned char*);
extern void rv_clz(unsigned char*,unsigned char*);
static const char hx[]="0123456789abcdef";
int main(void){
    struct { const char *n; void (*f)(unsigned char*,unsigned char*); } t[]={
      {"rev",rv_rev},{"rev32",rv_rev32},{"rev_w",rv_rev_w},{"clz",rv_clz}};
    unsigned char in[16],res[16]; char out[64]; int o;
    for (int i=0;i<16;i++) in[i]=(unsigned char)(i+1);   /* 01 02 03 ... 08 */
    for (unsigned i=0;i<4;i++){
        memset(res,0xcc,sizeof res); t[i].f(in,res);
        o=0; while(t[i].n[o]) out[o]=t[i].n[o],o++;
        out[o++]=':';
        for (int j=0;j<8;j++){ out[o++]=hx[res[j]>>4]; out[o++]=hx[res[j]&15]; }
        out[o++]='\n'; write(1,out,(size_t)o);
    }
    return 0;
}
