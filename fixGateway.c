#include "fixGateway.h"
#include "common.h"
#include "sglib.h"

#define ORDER_CRITICAL_SECTION      0

#define FIX_MAX_SEND_MESSAGE_SIZE	500
#define FIX_MIN_MESSAGE_LEN      	22
#define PRICE_DECIMALS   			6
#define PRICE_FACTOR				1000000		/* must be 10 ^ PRICE_DECIMALS */
#define MT_LOT_SIZE                 100000
#define MAX_MT4_ERR_CODE            5000

#define FIX_PREFIX_STRING_FORMAT 	"8=FIX.4.4" "\001" "9=%d"
#define FIX_BODY_START_OFFSET 		(sizeof(FIX_PREFIX_STRING_FORMAT) + 10)
#define FIX_SEND_BODY_BUFFER(bb) 	(bb + FIX_BODY_START_OFFSET)
#define FIX_RECOVERY_STRING      	"\0018=FIX.4"

// hope that we will not have conflicts at all
#define SIZE_OF_OPEN_ORDERS_HASH_TABLE 128
#define SYM_HASH_SIZE                  512

#define FIX_COMMON_HEADER_FORMAT					\
	"49=%s"  "\001"									\
	"56=%s"  "\001"									\
	"34=%d"   "\001"								\
	"52=%04d%02d%02d-%02d:%02d:%02d.000"			\
	"\001"											\


#define FIX_COMMON_HEADER_VALUES(fixCurrentSeqNum)						\
	fixSenderCompID,													\
		fixTargetCompID,												\
		fixCurrentSeqNum,												\
		1900+tm->tm_year, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec

enum fxgStates {
	STATE_INITIAL,
	STATE_WAITING_FOR_CLIENT,
	STATE_CLIENT_CONNECTED,
};

typedef struct order {
	int          symid;
	int          mbtn;
	int          ticket;
	int          cmd;
	int          volume;
	double       price;
	struct order *next;
} order;


static int serverPort = 9876;
static int latencyMode = 0;

static volatile int fxgState = 0;
static int clientsockfd = 0;
CRITICAL_SECTION socketCriticalSection; 
CRITICAL_SECTION orderCriticalSection; 
int fixSendCurrentSeqNum = 1;

char *fixSenderCompID = "MT4";
char fixTargetCompID[TMP_STRING_SIZE] = {'T', 0};

struct readBuffer readbuff;

struct order *orders[SIZE_OF_OPEN_ORDERS_HASH_TABLE];
struct order *exorders[SYM_HASH_SIZE];
char *symTable[SYM_HASH_SIZE];
char *errCodes[MAX_MT4_ERR_CODE];

//////////////////////////////////////////////////////////////////////////////////////
// hashing table mapping int to int

static int orderComparator(struct order *o1, struct order *o2) {
    return(o1->mbtn - o2->mbtn);
}

static inline unsigned orderHashFun(struct order *o) {
	return(o->mbtn);
}

// importing hashing on orders
SGLIB_DEFINE_LIST_PROTOTYPES(order, orderComparator, nextInHash);
SGLIB_DEFINE_LIST_FUNCTIONS(order, orderComparator, next);
SGLIB_DEFINE_HASHED_CONTAINER_PROTOTYPES(order, SIZE_OF_OPEN_ORDERS_HASH_TABLE, orderHashFun);
SGLIB_DEFINE_HASHED_CONTAINER_FUNCTIONS(order, SIZE_OF_OPEN_ORDERS_HASH_TABLE, orderHashFun) 

//////////////////////////////////////////////////////////////////////////////////////

