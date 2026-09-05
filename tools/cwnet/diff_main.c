#include "yhf_type.h"
#include <stdio.h>
#include <stdint.h>
#include "CwStreamEnc.h"
#include "cwnet_timestamp.h"
int main(void) {
    int eb = 0, db = 0;
    for (int ms = -50; ms <= 2000; ms++)
        if (CwStreamEnc_MillisecondsTo7BitTimestamp(ms) != cwstream_encode_timestamp(ms)) eb++;
    for (int b = 0; b <= 127; b++)
        if (CwStreamEnc_7BitTimestampToMilliseconds((BYTE)b) != cwstream_decode_timestamp((uint8_t)b)) db++;
    printf("ENCODE scarti: %d su 2051 | DECODE scarti: %d su 128\n", eb, db);
    return (eb || db) ? 1 : 0;
}
