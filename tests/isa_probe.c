#include <unistd.h>
#include <string.h>
typedef void (*fn_t)(unsigned char *, unsigned char *);
#define P(n) extern void pr_##n(unsigned char *, unsigned char *);
P(sri3) P(urshr2) P(sqshl1) P(ushll1) P(movi4h) P(mvni4s) P(dupelem) P(uqxtn)
P(rbit) P(fcvtl) P(sminv) P(smaxv) P(addp) P(bsl) P(bif) P(ext0) P(ext2)
P(ushr2) P(sshr2) P(shl1)
static struct { const char *n; fn_t f; } t[] = {
 {"sri3",pr_sri3},{"urshr2",pr_urshr2},{"sqshl1",pr_sqshl1},{"ushll1",pr_ushll1},
 {"movi4h",pr_movi4h},{"mvni4s",pr_mvni4s},{"dupelem",pr_dupelem},{"uqxtn",pr_uqxtn},
 {"rbit",pr_rbit},{"fcvtl",pr_fcvtl},{"sminv",pr_sminv},{"smaxv",pr_smaxv},
 {"addp",pr_addp},{"bsl",pr_bsl},{"bif",pr_bif},{"ext0",pr_ext0},{"ext2",pr_ext2},
 {"ushr2",pr_ushr2},{"sshr2",pr_sshr2},{"shl1",pr_shl1},
};
static const char hx[]="0123456789abcdef";
int main(void){
    unsigned char in[256], res[256]; char out[256]; int o=0;
    for (int i=0;i<256;i++) in[i]=(unsigned char)i;     /* 可读模式：0,1,2,... */
    for (unsigned i=0;i<sizeof t/sizeof t[0];i++){
        memset(res,0xcc,sizeof res);
        t[i].f(in,res);
        o=0; while(t[i].n[o]) out[o]=t[i].n[o], o++;
        out[o++]=':';
        for (int j=0;j<32;j++){ out[o++]=hx[res[j]>>4]; out[o++]=hx[res[j]&15]; }
        out[o++]='\n';
        write(1,out,(size_t)o);
    }
    return 0;
}