void initErrCodes() {
	errCodes[0] = "ERR_NO_ERROR: No error returned.";
	errCodes[1] = "ERR_NO_RESULT: No error returned, but the result is unknown.";
	errCodes[2] = "ERR_COMMON_ERROR: Common error.";
	errCodes[3] = "ERR_INVALID_TRADE_PARAMETERS: Invalid trade parameters.";
	errCodes[4] = "ERR_SERVER_BUSY: Trade server is busy.";
	errCodes[5] = "ERR_OLD_VERSION: Old version of the client terminal.";
	errCodes[6] = "ERR_NO_CONNECTION: No connection with trade server.";
	errCodes[7] = "ERR_NOT_ENOUGH_RIGHTS: Not enough rights.";
	errCodes[8] = "ERR_TOO_FREQUENT_REQUESTS: Too frequent requests.";
	errCodes[9] = "ERR_MALFUNCTIONAL_TRADE: Malfunctional trade operation.";
	errCodes[64] = "ERR_ACCOUNT_DISABLED: Account disabled.";
	errCodes[65] = "ERR_INVALID_ACCOUNT: Invalid account.";
	errCodes[128] = "ERR_TRADE_TIMEOUT: Trade timeout.";
	errCodes[129] = "ERR_INVALID_PRICE: Invalid price.";
	errCodes[130] = "ERR_INVALID_STOPS: Invalid stops.";
	errCodes[131] = "ERR_INVALID_TRADE_VOLUME: Invalid trade volume.";
	errCodes[132] = "ERR_MARKET_CLOSED: Market is closed.";
	errCodes[133] = "ERR_TRADE_DISABLED: Trade is disabled.";
	errCodes[134] = "ERR_NOT_ENOUGH_MONEY: Not enough money.";
	errCodes[135] = "ERR_PRICE_CHANGED: Price changed.";
	errCodes[136] = "ERR_OFF_QUOTES: Off quotes.";
	errCodes[137] = "ERR_BROKER_BUSY: Broker is busy.";
	errCodes[138] = "ERR_REQUOTE: Requote.";
	errCodes[139] = "ERR_ORDER_LOCKED: Order is locked.";
	errCodes[140] = "ERR_LONG_POSITIONS_ONLY_ALLOWED: Long positions only allowed.";
	errCodes[141] = "ERR_TOO_MANY_REQUESTS: Too many requests.";
	errCodes[145] = "ERR_TRADE_MODIFY_DENIED: Modification denied because order too close to market.";
	errCodes[146] = "ERR_TRADE_CONTEXT_BUSY: Trade context is busy.";
	errCodes[147] = "ERR_TRADE_EXPIRATION_DENIED: Expirations are denied by broker.";
	errCodes[148] = "ERR_TRADE_TOO_MANY_ORDERS: The amount of open and pending orders has reached the limit set by the broker.";
	errCodes[149] = "ERR_TRADE_HEDGE_PROHIBITED: An attempt to open a position opposite to the existing one when hedging is disabled.";
	errCodes[150] = "ERR_TRADE_PROHIBITED_BY_FIFO: An attempt to close a position contravening the FIFO rule.";
	errCodes[4000] = "ERR_NO_MQLERROR: No error.";
	errCodes[4001] = "ERR_WRONG_FUNCTION_POINTER: Wrong function pointer.";
	errCodes[4002] = "ERR_ARRAY_INDEX_OUT_OF_RANGE: Array index is out of range.";
	errCodes[4003] = "ERR_NO_MEMORY_FOR_CALL_STACK: No memory for function call stack.";
	errCodes[4004] = "ERR_RECURSIVE_STACK_OVERFLOW: Recursive stack overflow.";
	errCodes[4005] = "ERR_NOT_ENOUGH_STACK_FOR_PARAM: Not enough stack for parameter.";
	errCodes[4006] = "ERR_NO_MEMORY_FOR_PARAM_STRING: No memory for parameter string.";
	errCodes[4007] = "ERR_NO_MEMORY_FOR_TEMP_STRING: No memory for temp string.";
	errCodes[4008] = "ERR_NOT_INITIALIZED_STRING: Not initialized string.";
	errCodes[4009] = "ERR_NOT_INITIALIZED_ARRAYSTRING: Not initialized string in array.";
	errCodes[4010] = "ERR_NO_MEMORY_FOR_ARRAYSTRING: No memory for array string.";
	errCodes[4011] = "ERR_TOO_LONG_STRING: Too long string.";
	errCodes[4012] = "ERR_REMAINDER_FROM_ZERO_DIVIDE: Remainder from zero divide.";
	errCodes[4013] = "ERR_ZERO_DIVIDE: Zero divide.";
	errCodes[4014] = "ERR_UNKNOWN_COMMAND: Unknown command.";
	errCodes[4015] = "ERR_WRONG_JUMP: Wrong jump (never generated error).";
	errCodes[4016] = "ERR_NOT_INITIALIZED_ARRAY: Not initialized array.";
	errCodes[4017] = "ERR_DLL_CALLS_NOT_ALLOWED: DLL calls are not allowed.";
	errCodes[4018] = "ERR_CANNOT_LOAD_LIBRARY: Cannot load library.";
	errCodes[4019] = "ERR_CANNOT_CALL_FUNCTION: Cannot call function.";
	errCodes[4020] = "ERR_EXTERNAL_CALLS_NOT_ALLOWED: Expert function calls are not allowed.";
	errCodes[4021] = "ERR_NO_MEMORY_FOR_RETURNED_STR: Not enough memory for temp string returned from function.";
	errCodes[4022] = "ERR_SYSTEM_BUSY: System is busy (never generated error).";
	errCodes[4050] = "ERR_INVALID_FUNCTION_PARAMSCNT: Invalid function parameters count.";
	errCodes[4051] = "ERR_INVALID_FUNCTION_PARAMVALUE: Invalid function parameter value.";
	errCodes[4052] = "ERR_STRING_FUNCTION_INTERNAL: String function internal error.";
	errCodes[4053] = "ERR_SOME_ARRAY_ERROR: Some array error.";
	errCodes[4054] = "ERR_INCORRECT_SERIESARRAY_USING: Incorrect series array using.";
	errCodes[4055] = "ERR_CUSTOM_INDICATOR_ERROR: Custom indicator error.";
	errCodes[4056] = "ERR_INCOMPATIBLE_ARRAYS: Arrays are incompatible.";
	errCodes[4057] = "ERR_GLOBAL_VARIABLES_PROCESSING: Global variables processing error.";
	errCodes[4058] = "ERR_GLOBAL_VARIABLE_NOT_FOUND: Global variable not found.";
	errCodes[4059] = "ERR_FUNC_NOT_ALLOWED_IN_TESTING: Function is not allowed in testing mode.";
	errCodes[4060] = "ERR_FUNCTION_NOT_CONFIRMED: Function is not confirmed.";
	errCodes[4061] = "ERR_SEND_MAIL_ERROR: Send mail error.";
	errCodes[4062] = "ERR_STRING_PARAMETER_EXPECTED: String parameter expected.";
	errCodes[4063] = "ERR_INTEGER_PARAMETER_EXPECTED: Integer parameter expected.";
	errCodes[4064] = "ERR_DOUBLE_PARAMETER_EXPECTED: Double parameter expected.";
	errCodes[4065] = "ERR_ARRAY_AS_PARAMETER_EXPECTED: Array as parameter expected.";
	errCodes[4066] = "ERR_HISTORY_WILL_UPDATED: Requested history data in updating state.";
	errCodes[4067] = "ERR_TRADE_ERROR: Some error in trading function.";
	errCodes[4099] = "ERR_END_OF_FILE: End of file.";
	errCodes[4100] = "ERR_SOME_FILE_ERROR: Some file error.";
	errCodes[4101] = "ERR_WRONG_FILE_NAME: Wrong file name.";
	errCodes[4102] = "ERR_TOO_MANY_OPENED_FILES: Too many opened files.";
	errCodes[4103] = "ERR_CANNOT_OPEN_FILE: Cannot open file.";
	errCodes[4104] = "ERR_INCOMPATIBLE_FILEACCESS: Incompatible access to a file.";
	errCodes[4105] = "ERR_NO_ORDER_SELECTED: No order selected.";
	errCodes[4106] = "ERR_UNKNOWN_SYMBOL: Unknown symbol.";
	errCodes[4107] = "ERR_INVALID_PRICE_PARAM: Invalid price.";
	errCodes[4108] = "ERR_INVALID_TICKET: Invalid ticket.";
	errCodes[4109] = "ERR_TRADE_NOT_ALLOWED: Trade is not allowed. Enable checkbox 'Allow live trading' in the expert properties.";
	errCodes[4110] = "ERR_LONGS_NOT_ALLOWED: Longs are not allowed. Check the expert properties.";
	errCodes[4111] = "ERR_SHORTS_NOT_ALLOWED: Shorts are not allowed. Check the expert properties.";
	errCodes[4200] = "ERR_OBJECT_ALREADY_EXISTS: Object exists already.";
	errCodes[4201] = "ERR_UNKNOWN_OBJECT_PROPERTY: Unknown object property.";
	errCodes[4202] = "ERR_OBJECT_DOES_NOT_EXIST: Object does not exist.";
	errCodes[4203] = "ERR_UNKNOWN_OBJECT_TYPE: Unknown object type.";
	errCodes[4204] = "ERR_NO_OBJECT_NAME: No object name.";
	errCodes[4205] = "ERR_OBJECT_COORDINATES_ERROR: Object coordinates error.";
	errCodes[4206] = "ERR_NO_SPECIFIED_SUBWINDOW: No specified subwindow.";
	errCodes[4207] = "ERR_SOME_OBJECT_ERROR: Some error in object function.";
};

#define MT_OP_BUY	        0         /*	Buying position. */
#define MT_OP_SELL	        1         /*	Selling position. */
#define MT_OP_BUYLIMIT	    2         /*	Buy limit pending position. */
#define MT_OP_SELLLIMIT     3         /*	Sell limit pending position. */
#define MT_OP_BUYSTOP	    4         /*	Buy stop pending position. */
#define MT_OP_SELLSTOP	    5         /*	Sell stop pending position. */

