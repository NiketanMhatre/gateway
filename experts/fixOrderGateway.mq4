//+------------------------------------------------------------------+
//|                                              fixOrderGateway.mq4 |
//|                                Copyleft  2012, Xrefactory s.r.o. |
//+------------------------------------------------------------------+

#property copyright "Copyleft 2012, Xrefactory s.r.o."
#property link      "http://www.xrefactory.com"


#include "fixGwConstants.h"

#import "fixGateway.dll"
int  fixGatewayPoolOrder(string symbol, int ivals[], double dvals[]);

void fixGatewayOnOrderSendResult(string symbol, int ivals[], double dvals[], int ticket, double openPrice, double commision, int lastError);
void fixGatewayOnOrderModifyResult(string symbol, int ivals[], double dvals[], int b, int lastError);
void fixGatewayOnOrderDeleteResult(string symbol, int ivals[], double dvals[], int b, int lastError);
void fixGatewayOnOrderCloseResult(string symbol, int ivals[], double dvals[], int b, double closePrice, int lastError);
void fixGatewayOnOrderCloseByResult(string symbol, int ivals[], double dvals[], int b, int lastError);
void fixGatewayOnCheckOrderResult(string symbol, int ivals[], double dvals[], int orderType, double openPrice, double commision);
void fixGatewayInitConf(int port, int latencyMode);
void fixGatewayInitialPositionNote(string symbol, int ivals[], double dvals[], int ticket, double openPrice);
#import


// Here you can customize some behaviour

// Show orders executed from fix as Alerts
bool allowAlerts = false;
// Heartbeats allow to keep metatrader up
bool allowHeartbeats = false;
// If there are two oposite executed (opened) orders they can be automatically closed
bool allowAutoCloseBy = true;


// internal variables
string symbol;
int    ivals[VALS_SIZE];
double dvals[VALS_SIZE];

#define DSLIPPAGE 100
double exp10[8] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000};
double dslippagePerDigits[8] = {100.0, 10.0, 1.0, 0.1, 0.01, 0.001, 0.0001, 0.00001};

int gatewayport = 9871;
int heartbeat = 0;
int heartbeatTicket = 0;
string heartbeatComment = "heart";
string heartbeatSym = "USDJPY";

void resetHeartbeats() {
     int i;
       
     if (allowHeartbeats == false) return;
     
     // delete all old pending heartbeat orders
     heartbeatTicket = -1;
     for(i=0;i<OrdersTotal();i++) {
        OrderSelect(i, SELECT_BY_POS, MODE_TRADES);
        if (OrderComment() == heartbeatComment) {
            if (heartbeatTicket < 0) {
               heartbeatTicket = OrderTicket();
            } else {
               OrderDelete(OrderTicket());
            }
        }
    }
    if (heartbeatTicket < 0) {
       heartbeatTicket = OrderSend(heartbeatSym, OP_BUYLIMIT, 
                          10000.0/MarketInfo(heartbeatSym,MODE_LOTSIZE), 
                          20.0, 5, 0, 0, heartbeatComment, 11111
                          );
    }
}

double getLotSize(string symbol) {
	double lotSize;
	lotSize = MarketInfo(symbol, MODE_LOTSIZE);
	if (lotSize == 0) lotSize = 100000;
	return(lotSize);
}

void doHeartbeat() {
    double price;
    bool   rr;
    heartbeat ++;
    price = 10 + (heartbeat % 10);
    rr = OrderModify(heartbeatTicket, price, 0, 0, 0);
    if (rr != true) {
        resetHeartbeats();
    }
    // Alert("Heart beat " + price);
}


