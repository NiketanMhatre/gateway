#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <time.h>
//#include <unistd.h>
//#include <sys/types.h> 

#include <Windows.h>
#include <winsock.h>
#include <iphlpapi.h>


#define READ_BUFFER_SIZE                   2000
#define READ_BUFFER_MIN_SPACE_FOR_READ     100
#define TMP_STRING_SIZE                    256

#define ALLOCC(p,n,t)       {p = (t*) malloc((n)*sizeof(t)); if(p==NULL) {errorPrintf("Out of memory\n");}}
#define FREE(p)             {free(p);}

#undef assert
#define assert(x) {if ((x)==0) {errorPrintf("Assertion %d failed at %s:%d", #x, __FILE__, __LINE__);}}

// a read buffer is in fact a chunk in memory where data between buffer[i - j] are valid
struct readBuffer {
    char buffer[READ_BUFFER_SIZE];
    int  i, j;

    int bufferSize;
    int minSizeForRead;
    int readSocketFd;
};


// common.c
char *sprintCTime_st() ;
void readBufferInit(struct readBuffer *b, int bsize, int minSizeForRead, int fd) ;
void readBufferReset(struct readBuffer *b) ;
int readBufferRepositionAndReadNextChunk(struct readBuffer *b, int readSocketFd) ;
int getSymbolId(char *cp) ;


// fixGateway.c
void errorPrintf(char *fmt, ...);
void debugPrintf(char *fmt, ...);