int metatraderCmdFromFixOrderTypeAndSide(char *fixtype, char *fixside) {
	if (fixtype[0] == '1') {
		if (fixside[0] == '1') return MT_OP_BUY;
		if (fixside[0] == '2') return MT_OP_SELL;
		if (fixside[0] == '5') return MT_OP_SELL;
		if (fixside[0] == '6') return MT_OP_SELL;
	} else if (fixtype[0] == '2') {
		if (fixside[0] == '1') return MT_OP_BUYLIMIT;
		if (fixside[0] == '2') return MT_OP_SELLLIMIT;
		if (fixside[0] == '5') return MT_OP_SELLLIMIT;
		if (fixside[0] == '6') return MT_OP_SELLLIMIT;
	}
}

int fixSideFromMetatraderCmd(int cmd) {
	switch(cmd) {
	case MT_OP_BUY: return(1);
	case MT_OP_BUYLIMIT: return(1);
	case MT_OP_SELL: return(2);
	case MT_OP_SELLLIMIT: return(2);
	default: return('0');
	}
}

static void fixSymbolCopy(char *dst, char *symbol) {
	char *d, *s;
	for(d=dst,s=symbol; *s!=0 && *s!='\001'; s++, d++) *d = *s;
	*d = 0;
}

static int fixSymbolLen(char *symbol) {
	char *s;
	for(s=symbol; *s!=0 && *s!='\001'; s++) ;
	return(s - symbol);
}

static int fixSymComparator(unsigned char *s1, unsigned char *s2) {
	while (*s1!=0 && *s1!='\001' && *s2!=0 && *s2!='\001') {
		if (*s1 != *s2) return(*s1 - *s2);
		s1 ++;
		s2 ++;
	}
	if (*s1==0 || *s1=='\001') {
		if (*s2==0 || *s2=='\001') return(0);
		return(-1);
	} else {
		if (*s2==0 || *s2=='\001') return(1);
		assert(0);
	}
}

unsigned fixHashFun(char *s) {
	int       len;
	unsigned  res;
	len = fixSymbolLen(s);
	SGLIB_HASH_FUN(s, len, res);
	return(res % SYM_HASH_SIZE);
}

int getSymbolHash(char *cp) {
	int       i, len;
	char      *s, *ss;

	SGLIB_HASH_TAB_FIND_MEMBER(char, symTable, SYM_HASH_SIZE, cp, fixHashFun, fixSymComparator, i, s);
	if (s == NULL) {
		len = fixSymbolLen(cp);
		ALLOCC(ss, len+1, char);
		fixSymbolCopy(ss, cp);
		SGLIB_HASH_TAB_ADD_IF_NOT_MEMBER(char, symTable, SYM_HASH_SIZE, ss, fixHashFun, fixSymComparator, i, s);
	}
	return(i);
}

char *getMt4ErrorMessage(int lastError) {
	if (lastError < 0 || lastError >= MAX_MT4_ERR_CODE) return("Error code out of range.");
	if (errCodes[lastError] == NULL) return("Unknown error.");
	return(errCodes[lastError]);
}

//////////////////////////////////////////////////////////////////////////////////////


int fixSendNews(char *header, char *message);

#define NEWS_BUFFER_SIZE 10000

static void fixFormatAndSendNews(char *hdr, char *fmt, va_list arg_ptr) {
	char		bb[NEWS_BUFFER_SIZE];
	int			n;

	n = vsprintf(bb, fmt, arg_ptr);

	if (n >= NEWS_BUFFER_SIZE-1) {
		fixSendNews("ERROR: ", "News line longer than NEWS_BUFFER_SIZE, may be fatal for other variables");
		return;
	}
	fixSendNews(hdr, bb);
}

void debugPrintf(char *fmt, ...) {
	va_list 	arg_ptr;
	va_start(arg_ptr, fmt);
	fixFormatAndSendNews("", fmt, arg_ptr);
}

void errorPrintf(char *fmt, ...) {
	va_list 	arg_ptr;
	va_start(arg_ptr, fmt);
	fixFormatAndSendNews("ERROR:", fmt, arg_ptr);
}

void resetConnection() {
	if (clientsockfd >= 0) {
		close(clientsockfd);
		clientsockfd = -1;
	}
	fxgState = STATE_INITIAL;
}

void openListeningSocket() {
	static int 			sockfd = -1;
	int		 			clilen;
	struct sockaddr_in 	serv_addr, cli_addr;
	int		    		one = 1;

	fxgState = STATE_WAITING_FOR_CLIENT;

	if (sockfd < 0) {
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			//errorPrintf("ERROR opening socket");
			goto failfin;
		}

		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof (one)) ;
		memset((char *) &serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(serverPort);
		if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
			//errorPrintf("ERROR on binding");
			close(sockfd); sockfd = -1;
			goto failfin;
		}
		listen(sockfd, 5);
	}

	clilen = sizeof(cli_addr);
	clientsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
	if (clientsockfd < 0) {
		//errorPrintf("ERROR on accept");
		// close(sockfd); sockfd = -1;
		goto failfin;
	}

	if (latencyMode < 2) {
		setsockopt(clientsockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
		// setsockopt(clientsockfd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
	}

	fxgState = STATE_CLIENT_CONNECTED;

	// close(sockfd); sockfd = -1;

	return;

failfin:
	resetConnection();
}

////////////////////////////////////////////////////////////////////////////////////
// Fix sending
////////////////////////////////////////////////////////////////////////////////////

static int fixFieldLen(char *buffer) {
    char *p;
	if (buffer == NULL) {
		return(0);
	}
    for(p=buffer; *p != '\001'; p++) ;
	return(p-buffer);
}

int fixSendToSocket(char *buffer, int len) {
	int n, r;

	EnterCriticalSection(&socketCriticalSection);

	for(n=0; n<len; n+=r) {
		r = send(clientsockfd, buffer+n, len-n, 0);
		if (r < 0) {
			resetConnection();
			break;
		}
	}

	LeaveCriticalSection(&socketCriticalSection);
	return(n);
}

static int fixCheckSum(char *buffer, int len) {
    unsigned 		i, res;
	unsigned char	*b;
	b = (unsigned char *)buffer;
    res = 0;
    for(i=0; i<len; i++) res += b[i];
    return(res & 0xff);
}

