
#define VALS_SIZE		20

#define IV_SLIPPAGE     0
#define IV_CMD          1
#define IV_VOLUME       2
#define IV_MAGIC        3
#define IV_MAGIC2       4
#define IV_TICKET       5
#define IV_TICKET2      6
#define IV_CHECK_INDEX  7
#define IV_CHECK_INDEX2 8
#define IV_SYM_HASH     10

#define DV_PRICE        0

// possible orders (result of orderPool)
#define FN_CONTINUE       1
#define FN_HEARTBEAT      2
#define FN_WAIT           3
#define FN_ORDER_SEND     4
#define FN_ORDER_MODIFY   5
#define FN_ORDER_DELETE   6
#define FN_ORDER_CLOSE    7
#define FN_ORDER_CLOSE_BY 8
#define FN_CHECK_ORDER    9
#define IV_CHECK_SYMBOL  10
#define FN_SMS           11
#define FN_DEBUG         12

