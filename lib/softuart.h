#ifndef SOFTUART_H
#define SOFTUART_H

extern void initSoftUart(void);
inline void shutdownUart(void);
extern void softUartSendByte(char u);

#endif