static int fixCompleteMessage(char *body, int bodylen, char **fixSendMessage) {
	int 	lenlog, messageLen, checksumableBodyLen, footerLen, r, headerLen;
	char	*messageStart;

	lenlog = 3;
	if (bodylen < 100) lenlog = 2;

	headerLen = sizeof(FIX_PREFIX_STRING_FORMAT) - 2 + lenlog;
	messageStart = body - headerLen;
	*fixSendMessage = messageStart;

	r = sprintf(messageStart,
				FIX_PREFIX_STRING_FORMAT
				,
				bodylen
		);

	body[-1] = '\001';
	assert(headerLen == r+1);

	checksumableBodyLen = headerLen + bodylen;

    footerLen = sprintf(messageStart+checksumableBodyLen,
						"10=%03d" "\001"
						,
						fixCheckSum(messageStart, checksumableBodyLen)
        );
    messageLen = checksumableBodyLen + footerLen;

	if (FIX_BODY_START_OFFSET + bodylen + footerLen >= FIX_MAX_SEND_MESSAGE_SIZE-1) {
		errorPrintf("Fix Message longer than FIX_MAX_SEND_MESSAGE_SIZE, fatal, increase the constant !!!!\n");
		return(0);
	}

	return(messageLen);
}

static int fixCompleteAndSendMessage(char *body, int bodylen) {
	int 	messageLen, res;
	char 	*fixSendMessage;

	messageLen = fixCompleteMessage(body, bodylen, &fixSendMessage);
	if (messageLen > 0) {
		res = fixSendToSocket(fixSendMessage, messageLen);
	} else {
		res = 0;
	}
	return(res);
}

////////////////

static struct tm *setTmToCurrentTime(struct tm *tm) {
	time_t		t0;
	t0 = time(NULL);
    memcpy(tm, gmtime(&t0), sizeof(*tm));
	return(tm);
}

int fixSendMarketDataIncrementalRefreshSingleValue(char *symbol, int bidAsk, double price, int size) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=X"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "268=1" "\001"
						  "279=0" "\001"
						  "55=%s" "\001"
						  "269=%d" "\001"
						  "270=%3.6f" "\001"
						  "271=%d" "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  symbol,
					  bidAsk,
					  price,
					  size
        );

    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);

	return(r);
}

int fixSendMarketDataIncrementalRefreshBidAndAsk(char *symbol, double bidPrice, int bidSize, 
												 double askPrice, int askSize) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=X"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "268=2" "\001"
						  "279=0" "\001"
						  "55=%s" "\001"
						  "269=0" "\001"
						  "270=%3.6f" "\001"
						  "271=%d" "\001"
						  "279=0" "\001"
						  "55=%s" "\001"
						  "269=1" "\001"
						  "270=%3.6f" "\001"
						  "271=%d" "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  symbol,
					  bidPrice,
					  bidSize,
					  symbol,
					  askPrice,
					  askSize
        );

    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);

	return(r);
}

int fixSendNews(char *header, char *message) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=B"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "148=%s" "\001"
						  "58=%s" "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  header,
					  message
        );

    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);

	return(r);
}


int fixSendHeartBeat(char *fixMsg) {
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
    int          bodylen, mlen, n, r;
    struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	if (fixMsg == NULL) {
		bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb), 
						  "35=0"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  ""
						  //"108=%d" "\001"
						  ,
						  FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum)
						  //,fixHeartBeatInterval
			);
	} else {
		n = fixFieldLen(fixMsg);
		bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=0"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  // "108=%d" "\001"
						  "112=%*.*s" "\001"
						  ,
						  FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
						  // fixHeartBeatInterval,
						  n,n,fixMsg
			);
		// printf("Not yet implemented\n");
	}
    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);
	return(r);
}

int fixSendLogin() {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=A"  "\001"
						  FIX_COMMON_HEADER_FORMAT
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum)
        );

    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);

	return(r);
}


int fixSendTradingSessionStatus() {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=h"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "340=2"  "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum)
        );

    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);

	return(r);
}

int fixSendOrderAccepted(char *sym, int ivals[], double dvals[]) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=8"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "150=0"  "\001"
						  "44=%f"  "\001"
						  "55=%s"  "\001"
						  "11=%d"  "\001"
						  "151=%d"  "\001"
						  "54=%d"  "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  dvals[DV_PRICE],
					  sym,
					  ivals[IV_MAGIC],
					  ivals[IV_VOLUME],
					  fixSideFromMetatraderCmd(ivals[IV_CMD])
        );
    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);
	return(r);
}

int fixSendOrderReplaced(char *sym, int ivals[], double dvals[], struct order *ii) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=8"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "150=5"  "\001"
						  "44=%f"  "\001"
						  "55=%s"  "\001"
						  "11=%d"  "\001"
						  "41=%d"  "\001"
						  "151=%d"  "\001"
						  "54=%d"  "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  dvals[DV_PRICE],
					  sym,
					  ivals[IV_MAGIC],
					  ivals[IV_MAGIC2],
					  ii->volume,
					  fixSideFromMetatraderCmd(ii->cmd)
        );
    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);
	return(r);
}

int fixSendOrderCancelled(char *sym, int ivals[], double dvals[]) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=8"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "150=4"  "\001"
						  "44=%f"  "\001"
						  "55=%s"  "\001"
						  "11=%d"  "\001"
						  "41=%d"  "\001"
						  "38=%d"  "\001"
						  "54=%d"  "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  dvals[DV_PRICE],
					  sym,
					  ivals[IV_MAGIC],
					  ivals[IV_MAGIC2],
					  ivals[IV_VOLUME],
					  fixSideFromMetatraderCmd(ivals[IV_CMD])
        );
    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);
	return(r);
}

int fixSendOrderCancelReject(int orderNum, int origOrderNum) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=9"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "11=%d"  "\001"
						  "41=%d"  "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  orderNum,
					  origOrderNum
        );
    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);
	return(r);
}

int fixSendOrderCanceled(char *sym, int ivals[], double dvals[]) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=8"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "150=C"  "\001"
						  "55=%s"  "\001"
						  "11=%d"  "\001"
						  "54=%d"  "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  sym,
					  ivals[IV_MAGIC],
					  fixSideFromMetatraderCmd(ivals[IV_CMD])
        );
    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);
	return(r);
}


int fixSendExecutionReport(char *sym, int ivals[], double dvals[], double price, double commision) {
	int 		r, bodylen;
	char		bb[FIX_MAX_SEND_MESSAGE_SIZE];
	struct tm   *tm, ttm;

	tm = setTmToCurrentTime(&ttm);
	bodylen = sprintf(FIX_SEND_BODY_BUFFER(bb),
						  "35=8"  "\001"
						  FIX_COMMON_HEADER_FORMAT
						  "150=F"  "\001"
					      "31=%f"  "\001"
						  "55=%s"  "\001"
						  "11=%d"  "\001"
					      "12=%1.3f"  "\001"
					      "14=%d"  "\001"
						  "32=%d"  "\001"
						  "54=%d"  "\001"
                      ,
				      FIX_COMMON_HEADER_VALUES(fixSendCurrentSeqNum),
					  price,
					  sym,
					  ivals[IV_MAGIC],
					  -commision,             // seems in MT4 commisions are negative
					  ivals[IV_VOLUME],
					  ivals[IV_VOLUME],
					  fixSideFromMetatraderCmd(ivals[IV_CMD])
        );
    r = fixCompleteAndSendMessage(FIX_SEND_BODY_BUFFER(bb), bodylen);
	return(r);
}