//+------------------------------------------------------------------+
//| expert initialization function                                   |
//+------------------------------------------------------------------+
int init() {
    int i;
 
    // You can change "server" port by setting global 
    // variable 'fixGatewayPort'. You have to restart Metatrader if 
    // you change the port number.
    int port = GlobalVariableGet("fixGatewayPort");
    if (port != 0) gatewayport = port;

	// fixGatewayLatencyMode == 0 or none means top latency
	// fixGatewayLatencyMode >= 1 means 'timeBeginPeriod(1)' is not turned on
	// fixGatewayLatencyMode >= 2 means 'TCP_NODELAY' is not turned on
    int latencyMode = GlobalVariableGet("fixGatewayLatencyMode");
	// Alert("LatencyMode == " + latencyMode);

    fixGatewayInitConf(gatewayport, latencyMode);

	int hbs = GlobalVariableGet("fixGatewayHeartbeat");
	if (hbs != 0) allowHeartbeats = true;
	if (hbs == 0xe0)  heartbeatSym = "USDJPYmicro";		// an ad-hoc value for xemarkets micro account
	if (hbs == 0xad)  heartbeatSym = "USDJPY.arm";		// an ad-hoc value for armada prime FX

	int alert = GlobalVariableGet("fixGatewayAlerts");
	if (alert != 0) allowAlerts = true;

    // create a string large enough to keep messages
    symbol = "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890";

    for(i=0; i<VALS_SIZE; i++) {
        ivals[i] = 0;
        dvals[i] = 0.0;
    }

    if (! allowAutoCloseBy) ivals[IV_CHECK_SYMBOL] = -1;
    if (allowHeartbeats) resetHeartbeats();

	for(i=0;i<OrdersTotal();i++) {
		int 		cmd;
		string     symbol;
        OrderSelect(i, SELECT_BY_POS, MODE_TRADES);
		cmd = OrderType();
		symbol = OrderSymbol();

		if (cmd == OP_BUY || cmd == OP_SELL) {
			ivals[IV_MAGIC] = 0;
			ivals[IV_CMD] = cmd;
			ivals[IV_VOLUME] = NormalizeDouble(OrderLots() * getLotSize(symbol), 0);
			fixGatewayInitialPositionNote(symbol, ivals, dvals, OrderTicket(), OrderOpenPrice());
			// Alert("fixGatewayInitialPositionNote(" + symbol + ", " + ivals[IV_VOLUME] + ", " + OrderTicket() + ", " + OrderOpenPrice());
		}
    }
	

    return(0);
}


// +------------------------------------------------------------------+
// | expert deinitialization function                                 |
// +------------------------------------------------------------------+
int deinit() {
   return(0);
}


