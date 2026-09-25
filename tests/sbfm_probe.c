#include <unistd.h>
#include <string.h>
extern void sb_sbfiz(unsigned char*,unsigned char*);
extern void sb_sbfiz2(unsigned char*,unsigned char*);
extern void sb_sbfx(unsigned char*,unsigned char*);
static const char hx[]="0123456789abcdef";
int main(void){
    struct { const char *n; void (*f)(unsigned char*,unsigned char*); } t[]={
      {"sbfiz_3_20",sb_sbfiz},{"sbfiz_1_8",sb_sbfiz2},{"sbfx_5_9",sb_sbfx}};
    unsigned char in[16],res[16]; char out[64]; int o;
    /* 输入 1: 0x0000000000123456；输入逐轮换：0x8000000000ABCDEF 等 */
    unsigned long long vals[3] = {0x0000000000123456ULL, 0x8000000000ABCDEFULL, 0x0000000087654321ULL};
    for (unsigned i=0;i<3;i++){
        memcpy(in,&vals[i],8);
        memset(res,0xcc,sizeof res); t[i].f(in,res);
        o=0; while(t[i].n[o]) out[o]=t[i].n[o],o++;
        out[o++]=':';
        for (int j=0;j<8;j++){ out[o++]=hx[res[j]>>4]; out[o++]=hx[res[j]&15]; }
        out[o++]='\n'; write(1,out,(size_t)o);
    }
    return 0;
}