////////////////////////////////////////////////////////////////////////////////////
// Fix parsing
////////////////////////////////////////////////////////////////////////////////////
#define FIX_ID_MAX_FIELD_ID                 1000
#define FIX_MAX_FIELDS_IN_ONE_LINE          100     /* how many fields of form nn=xxx can be in one line */

char *fixFields[FIX_ID_MAX_FIELD_ID];
int	fixUsedFields[FIX_MAX_FIELDS_IN_ONE_LINE];
int fixUsedFieldsIndex=0;

static char *fixEndOfField(char *buff, char *buffEnd) {
	char *p;
	for(p=buff; *p!='\001' && p<buffEnd; p++) ;
	if (p < buffEnd) return(p);
	// I can return anything here, it is an error
	return(buffEnd);
}

static int fixStrCpy(char *dst, char *src) {
    char *d, *p;
	if (dst == NULL || src == NULL) {
		return(0);
	}
    for(d=dst,p=src; *p != 0 && *p != '\001'; p++,d++) *d = *p;
	*d = 0;
	return(p-src);
}

static int fixParseInt(char *pp) {
    int 	res;
	char	*p;
	p = pp;
	if (p == NULL) return(-1);
    res = 0;
    // parse number until dot
    while (isdigit(*p)) res = res * 10 + *p++ - '0';
	// allow a dot to terminate number as it may be real number ....
    if (*p != '\001' && *p != '.') {
        errorPrintf("Fix Error: while parsing int: "); // printField(pp); printf("\n");
        return(-1);     
    }
    return(res);
}

static double fixParsePrice(char *p) {
    long        res;
    int         i,n;
	int			sign;

	if (p == NULL) return(-1);

	sign = 1;
	if (*p == '-') {
		sign = -1; p++;
	} else if (*p == '+') {
		sign = 1; p++;
	}

    res = 0;
    // parse number until dot
    while (isdigit(*p)) res = res * 10 + *p++ - '0';
    if (*p == '.') p++;

    n = PRICE_DECIMALS;
    for(i=0; i<n; i++) {
        res = res * 10;
        if (isdigit(*p)) res += *p++ - '0';
    }
    while (isdigit(*p)) p++;
    if (*p != '\001') {
        errorPrintf("Fix Error: while parsing price: "); // printField(p); printf("\n");
        return(-1);
    }

	res = res * sign;

    return (((double)res) / PRICE_FACTOR);
}

static double fixParseDouble(char *p) {
	double d;
	sscanf(p, "%lf", &d);
	return(d);
}

static struct order *oppositeOpenedOrder(int sid, int cmd, int volume) {
	struct order *oo;
	// debugPrintf("looking for opened order sid==%d, cmd == %d, volume == %d", sid, cmd, volume);
	for(oo=exorders[sid]; oo != NULL; oo=oo->next) {
		// debugPrintf("check against volume == %d, cmd == %d", oo->volume, oo->cmd);
		if (oo->volume == volume && oo->cmd != cmd) return(oo);
	}
	// debugPrintf("not found");
	return(NULL);
}

int fixProcessParsedLine(char *buffer, int len, char *symbol, int ivals[], double dvals[]) {
	int         n, res, on;
	struct order *ii, iii, *oo;

	// printf("processing line with tag %s\n", fixFields[35]);
	res = FN_CONTINUE;
	if (fixFields[35] == NULL) {
		errorPrintf("%s: Fix Error: No field type in a fix message!", sprintCTime_st()); // fflush(stdout);
		goto fini;
	}
	switch (*fixFields[35]) {
	case '0':	// heartbeat
		// reply with heartbeat too
		// mergePositions();
		fixSendHeartBeat(NULL);
		res = FN_HEARTBEAT;
		break;
	case '1':   // test request
		// if there is a special test request informing about quote timeot, 
		// exit the application. This shall cause that the application is restarted by
		// the watching script
		if (fixSymComparator(fixFields[112], "QUOTE_TIMEOUT") == 0) {
			// Hope, this will terminate calling mt4 station too
			TerminateProcess(GetCurrentProcess(),0);
		}
		fixSendHeartBeat(fixFields[112]);
		res = FN_CONTINUE;
		break;
	case '5':
		// logout
		// dukasMergePositions();
		res = FN_CONTINUE;
		break;
	case 'A':
		// login request
		fixSendCurrentSeqNum = 1;
		fixStrCpy(fixTargetCompID, fixFields[49]);
		debugPrintf("LOGIN AS '%s'", fixTargetCompID);
		fixSendLogin();
		fixSendTradingSessionStatus();
		res = FN_CONTINUE;
		break;
	case 'D':
		//ticket = OrderSend(symbol, ivals[IV_CMD], ivals[IV_VOLUME], dvals[DV_PRICE], ivals[IV_SLIPPAGE], 0, 0, NULL, ivals[IV_MAGIC]);
		fixSymbolCopy(symbol, fixFields[55]);
		ivals[IV_CMD] = metatraderCmdFromFixOrderTypeAndSide(fixFields[40], fixFields[54]);
		ivals[IV_VOLUME] = fixParseInt(fixFields[38]);
		if (fixFields[44] != NULL) {
			dvals[DV_PRICE] = fixParsePrice(fixFields[44]);
		} else {
			dvals[DV_PRICE] = 0.0;
		}
		ivals[IV_SLIPPAGE] = 0.0;
		ivals[IV_MAGIC] = fixParseInt(fixFields[11]);
		ivals[IV_SYM_HASH] = getSymbolHash(fixFields[55]);
		res = FN_ORDER_SEND;
		// if we have an opposite open order, just close it
		if (ivals[IV_CMD] == MT_OP_BUY || ivals[IV_CMD] == MT_OP_SELL) {
			oo = oppositeOpenedOrder(ivals[IV_SYM_HASH], ivals[IV_CMD], ivals[IV_VOLUME]);
			if (oo != NULL) {
				ivals[IV_TICKET] = oo->ticket;
				ivals[IV_MAGIC2] = oo->mbtn;
				res = FN_ORDER_CLOSE;
			}
		}
		break;
	case 'F':
		//	b = OrderDelete(ivals[IV_TICKET]);
		on = fixParseInt(fixFields[41]);
		iii.mbtn = on;
		ii = sglib_hashed_order_find_member(orders, &iii);
		if (ii != NULL) {
			ivals[IV_TICKET] = ii->ticket;
			ivals[IV_MAGIC] = fixParseInt(fixFields[11]);
			ivals[IV_MAGIC2] = on;
			res = FN_ORDER_DELETE;
		} else {
			errorPrintf("fix requests to delete unknown order %d", on);
		}
		break;

    case 'G':
		//	b = OrderModify(ivals[IV_TICKET], dvals[DV_PRICE], 0, 0, 0);
		//res = FN_ORDER_MODIFY;
		on = fixParseInt(fixFields[41]);
		iii.mbtn = on;
		ii = sglib_hashed_order_find_member(orders, &iii);
		if (ii != NULL) {
			ivals[IV_TICKET] = ii->ticket;
			ivals[IV_MAGIC] = fixParseInt(fixFields[11]);
			ivals[IV_MAGIC2] = on;
			dvals[DV_PRICE] = fixParsePrice(fixFields[44]);
			res = FN_ORDER_MODIFY;
		} else {
			errorPrintf("fix requests to modify unknown order %d", on);
		}
		break;

	default:
		errorPrintf("Fix Unexpected message received: ", "%*.*d", len, len, buffer);
		res = FN_CONTINUE;
		break;
	}
fini:
	return(res);
}

