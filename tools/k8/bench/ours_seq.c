/* Legge dallo stdin righe "t_us dit dah" e stampa la sequenza di elementi.
 * Pilota la FSM a 1 ms, come rt_task. */
#include <stdio.h>
#include <stdlib.h>
#include "iambic.h"
#include "stubs/esp_stubs.h"
#define MAXEV 64
int main(int argc, char**argv){
    uint32_t wpm = (argc>1)? (uint32_t)atoi(argv[1]) : 25;
    int mode_b   = (argc>2)? atoi(argv[2]) : 0;
    int64_t tend = (argc>3)? atoll(argv[3]) : 1000000;
    int64_t et[MAXEV]; int ed[MAXEV], ea[MAXEV]; int n=0;
    while(n<MAXEV && scanf("%lld %d %d",(long long*)&et[n],&ed[n],&ea[n])==3) n++;
    iambic_config_t c = IAMBIC_CONFIG_DEFAULT;
    c.wpm=wpm; c.mode = mode_b?IAMBIC_MODE_B:IAMBIC_MODE_A;
    c.squeeze_mode = SQUEEZE_MODE_SAMPLED; c.memory_mode = MEMORY_MODE_DOT_AND_DAH;
    iambic_processor_t p; iambic_init(&p,&c);
    int idx=0; bool dit=false,dah=false; iambic_state_t prev=IAMBIC_STATE_IDLE;
    for(int64_t t=0;t<=tend;t+=1000){
        while(idx<n && et[idx]<=t){ dit=ed[idx]; dah=ea[idx]; idx++; }
        esp_timer_set_time(t);
        iambic_tick(&p,t,gpio_from_paddles(dit,dah));
        if(p.state!=prev){
            if(p.state==IAMBIC_STATE_SEND_DIT) putchar('.');
            else if(p.state==IAMBIC_STATE_SEND_DAH) putchar('-');
        }
        prev=p.state;
    }
    putchar('\n'); return 0;
}
