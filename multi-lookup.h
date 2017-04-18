#ifndef MULTILOOKUP_H
#define MULTILOOKUP_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/sem.h>
#include <semaphore.h>
#include "queue.h"
#include "util.h"

void* requesterThread(void* inputfile);

void* resolverThread(void* outputfile);

#endif