#define FIX_SCAN_INT(res, p, errMessage) {		\
		res = 0;								\
		while (isdigit(*p)) {					\
			res = res * 10 + *p - '0';			\
			p++;								\
		}										\
		if (*p != '\001' && *p != '=') {		\
			errorPrintf(errMessage);					\
			goto skipLineOnProblem;				\
		}										\
		p++;									\
	}

int fixParseLineIfWholeAvailableInBuffer(char *buffer, char *bufferEnd, int *error) {
    char   *p, *msgEnd;
    int     i, n, r, msgLen;
    int     seq, fieldId;

	*error = 0;

    p = buffer;

	// clear old fix fields first
	for(i=0; i<fixUsedFieldsIndex; i++) fixFields[fixUsedFields[i]] = NULL;
	fixUsedFieldsIndex = 0;

    // no way to have such small line
    if (bufferEnd - buffer < FIX_MIN_MESSAGE_LEN) return(0);

	
	// check 8 TAG
	if (p[0] != '8' || p[1] != '=') {
		errorPrintf("Fix ERROR: message does not start with TAG 8\n");
		goto skipLineOnProblem;
	}
	p += 2;
	p = fixEndOfField(p, bufferEnd) + 1;

	// check 9 TAG (len)
	if (p[0] != '9' || p[1] != '=') {
		errorPrintf("Fix ERROR: second field is not TAG 9\n");
		goto skipLineOnProblem;
	}
	p += 2;

	FIX_SCAN_INT(msgLen, p, "Fix ERROR: unexpected character in TAG 9\n");
	msgEnd = p + msgLen;

	// if the message is not read entirely, do nothing
	if (msgEnd + 7 > bufferEnd) return(0);

    // parse items
    for(; p<msgEnd; p++) {

		FIX_SCAN_INT(fieldId, p, "Fix ERROR: unexpected character in field Id\n");

        if (fieldId >= 0 && fieldId < FIX_ID_MAX_FIELD_ID) {
			fixFields[fieldId] = p;
			if (fixUsedFieldsIndex < FIX_MAX_FIELDS_IN_ONE_LINE) {
				fixUsedFields[fixUsedFieldsIndex] = fieldId;
				fixUsedFieldsIndex ++;
			} else {
				errorPrintf("Fix Error: number of fix fields overflowed over %d, increase the constant\n", FIX_MAX_FIELDS_IN_ONE_LINE);
				goto skipLineOnProblem;
			}
		} else {
            errorPrintf("Fix Error: fieldId %d out of born 0 - %d\n", fieldId, FIX_ID_MAX_FIELD_ID);
        }

        // skip field text / body
        while (p < msgEnd && *p != '\001') p ++;
    }

	// scan 10 == xxx
	if (p + 7 > bufferEnd || strncmp(p, "10=", 3) != 0 || p[6] != '\001') {
		errorPrintf("Fix Error: missing or wrong checksum: ");
		goto skipLineOnProblem;
	}
	p += 7;

	// Hmm. I shall not update this during answer to resend request, but for the moment it is ok
	// if ((seq = fixParseInt(fixFields[34])) != -1) fx->fixReadCurrentSeqNum = seq;
    return(p-buffer);

skipLineOnProblem:
    // errorPrintf("Fix: Error");
    while (p < bufferEnd-sizeof(FIX_RECOVERY_STRING-1) && strncmp(p, FIX_RECOVERY_STRING, sizeof(FIX_RECOVERY_STRING)-1) != 0) p ++;
    // if (p >= bufferEnd-sizeof(FIX_RECOVERY_STRING)-1) return(0);
    p ++;
	*error = 1;

	// Hmm. Maybe I shall record sequence number as readed even in case of error, so that we do not return to this problem
	// if ((seq = fixParseInt(fieldId[34])) != -1) fixReadCurrentSeqNum = seq;
    return(p - buffer);
}


////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
int clientConnected() {
	int i;
	// TODO: do this better handling possible concurrency by atomic instructions
	if (fxgState == STATE_INITIAL) {
		openListeningSocket();
		// debugPrintf("clientsocket == %d", clientsockfd);
		readBufferInit(&readbuff, READ_BUFFER_SIZE, READ_BUFFER_MIN_SPACE_FOR_READ, clientsockfd);
		sglib_hashed_order_init(orders);
	}

	if (fxgState == STATE_CLIENT_CONNECTED) return(1);
	return(0);
}

void clientDisconnected() {
	resetConnection();
	// TODO:
	// free readbuffer
	// free hashtables
}


////////////////////////////////////////////////////////////////////////////////////

static void orderDeleteFromExOrders(int sid, int mbtnid) {
	struct order *ii, iii;

	assert(sid>=0 && sid < SYM_HASH_SIZE);
	iii.mbtn = mbtnid;
	SGLIB_LIST_DELETE_IF_MEMBER(struct order, exorders[sid], &iii, orderComparator, next, ii);
	if (ii != NULL) {
		FREE(ii);
	} else {
		errorPrintf("Close by order not found");
	}
}

static void orderExecution(char *sym, int ivals[], double dvals[], double openPrice, double commision, struct order *oo) {
	int    sid;
	char   ss[TMP_STRING_SIZE];

	fixSendExecutionReport(sym, ivals, dvals, openPrice, commision);
	sid = ivals[IV_SYM_HASH];
	//debugPrintf("Inserting order to exorders[%d]: volume == %d , cmd == %d", sid, oo->volume, oo->cmd);
	oo->next = exorders[sid];
	exorders[sid] = oo;
}

static int ordersOrderingBySymbolVolumeAndCmd(struct order *o1, struct order *o2) {
	if (o1->volume < o2->volume) return(-1);
	if (o1->volume > o2->volume) return(1);
	if (o1->cmd < o2->cmd) return(-1);
	if (o1->cmd > o2->cmd) return(1);
	return(1);
}

