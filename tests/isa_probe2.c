#include <unistd.h>
#include <string.h>
extern void pr2_bsl(unsigned char *, unsigned char *);
extern void pr2_bit(unsigned char *, unsigned char *);
extern void pr2_bif(unsigned char *, unsigned char *);
extern void pr2_urshr2(unsigned char *, unsigned char *);
extern void pr2_sri3(unsigned char *, unsigned char *);
extern void pr2_sqshl3(unsigned char *, unsigned char *);
extern void pr2_sminv(unsigned char *, unsigned char *);
extern void pr2_smaxv(unsigned char *, unsigned char *);
extern void pr2_uminv(unsigned char *, unsigned char *);
extern void pr2_rbit(unsigned char *, unsigned char *);
extern void pr2_clz(unsigned char *, unsigned char *);
extern void pr2_cls(unsigned char *, unsigned char *);
extern void pr2_addp(unsigned char *, unsigned char *);
extern void pr2_addlv(unsigned char *, unsigned char *);
extern void pr2_uaddlv(unsigned char *, unsigned char *);
extern void pr2_uqxtn(unsigned char *, unsigned char *);
extern void pr2_sqxtn(unsigned char *, unsigned char *);
extern void pr2_fcvtl(unsigned char *, unsigned char *);
static struct { const char *n; void (*f)(unsigned char*,unsigned char*); } t[] = { {"bsl",pr2_bsl}, {"bit",pr2_bit}, {"bif",pr2_bif}, {"urshr2",pr2_urshr2}, {"sri3",pr2_sri3}, {"sqshl3",pr2_sqshl3}, {"sminv",pr2_sminv}, {"smaxv",pr2_smaxv}, {"uminv",pr2_uminv}, {"rbit",pr2_rbit}, {"clz",pr2_clz}, {"cls",pr2_cls}, {"addp",pr2_addp}, {"addlv",pr2_addlv}, {"uaddlv",pr2_uaddlv}, {"uqxtn",pr2_uqxtn}, {"sqxtn",pr2_sqxtn}, {"fcvtl",pr2_fcvtl},};
static const char hx[]="0123456789abcdef";
int main(void){ unsigned char in[256], res[256]; char out[256]; int o;
  for (int i=0;i<256;i++) in[i]=0x55;
  for (int i=0;i<16;i++) in[i]=0xCC;
  for (int i=16;i<32;i++) in[i]=0xAA;
  for (int i=32;i<48;i++) in[i]=0xF0;
  for (unsigned i=0;i<sizeof t/sizeof t[0];i++){ memset(res,0xcc,sizeof res); t[i].f(in,res);
    o=0; while(t[i].n[o]) out[o]=t[i].n[o], o++; out[o++]=':';
    for (int j=0;j<32;j++){ out[o++]=hx[res[j]>>4]; out[o++]=hx[res[j]&15]; } out[o++]='\n';
    write(1,out,(size_t)o);} return 0; }
