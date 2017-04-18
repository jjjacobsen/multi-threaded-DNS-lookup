/*
 * File: multi-lookup.c
 * Author: Jonah Jacobsen
 * Project: CSCI 3753 Programming Assignment 3
 */

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
#include "multi-lookup.h"

#define MINARGS 3
#define USAGE "<inputFile> <anotherInputFile> <...> <outputFile>"
#define SBUFSIZE 1025
#define INPUTFS "%1024s"
#define QUEUE_SIZE 8


/* create the mutex's */
pthread_mutex_t bufferLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t threadCountLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t outputLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t inputLock = PTHREAD_MUTEX_INITIALIZER;

/* initialize the queue */
queue q;

/* make file pointers and global vars */
FILE* inputfp = NULL;
FILE* outputfp = NULL;
int requestThreadCount;

/* here are ways to lock are release sems and mutex, also the sleep command
sem_wait(&semname)
sem_post(&semname)
pthread_mutex_lock(&mutexname)
pthread_mutex_unlock(&mutexname)
usleep((rand()%100)*1000) */

/* these are critical data types that need to be protected
1. output file
2. buffer
3. requestThreadCount
4. hostname? - made local var
5. input file */

/* how requesters run:
1. create local vars and then lock
2. open input file and start a while loop to read through the file
3. add a hostname to the buffer if the buffer is not full, make sure to
lock the buffer when checking and adding
4. at eof close file stream and decrement global requester thread count */

void* requesterThread(void* inputfile){
    /* create local var */
    char hostname[SBUFSIZE];
    char errorstr[SBUFSIZE];

    pthread_mutex_lock(&inputLock);

    /* open input file to read, print if error */
    inputfp = fopen(inputfile, "r");
    if(!inputfp){
      sprintf(errorstr, "Error Opening Input File");
    	perror(errorstr);
    	exit(EXIT_FAILURE);
    }

    /* this loop will read till eof and on each iteration it will store the
    line value into the var hostname */
    while(fscanf(inputfp, INPUTFS, hostname) > 0){
      /* check if queue is full, if it is then wait until it isn't, then
      push hostname to queue */
      pthread_mutex_lock(&bufferLock);
      while(queue_is_full(&q)){
        pthread_mutex_unlock(&bufferLock);
        // sleep between 0 and 100 micro seconds
        usleep((rand()%100)*1000);
        pthread_mutex_lock(&bufferLock);
      }
      /* strdup mallocs memory and copys the argument into this new memory,
      need to free this in resolver or else get memory leaks */
      queue_push(&q, strdup(hostname));
      pthread_mutex_unlock(&bufferLock);
    }

    fclose(inputfp);
    pthread_mutex_unlock(&inputLock);

    pthread_mutex_lock(&threadCountLock);
    requestThreadCount--;
    pthread_mutex_unlock(&threadCountLock);

    return EXIT_SUCCESS;
}

/* how resolvers run:
1. create local vars
2. enter while loop that will continue if the buffer is not empty or
if the requester thread count is still above 0
3. if there is something in the buffer pop it off then release lock on buffer
4. perform dnslookup, this is where the time savings come in since the lock
on buffer was release there can be 5 dns lookups happening at once
5. on dnslookup return lock the output file and write result to it
6. exit once while loop conditions both fail (empty queue and no more
requester threads to add more hostnames */

void* resolverThread(void* outputfile)
{
  /* create local vars */
  char hostname[SBUFSIZE];
  char firstipstr[INET6_ADDRSTRLEN];

  /* lock buffer and global var */
  pthread_mutex_lock(&bufferLock);
  pthread_mutex_lock(&threadCountLock);
  /* check if queue is empty or if all requesters are done (global var) */
  while(queue_is_empty(&q)<1 || requestThreadCount>0){
    pthread_mutex_unlock(&threadCountLock);
    /* make sure something is in queue */
    if(!queue_is_empty(&q)){
      /* pop hostname off queue, free memory, and then unlock buffer */
      char* memPointer = queue_pop(&q);
      strcpy(hostname, memPointer);
      free(memPointer);
      pthread_mutex_unlock(&bufferLock);

      /* perform a dns lookup, this takes time so OS will probably time
      switch this thread off the CPU */
      if(dnslookup(hostname, firstipstr, sizeof(firstipstr)) == UTIL_FAILURE){
			  fprintf(stderr, "dnslookup error: %s\n", hostname);
			  strncpy(firstipstr, "", sizeof(firstipstr));
	    }

      /* lock, open, and write to output file */
      pthread_mutex_lock(&outputLock);
      outputfp = fopen(outputfile, "a");
      /* error message */
      if(!outputfp){
        perror("Error Opening Output File");
        exit(EXIT_FAILURE);
      }
      fprintf(outputfp, "%s,%s\n", hostname, firstipstr);
      fclose(outputfp);
      pthread_mutex_unlock(&outputLock);
      pthread_mutex_lock(&bufferLock);
      pthread_mutex_lock(&threadCountLock);
    }
    else{
      /* case where queue is empty but there are still requester threads that
      will add to queue, just make sure locks are cleared */
      pthread_mutex_unlock(&bufferLock);
      // wait here
      usleep((rand()%100)*1000);
      pthread_mutex_lock(&bufferLock);
      pthread_mutex_lock(&threadCountLock);
    }
  }
  pthread_mutex_unlock(&threadCountLock);
  pthread_mutex_unlock(&bufferLock);

  /* while loop exits when buffer is empty and requestThreadCount = 0 meaning
  all hostnames from files are searched and we can exit the resolver threads */

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]){

	/* Check Arguments, print error messages if out of bounds */
    if(argc < MINARGS){
	     fprintf(stderr, "Not enough arguments: %d\n", (argc - 1));
       fprintf(stderr, "Usage:\n %s %s\n", argv[0], USAGE);
       return EXIT_FAILURE;
    }
    else if(argc > 12){
        fprintf(stderr, "Too many arguments: %d, max is 10\n", (argc - 1));
        return EXIT_FAILURE;
    }

	/* initialize threads, queue, and global var */
    pthread_t threads[argc-2];
    pthread_t rthreads[5];

    queue_init(&q, QUEUE_SIZE);

    requestThreadCount = argc-2;

    /* wipe/create output file */
    fclose(fopen(argv[argc-1],"w"));

    for(int t=0;t<argc-2;t++){
        /* create appropriate number of requester threads, print if error */
		    if (pthread_create(&(threads[t]), NULL, requesterThread, argv[argc-(t+2)])){
	    	    printf("ERROR when creating requester threads");
	    	    exit(EXIT_FAILURE);
		    }
    }

    for(int t=0;t<5;t++){
      /* create 5 resolver threads, print if error */
      if (pthread_create(&(rthreads[t]), NULL, resolverThread, argv[argc-1])){
          printf("ERROR when creating resolver threads");
          exit(EXIT_FAILURE);
      }
    }

    /* wait for all requester theads to finish */
    for(int t=0;t<argc-2;t++){
		    pthread_join(threads[t],NULL);
    }

    /* wait for all resolver threads to finish */
    for(int t=0;t<5;t++){
      pthread_join(rthreads[t],NULL);
    }

    /* free array from memory */
    queue_cleanup(&q);

    return EXIT_SUCCESS;
}