int checkForOrderMerging(char *sym, int ivals[], double dvals[]) {
	int          sid;
	struct order *oo;
	// hmm. for the moment do very simple closing by finding too oposite orders with the same volume
	// if no order there, do nothing
	sid = ivals[IV_CHECK_SYMBOL];
	// debugPrintf("checking for order merging of %d", sid);
	assert(sid>=0 && sid < SYM_HASH_SIZE);
	if (exorders[sid] == NULL || exorders[sid]->next == NULL) goto fininothing;
	SGLIB_LIST_SORT(struct order, exorders[sid], ordersOrderingBySymbolVolumeAndCmd, next);
	for(oo=exorders[sid]; oo->next != NULL; oo=oo->next) {
		if (oo->volume == oo->next->volume
			&& oo->cmd != oo->next->cmd) {
			// this shall be a candidate
			ivals[IV_TICKET] = oo->ticket;
			ivals[IV_TICKET2] = oo->next->ticket;
			ivals[IV_MAGIC] = oo->mbtn;
			ivals[IV_MAGIC2] = oo->next->mbtn;
			return(FN_ORDER_CLOSE_BY);
		}
	}
fininothing:
	return(FN_CONTINUE);
}

////////////////////////////////////////////////////////////////////////////////////
// exported functions
////////////////////////////////////////////////////////////////////////////////////


void DLL_SPEC fixGatewayOnTick(char *symbol, double bid, double ask, int bidsize, int asksize) {
	if (! clientConnected()) return;
	// ok we have a client connected
	fixSendMarketDataIncrementalRefreshBidAndAsk(symbol, bid, bidsize, ask, asksize);
}

static int selectOnClientSocket() {
	int             r;
	fd_set          fdset;
    struct timeval 	sleepTime;

	FD_ZERO(&fdset);
	FD_SET(clientsockfd, &fdset);
	sleepTime.tv_sec = 0;
	sleepTime.tv_usec = 0;
	r = select(clientsockfd + 1, &fdset, NULL, NULL, &sleepTime);
	return(r);
}


void DLL_SPEC fixGatewayInitialPositionNote(char *sym, int ivals[], double dvals[], int ticket, double openPrice) {
	struct order *ii;
	int    sid;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);
	// debugPrintf("fixGatewayInitialPositionNote(%s, , , %d, %f);", sym, ticket, openPrice);
	if (ticket >= 0) {
		sid = getSymbolHash(sym);
		ALLOCC(ii, 1, struct order);
		ii->symid = sid;
		ii->mbtn = ivals[IV_MAGIC];
		ii->ticket = ticket;
		ii->cmd = ivals[IV_CMD];
		ii->volume = ivals[IV_VOLUME];
		ii->price = openPrice;
		ii->next = NULL;

		// debugPrintf("Inserting order to exorders[%d]: volume == %d , cmd == %d", sid, ii->volume, ii->cmd);
		ii->next = exorders[sid];
		exorders[sid] = ii;

	}
	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);
}


// 'callback' functions passing result of metatrader trade functions to this dll.
// Original parameters from 'PoolOrder' are passes as well, so that the dll
// knows to which call the callback function was called
void DLL_SPEC fixGatewayOnOrderSendResult(char *sym, int ivals[], double dvals[], int ticket, double openPrice, double commision, int lastError) {
	struct order *ii;
	if (! clientConnected()) return;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);
	// debugPrintf("fixGatewayOnOrderSendResult(%s, , , %d, %f);", sym, ticket, openPrice);
	if (ticket >= 0) {
		fixSendOrderAccepted(sym, ivals, dvals);
		// memorize it and check it later
		ALLOCC(ii, 1, struct order);
		ii->symid = ivals[IV_SYM_HASH];
		ii->mbtn = ivals[IV_MAGIC];
		ii->ticket = ticket;
		ii->cmd = ivals[IV_CMD];
		ii->volume = ivals[IV_VOLUME];
		ii->price = openPrice;
		ii->next = NULL;
		if (ivals[IV_CMD] == MT_OP_BUY || ivals[IV_CMD] == MT_OP_SELL) {
			// it was a market order, so it is executed now.
			orderExecution(sym, ivals, dvals, openPrice, commision, ii);
		} else {
			sglib_hashed_order_add(orders, ii);
		}
	} else if (lastError == 129 /* invalid price */ ) {
		/* invalid price, probably due to slippage control */
		// generate accept and cancel message, i.e. the order is lost
		fixSendOrderAccepted(sym, ivals, dvals);
		fixSendOrderCanceled(sym, ivals, dvals);
	} else {
		// for those error code generate reject message causing the order to be resubmited
		debugPrintf("OrderSend: Error %d: %s", lastError, getMt4ErrorMessage(lastError));
		fixSendOrderCancelReject(ivals[IV_MAGIC], ivals[IV_MAGIC]);
	}
	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);
}

void DLL_SPEC fixGatewayOnOrderCloseResult(char *sym, int ivals[], double dvals[], int b, double closePrice, int lastError) {
	struct order *ii, iii;
	if (! clientConnected()) return;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);
	if (b) {
		fixSendOrderAccepted(sym, ivals, dvals);
		fixSendExecutionReport(sym, ivals, dvals, closePrice, 0);
		orderDeleteFromExOrders(ivals[IV_SYM_HASH], ivals[IV_MAGIC2]);
	} else {
		fixSendOrderCancelReject(ivals[IV_MAGIC], ivals[IV_MAGIC]);
	}
	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);

}

void DLL_SPEC fixGatewayOnOrderModifyResult(char *sym, int ivals[], double dvals[], int b, int lastError) {
	struct order *ii, iii;
	if (! clientConnected()) return;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);
	iii.mbtn = ivals[IV_MAGIC2];
	ii = sglib_hashed_order_find_member(orders, &iii);
	if (b) {
		if (ii != NULL) {
			fixSendOrderReplaced(sym, ivals, dvals, ii);
			sglib_hashed_order_delete(orders, ii);
			ii->mbtn = ivals[IV_MAGIC];
			ii->price = dvals[DV_PRICE];
			sglib_hashed_order_add(orders, ii);
		} else {
			errorPrintf("an unknown order %d was replaced", ivals[IV_MAGIC2]);
		}
	} else {
		fixSendOrderCancelReject(ivals[IV_MAGIC], ivals[IV_MAGIC2]);
	}
	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);
}

void DLL_SPEC fixGatewayOnOrderDeleteResult(char *sym, int ivals[], double dvals[], int b, int lastError) {
	struct order *ii, iii;
	if (! clientConnected()) return;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);
	iii.mbtn = ivals[IV_MAGIC2];
	ii = sglib_hashed_order_find_member(orders, &iii);
	if (b) {
		if (ii != NULL) {
			sglib_hashed_order_delete(orders, ii);
			FREE(ii);
		}
		fixSendOrderCancelled(sym, ivals, dvals);
	} else {
		fixSendOrderCancelReject(ivals[IV_MAGIC], ivals[IV_MAGIC2]);
	}
	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);
}

