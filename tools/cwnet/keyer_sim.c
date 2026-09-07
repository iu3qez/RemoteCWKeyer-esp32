/* keyer_sim - il KeyerThread di DL4YHF (KeyerThread.c:2707-2762) simulato
 * sull'encoder di riferimento CwStreamEnc.c, con un cronometro ideale in µs.
 * Produce i byte MORSE che il client ufficiale accoderebbe per una lista di
 * edge: e' la provenienza degli attesi sintetici in test_cwnet_client.c.
 * Il caso A riproduce il primo over della sessione 12 del 2026-09-05. */
#include "yhf_type.h"
#include <stdio.h>
#include <string.h>
#include "CwStreamEnc.h"

static T_CW_KEYING_FIFO fifo;
static long sw_start_us;     /* istante di partenza del cronometro, aggiustato */
static int  filling, sent;   /* fFillingTxFifo, fMorseOutput_sent */

static void reset(void){ memset(&fifo,0,sizeof fifo); sw_start_us=0; filling=0; sent=0; }

/* KeyerThread.c:2717-2741 (transizione) e :2743-2761 (fine over) */
static void tick(long now_us, int key, int dot_ms){
  long t_us; int enc;
  if(key != sent){
    t_us = now_us - sw_start_us;
    if(!filling){ t_us = 0; sw_start_us = now_us; filling = 1; }
    enc = CwStream_EncodeKeyUpDownEvent(&fifo, key, (int)((t_us + 500) / 1000));
    sw_start_us += (long)enc * 1000;          /* TIM_AdjustStopwatch_ms */
    sent = key;
  } else {
    t_us = now_us - sw_start_us;
    if(!key && t_us > 14000L * dot_ms && filling){
      enc = CwStream_EncodeKeyUpDownEvent(&fifo, key, (int)((t_us + 500) / 1000));
      sw_start_us += (long)enc * 1000;
      filling = 0;
    }
  }
}
static void dump(const char *tag){
  int i = fifo.iTailIndex; printf("%-28s", tag);
  while(i != fifo.iHeadIndex){ printf(" %02X", fifo.elem[i].bCmd); i=(i+1)&(CW_KEYING_FIFO_SIZE-1); }
  fifo.iTailIndex = fifo.iHeadIndex; printf("   (cronometro ora a %ld us)\n", sw_start_us);
}
typedef struct { long t; int k; } edge_t;
/* edge[] = {istante_us, stato}; poll ogni period_us fino a t_end */
static void run(const char *tag, const edge_t *e, int n, int dot_ms, long period_us, long t_end){
  int i=0; long t; int key=0; reset();
  for(t=0; t<=t_end; t+=period_us){
    while(i<n && e[i].t <= t){ key=e[i].k; tick(e[i].t, key, dot_ms); i++; }
    tick(t, key, dot_ms);
  }
  dump(tag);
}
int main(void){
  edge_t A[]={{0,1},{48000,0},{96000,1},{240000,0}};
  run("A primo over, dot 48", A, 4, 48, 2000, 1500000);        /* -> 80 24 A4 3C 60 */
  edge_t B[]={{0,1},{33000,0},{66000,1},{99000,0},{132000,1}};
  run("B edge a 33 ms", B, 5, 48, 2000, 132000);                /* -> 80 20 A0 20 A1 */
  edge_t C[]={{0,1},{20000,0},{2020000,1},{2050000,0}};
  run("C gap 2000 ms spezzato", C, 4, 240, 2000, 2050000);      /* -> 80 14 FF EA 21 */
  edge_t D[]={{0,1},{31600,0},{63200,1}};
  run("D arrotondamento 31.6 ms", D, 3, 48, 2000, 64000);       /* -> 80 20 9F */
  edge_t E[]={{0,1},{48000,0}};
  run("E fine over, poll 1 ms", E, 2, 48, 1000, 800000);        /* -> 80 24 60 */
  edge_t F[]={{0,1},{100000,0}};
  run("F fine over a 12 WPM (dot 100)", F, 2, 100, 1000, 2000000); /* -> 80 31 7F 44: soglia 1400 > 1165 */
  return 0;
}
