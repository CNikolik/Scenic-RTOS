#ifndef _WAITINGQUEUE
#define _WAITINGQUEUE

#include <stdint.h>
#include <LPC17xx.h>
#include "osDefs.h"

int Rear;
int Front;

uint32_t enqueue(int threadId);
thread dequeue();
void show();

#endif