#include "yhf_type.h"
#include <stdio.h>
#include "CwStreamEnc.h"
int main(void){
  struct{int down,wait;}ev[]={{1,0},{0,180},{1,60},{0,60},{1,180},{0,60},{1,60},{0,180},
    {1,180},{0,60},{1,180},{0,60},{1,60},{0,60},{1,180},{0,500},{0,0}};
  int n=(int)(sizeof ev/sizeof ev[0]); unsigned char fr[160]; int k=0;
  fr[k++]=0x10|0x40; fr[k++]=(unsigned char)n;
  for(int i=0;i<n;i++) fr[k++]=(unsigned char)((ev[i].down?0x80:0)|CwStreamEnc_MillisecondsTo7BitTimestamp(ev[i].wait));
  FILE*f=fopen("synth_morse.bin","wb"); fwrite(fr,1,(size_t)k,f); fclose(f);
  printf("synth_morse.bin: %d byte\n",k); return 0;}
