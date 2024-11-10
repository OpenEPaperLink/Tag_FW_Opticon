#ifndef _MEINRADIO_H
#define _MEINRADIO_H

void radioInit(void);
void radioTx(__xdata uint8_t* buff);
int8_t radioRx(__xdata void* data);
void radioRxEnable(bool renable);
void radioOn(void);
void radioOff(void);
void radioSetChannel(uint8_t channel);
void radioRxFlush(void);


extern int8_t __xdata lastRSSI;
extern int8_t __xdata lastLQI;



#endif