void DLL_SPEC fixGatewayOnOrderCloseByResult(char *sym, int ivals[], double dvals[], int b, int lastError) {
	int          sid;
	if (! clientConnected()) return;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);
	if (b) {
		sid = ivals[IV_CHECK_SYMBOL];
		orderDeleteFromExOrders(sid, ivals[IV_MAGIC]);
		orderDeleteFromExOrders(sid, ivals[IV_MAGIC2]);
	} else {
		debugPrintf("Closeby: Error %d: %s", lastError, getMt4ErrorMessage(lastError));
	}
	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);
}

void DLL_SPEC fixGatewayOnCheckOrderResult(char *sym, int ivals[], double dvals[], int orderType, double openPrice, double commision) {
	struct order *ii, iii;
	if (! clientConnected()) return;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);
	if (orderType == MT_OP_BUY || orderType == MT_OP_SELL) {
		// a limit order was executed
		iii.mbtn = ivals[IV_MAGIC];
		ii = sglib_hashed_order_find_member(orders, &iii);
		if (ii != NULL) {
			sglib_hashed_order_delete(orders, ii);
			ii->cmd = orderType;
			orderExecution(sym, ivals, dvals, openPrice, commision, ii);
		} else {
			errorPrintf("fixGatewayOnCheckOrderResult: A checked and executed order was not found in order hashtable!");
		}
	}
	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);
}

void DLL_SPEC fixGatewayInitConf(int port, int latMode) {
	serverPort = port;
	latencyMode = latMode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
// The main function generating orders for metatrader
// All the arrays are 'inout' parameters containing parameters
/////////////////////////////////////////////////////////////////////////////////////////////////////

static int lastlyWasConnected = 0;

int DLL_SPEC fixGatewayPoolOrder(char *symbol, int ivals[], double dvals[]) {
	int           i, j, jmax, r, nn, err, res, fixReadedBytes;
	struct order  *oo;

	if (ORDER_CRITICAL_SECTION) EnterCriticalSection(&orderCriticalSection);

	if (clientConnected()) {
		if (lastlyWasConnected == 0) {
			lastlyWasConnected = 1;
			sprintf(symbol, "Fix client connected");
			res = FN_SMS;
			goto finish;
		}
	} else {
		if (lastlyWasConnected) {
			lastlyWasConnected = 0;
			sprintf(symbol, "Fix client disconnected");
			res = FN_SMS;
		} else {
			res = FN_WAIT;
		}
		goto finish;
	}

parseOrder:

	// if we have a message in the buffer, parse and process it
	nn = fixParseLineIfWholeAvailableInBuffer(readbuff.buffer+readbuff.i, readbuff.buffer+readbuff.j, &err);
	// debugPrintf("nn == %d", nn);
	if (nn > 0) {
		res = FN_CONTINUE;
		if (err == 0) {
			res = fixProcessParsedLine(readbuff.buffer+readbuff.i, nn, symbol, ivals, dvals);
		}
		//debugPrintf("res == %d", res);
		readbuff.i += nn;
		if (res == FN_CONTINUE) goto parseOrder;
		goto finish;
	} else if (nn < 0) {
		clientDisconnected();
		res = FN_WAIT;
		goto finish;
	}


	// if we have something on socket, read it
	// TODO: do this better, our port has number 300 or so and creating set, making select, etc. is bad
	r = selectOnClientSocket();
	if (r > 0) {
		fixReadedBytes = readBufferRepositionAndReadNextChunk(&readbuff, clientsockfd);
		//debugPrintf("fixReadedBytes == %d\n", fixReadedBytes);
		// if we have erad something process it immediately
		if (fixReadedBytes <= 0) {
			// an error, probably disconnection
			resetConnection();
			res = FN_WAIT;
			goto finish;
		}
		if (fixReadedBytes != 0) goto parseOrder;
	} else if (r < 0) {
		// an error, probably disconnection
		resetConnection();
		res = FN_WAIT;
		goto finish;
	}

	// no new order parsed nor read, check for executions
	i = ivals[IV_CHECK_INDEX];
	jmax = ivals[IV_CHECK_INDEX2];
	for( ; i<SIZE_OF_OPEN_ORDERS_HASH_TABLE; i++) {
		oo = orders[i];
		for(j=0; j<jmax && oo != NULL; j++, oo=oo->next);
		if (oo != NULL) {
			ivals[IV_CHECK_INDEX] = i;
			ivals[IV_CHECK_INDEX2] = j+1;
			ivals[IV_TICKET] = oo->ticket;
			ivals[IV_MAGIC] = oo->mbtn;
			ivals[IV_CMD] = oo->cmd;
			ivals[IV_VOLUME] = oo->volume;
			ivals[IV_SYM_HASH] = oo->symid;
			dvals[DV_PRICE] = oo->price;
			res = FN_CHECK_ORDER;
			goto finish;
		}
		jmax = 0;
	}
	ivals[IV_CHECK_INDEX] = 0;
	ivals[IV_CHECK_INDEX2] = 0;

	if (ivals[IV_CHECK_SYMBOL] >= 0) {
		ivals[IV_CHECK_SYMBOL] ++;
		if (ivals[IV_CHECK_SYMBOL] >= SYM_HASH_SIZE) ivals[IV_CHECK_SYMBOL] = 0;
		res = checkForOrderMerging(symbol, ivals, dvals);
		goto finish;
	}

	res = FN_CONTINUE;

finish:

	if (ORDER_CRITICAL_SECTION) LeaveCriticalSection(&orderCriticalSection);
	return(res);
}

///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////

BOOL WINAPI DllMain (HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
	int i;
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            // Code to run when the DLL is loaded
			initErrCodes();
			SGLIB_HASH_TAB_INIT(char, symTable, SYM_HASH_SIZE);
			for(i=0; i<SYM_HASH_SIZE; i++) exorders[i] = NULL;
			if (latencyMode < 1) timeBeginPeriod(1);
			InitializeCriticalSectionAndSpinCount(&socketCriticalSection, 1);
			if (ORDER_CRITICAL_SECTION) InitializeCriticalSectionAndSpinCount(&orderCriticalSection, 1);
            break;

        case DLL_PROCESS_DETACH:
            // Code to run when the DLL is freed
			DeleteCriticalSection(&socketCriticalSection);
			if (ORDER_CRITICAL_SECTION) DeleteCriticalSection(&orderCriticalSection);
			if (latencyMode < 1) timeEndPeriod(1);
			resetConnection();
            break;
#if 0
        case DLL_THREAD_ATTACH:
            // Code to run when a thread is created during the DLL's lifetime
            break;

        case DLL_THREAD_DETACH:
            // Code to run when a thread ends normally.
            break;
#endif

    }
    return TRUE;
}

