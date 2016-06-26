#ifndef EXAMPLE_DLL_H
#define EXAMPLE_DLL_H

// this is a header shared with metatrader .mq4 expert advisor
// it defines mainly at which indices the actual parameters are stored in parameters
// arrays.
#include "fixGwConstants.h"

#ifdef BUILDING_DLL
#define DLL_SPEC __declspec(dllexport)
#else
#define DLL_SPEC __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Here are prototypes of functions exported by this dll

// action called on symbol price update
void DLL_SPEC fixGatewayOnTick(char *symbol, double bid, double ask, int bidsize, int asksize);

// 'callback' functions passing result of metatrader trade functions to this dll.
// Original parameters from 'PoolOrder' are passes as well, so that the dll
// knows to which call the callback function was called
void DLL_SPEC fixGatewayOnOrderSendResult(char *sym, int ivals[], double dvals[], int ticket, double openPrice, double commision, int lastError);
void DLL_SPEC fixGatewayOnOrderModifyResult(char *sym, int ivals[], double dvals[], int b, int lastError);
void DLL_SPEC fixGatewayOnOrderDeleteResult(char *sym, int ivals[], double dvals[], int b, int lastError);
void DLL_SPEC fixGatewayOnOrderCloseResult(char *sym, int ivals[], double dvals[], int b, double closePrice, int lastError);
void DLL_SPEC fixGatewayOnOrderCloseByResult(char *sym, int ivals[], double dvals[], int b, int lastError);
void DLL_SPEC fixGatewayOnCheckOrderResult(char *sym, int ivals[], double dvals[], int orderType, double openPrice, double commision);

// function informing about previously opened positions at mt4 start time
void DLL_SPEC fixGatewayInitialPositionNote(char *sym, int ivals[], double dvals[], int ticket, double openPrice);

// the main function generating orders for metatrader
// All the arrays are 'inout' parameters containing parameters
int DLL_SPEC fixGatewayPoolOrder(char *symbol, int ivals[], double dvals[]);

	void DLL_SPEC fixGatewayInitConf(int port, int latencyMode);

////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif  // EXAMPLE_DLL_H
