/* cwnet_dump - decodifica un flusso CWNet grezzo (una direzione).
 * Parser di frame: il nostro. Codec del keying: DL4YHF. Strumento diagnostico. */
#include "yhf_type.h"
#include <stdio.h>
#include <stdlib.h>
#include "CwStreamEnc.h"
#include "cwnet_frame.h"
#define CMD_MORSE 0x10
static const char *nm(uint8_t c){switch(c){
 case 0x10:return "MORSE (DL4YHF)"; case 0x03:return "PING";
 case 0x14:return "CI-V  (= nostro CW_UP)"; case 0x15:return "SPECTRUM (= nostro CW_DOWN)";
 case 0x01:return "CONNECT"; case 0x02:return "DISCONNECT"; case 0x11:return "AUDIO";
 case 0x04:return "PRINT"; case 0x05:return "TX_INFO"; default:return "?";}}
static void morse(const uint8_t *p, uint16_t n){
  long t=0; printf("      stream a 7 bit, %u eventi:\n", n);
  for(uint16_t i=0;i<n;i++){int d=(p[i]&0x80)?1:0;
    int w=CwStreamEnc_7BitTimestampToMilliseconds((BYTE)(p[i]&0x7F)); t+=w;
    printf("        [%3u] 0x%02X  attesa %4d ms -> t=%6ld ms  key %s\n",i,p[i],w,t,d?"DOWN":"up");}}
int main(int argc,char**argv){
  if(argc<2){fprintf(stderr,"uso: cwnet_dump <flusso.bin> [frammento]\n");return 2;}
  FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 2;}
  static uint8_t buf[1<<22]; size_t len=fread(buf,1,sizeof buf,f); fclose(f);
  size_t frag=(argc>2)?(size_t)atoi(argv[2]):0;
  printf("== %s: %zu byte ==\n\n",argv[1],len);
  cwnet_frame_parser_t ps; cwnet_frame_parser_init(&ps);
  size_t off=0,tot=0; int nf=0;
  while(tot<len){
    size_t ch=len-tot; if(frag&&ch>frag) ch=frag;
    cwnet_parse_result_t r=cwnet_frame_parse(&ps,buf+tot,ch);
    if(r.status==CWNET_PARSE_ERROR){printf("offset %zu: ERRORE DI PARSING\n",tot);break;}
    if(r.status==CWNET_PARSE_OK){nf++;
      printf("frame %d @ offset %zu: cmd 0x%02X %-24s payload %u byte\n",nf,off,r.command,nm(r.command),r.payload_len);
      if(r.payload_len&&r.payload){printf("      hex:");
        for(uint16_t i=0;i<r.payload_len&&i<32;i++)printf(" %02X",r.payload[i]);
        if(r.payload_len>32)printf(" ...");printf("\n");
        if(r.command==CMD_MORSE) morse(r.payload,r.payload_len);}
      off=tot+r.bytes_consumed;}
    if(r.bytes_consumed==0){printf("nessun progresso, mi fermo\n");break;}
    tot+=r.bytes_consumed;}
  printf("\n== %d frame, %zu/%zu byte consumati ==\n",nf,tot,len);
  return 0;}
