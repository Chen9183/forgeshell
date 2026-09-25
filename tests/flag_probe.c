#include <unistd.h>
#include <string.h>
extern void fp_cmp_imm(unsigned char*,unsigned char*);
extern void fp_cmp_reg(unsigned char*,unsigned char*);
extern void fp_adds_imm(unsigned char*,unsigned char*);
static const char hx[]="0123456789abcdef";
int main(void){
    struct { const char *n; void (*f)(unsigned char*,unsigned char*); } t[]={
      {"cmp_imm",fp_cmp_imm},{"cmp_reg",fp_cmp_reg},{"adds_imm",fp_adds_imm}};
    unsigned char in[32],res[32]; char out[64]; int o;
    for (int i=0;i<8;i++){ in[i]=(unsigned char)(8-i); in[8+i]=(unsigned char)i; }  /* x2大, x3小 */
    for (unsigned i=0;i<3;i++){
        memset(res,0xcc,sizeof res); t[i].f(in,res);
        o=0; while(t[i].n[o]) out[o]=t[i].n[o],o++;
        out[o++]=':';
        for (int j=0;j<8;j++){ out[o++]=hx[res[j]>>4]; out[o++]=hx[res[j]&15]; }
        out[o++]='\n'; write(1,out,(size_t)o);
    }
    return 0;
}