//+------------------------------------------------------------------+
//| expert start function                                            |
//+------------------------------------------------------------------+
int start() {
    bool b;
    int ord, ticket, lastError;
    double price, lotSize;
    if (allowAlerts) Alert("Fix Gateway is Running (port " + gatewayport + ").");
    while (true) {
        ord = fixGatewayPoolOrder(symbol, ivals, dvals);
        //Alert("ord == " + ord);
        switch (ord) {
        case FN_CONTINUE:
            // let others do something
            Sleep(1);
            break;
        case FN_HEARTBEAT:
            if (allowHeartbeats) doHeartbeat();
           break;
        case FN_WAIT:
            // Alert("Waiting");
            Sleep(1000);
            break;
        case FN_ORDER_SEND:
        case FN_ORDER_CLOSE:
            if (ivals[IV_CMD] == OP_BUY) {
                RefreshRates();
                price = MarketInfo(symbol, MODE_ASK);
            } else if (ivals[IV_CMD] == OP_SELL) {
                RefreshRates();
                price = MarketInfo(symbol, MODE_BID);
            } else {
                price = dvals[DV_PRICE];
            }
			lotSize = MarketInfo(symbol,MODE_LOTSIZE);
			if (lotSize == 0) lotSize = 100000;
			// compute slippage
			int    digits = MarketInfo(symbol, MODE_DIGITS);
			int    slippage = 1000;
			// if the worst price is comming from fix protocol adjust price and slippage params
			if (dvals[DV_PRICE] != 0) {
			   if (ivals[IV_CMD] == OP_BUY || ivals[IV_CMD] == OP_SELL) {
			   	  if (ivals[IV_CMD] == OP_BUY) {
			   	  	 slippage = (dvals[DV_PRICE] - price) * exp10[digits];
			   	  } else {
			   	  	 slippage = (price - dvals[DV_PRICE]) * exp10[digits];
				  }
				  if (slippage < 0) {
				     // this will probably fail anyway
					 slippage = DSLIPPAGE;
					 if (ivals[IV_CMD] == OP_BUY) {
					 	price = dvals[DV_PRICE] - dslippagePerDigits[digits];
					 } else {
					 	price = dvals[DV_PRICE] + dslippagePerDigits[digits];
					 }
				  }
				  if (slippage <= 0) slippage = 1;
				}
			}
			if (ord == FN_ORDER_SEND) {
				// order send
				ticket = OrderSend(symbol, ivals[IV_CMD], ivals[IV_VOLUME] / lotSize, price, slippage, 0, 0, NULL, ivals[IV_MAGIC]);
				lastError = GetLastError();
				b = OrderSelect(ticket, SELECT_BY_TICKET);
				fixGatewayOnOrderSendResult(symbol, ivals, dvals, ticket, OrderOpenPrice(), OrderCommission(), lastError);
				if (allowAlerts) {
					Alert("OrderSend(" + symbol + ", " + ivals[IV_CMD] + ", " + ivals[IV_VOLUME]/lotSize + ", " + price + ", " + slippage + ", 0, 0, NULL, " + ivals[IV_MAGIC] + ");");
					if (ticket < 0) Alert("OrderSend: error " + lastError);
				}
			} else {
				// order close
				b = OrderSelect(ivals[IV_TICKET], SELECT_BY_TICKET);
				b = OrderClose(ivals[IV_TICKET], ivals[IV_VOLUME] / lotSize, price, slippage);
				fixGatewayOnOrderCloseResult(symbol, ivals, dvals, b, OrderClosePrice(), GetLastError());
				if (allowAlerts) Alert("OrderClose(" + ivals[IV_TICKET] + "," + ivals[IV_VOLUME]/lotSize + "," + price + "," + slippage + ");");
			}
            break;
        case FN_ORDER_MODIFY:
            b = OrderModify(ivals[IV_TICKET], dvals[DV_PRICE], 0, 0, 0);
            fixGatewayOnOrderModifyResult(symbol, ivals, dvals, b, GetLastError());
            if (allowAlerts) Alert("OrderModify(" + ivals[IV_TICKET] + ", " + dvals[DV_PRICE] + ", 0, 0, 0" + ");");
            break;
        case FN_ORDER_DELETE:
            b = OrderDelete(ivals[IV_TICKET]);
            fixGatewayOnOrderDeleteResult(symbol, ivals, dvals, b, GetLastError());
            if (allowAlerts) Alert("OrderDelete(" + ivals[IV_TICKET] + ");");
            break;
        case FN_ORDER_CLOSE_BY:
            b = OrderCloseBy(ivals[IV_TICKET], ivals[IV_TICKET2]);
            fixGatewayOnOrderCloseByResult(symbol, ivals, dvals, b, GetLastError());
            if (allowAlerts) Alert("OrderCloseBy(" + ivals[IV_TICKET] + ", " + ivals[IV_TICKET2] + ");");
            break;
        case FN_CHECK_ORDER:
            b = OrderSelect(ivals[IV_TICKET], SELECT_BY_TICKET);
            if (b) fixGatewayOnCheckOrderResult(symbol, ivals, dvals, OrderType(), OrderOpenPrice(), OrderCommission());
            // Alert("Check Order " + ivals[IV_TICKET]);
            break;
        case FN_SMS:
            if (allowAlerts) Alert(symbol);
            Sleep(100);
            break;
        case FN_DEBUG:
            // Alert("FN_DEBUG: sym==" + symbol + ", cmd==" + ivals[IV_CMD] + ", volume==" + ivals[IV_VOLUME] + ", price==" + dvals[DV_PRICE] + ", slippage==" + ivals[IV_SLIPPAGE]);
            Alert("FN_DEBUG: " + symbol);
            Sleep(10);
            break;
        default:
            Alert("Unknown order function " + ord + " required from pool");
            Sleep(100);
        }
    }
    return(0);
}

