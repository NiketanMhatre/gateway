#include "fixGateway.h"

#include "common.h"

char *sprintCTime_st() {
	static char 	res[TMP_STRING_SIZE];
	time_t 			t;
	int    			u;
	struct tm 		*tm;

	t = time(NULL);
	tm =  localtime(&t);
	sprintf(res, "%4d-%02d-%02d %02d:%02d:%02d", 
			1900+tm->tm_year, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec
			);
	return(res);
}

void readBufferInit(struct readBuffer *b, int bsize, int minSizeForRead, int fd) {
	// allocate 1 byte more for quotas sentinel (optimization for faster parsing)
	// ALLOCC(b->buffer, bsize+1, char);
	assert(bsize == READ_BUFFER_SIZE);
	b->bufferSize = bsize;
	b->i = b->j = 0;
	b->minSizeForRead = minSizeForRead;
	b->readSocketFd = fd;
}

void readBufferReset(struct readBuffer *b) {
	b->i = b->j = 0;
}

int readBufferRepositionAndReadNextChunk(struct readBuffer *b, int readSocketFd) {
	int n, res;

	if (b->i == b->j) {
		// we have previously processed all the buffer
		b->i = b->j = 0;
	} else if (b->bufferSize - b->j < b->minSizeForRead) {
		// we rolled to the end of the buffer, move the trail to get enough of space
		n = b->j - b->i;
		memmove(b->buffer, b->buffer + b->i, n);
		b->i = 0; 
		b->j = n;
	}
	if (b->bufferSize - b->j < b->minSizeForRead) {
		errorPrintf("Error: read buffer is too short to hold whole line\n");
		b->i = b->j = 0;
	}

	// if (debug > 5) {printf("read from socket %d\n", b->readSocketFd); fflush(stdout);}

	//res = read(readSocketFd, b->buffer + b->j, b->bufferSize - b->j); // read does not work under Windows!
	res = recv(readSocketFd, b->buffer + b->j, b->bufferSize - b->j, 0);
	// debugPrintf("%d bytes read from socket %d", res, readSocketFd);


	if (res == 0) {
		static int messageShowed = 0;
		if (messageShowed == 0) {
			debugPrintf("%s: 0 bytes read from socket %d: Probably connection closed by  remote host.\n", sprintCTime_st(), readSocketFd);
			messageShowed = 1;
		}
	}

	if (res > 0) b->j += res;
	return(res);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
