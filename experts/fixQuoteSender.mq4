//+------------------------------------------------------------------+
//|                                               fixQuoteSender.mq4 |
//|                                 Copyleft 2012, Xrefactory s.r.o. |
//+------------------------------------------------------------------+

#property copyright "Copyleft 2012, Xrefactory s.r.o."
#property link      "http://www.xrefactory.com"


#import "fixGateway.dll"
void fixGatewayOnTick(string symbol, double bid, double ask, int bidsize, int asksize);
void fixGatewayInitConf(int port, int latencyMode);
#import

int gatewayport = 9871;

//+------------------------------------------------------------------+
//| expert initialization function                                   |
//+------------------------------------------------------------------+
int init() {
	// You can change fix "server" port by setting
	// global variable 'fixGatewayPort', You have to restart Metatrader if 
	// you change the port number.
	int port = GlobalVariableGet("fixGatewayPort");
    if (port != 0) gatewayport = port;

	// fixGatewayLatencyMode == 0 or none means top latency
	// fixGatewayLatencyMode >= 1 means 'timeBeginPeriod(1)' is not turned on
	// fixGatewayLatencyMode >= 2 means 'TCP_NODELAY' is not turned on
    int latencyMode = GlobalVariableGet("fixGatewayLatencyMode");

    fixGatewayInitConf(gatewayport, latencyMode);
	return(0);
}


//+------------------------------------------------------------------+
//| expert deinitialization function                                 |
//+------------------------------------------------------------------+
int deinit() {
   return(0);
}


//+------------------------------------------------------------------+
//| expert start function                                            |
//+------------------------------------------------------------------+
int start() {
	fixGatewayOnTick(Symbol(), Bid, Ask, 1000, 1000);
	return(0);
}

