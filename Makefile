SRCS=fixGateway.c common.c
OBJS=fixGateway.o common.o

#####################################################################################
# add instalation dirs here

METATRADER_DIR="/c/Program Files/MetaTrader - Alpari UK"
METATRADER_DIR2="/c/Program Files/FXCM MetaTrader 4"

#####################################################################################
# add platforms goals here

all: a2 

#all: a1 a2 

#####################################################################################

dll:;
	gcc -c -O2 -DBUILDING_DLL -Iexperts $(SRCS)
	gcc -shared -o fixGateway.dll $(OBJS) -Wl,--out-implib,libfixGateway_dll.a -lws2_32 -lwinmm -lIphlpapi 
	cp fixGateway.dll experts/libraries/

#####################################################################################
# add new platforms here

a1: dll
	$(METATRADER_DIR)/metalang experts/fixQuoteSender.mq4
	$(METATRADER_DIR)/metalang experts/fixOrderGateway.mq4
	cp experts/*.ex4 $(METATRADER_DIR)/experts/
	cp fixGateway.dll $(METATRADER_DIR)/experts/libraries/

a2: dll
	$(METATRADER_DIR2)/metalang experts/fixQuoteSender.mq4
	$(METATRADER_DIR2)/metalang experts/fixOrderGateway.mq4
	cp experts/*.ex4 $(METATRADER_DIR2)/experts/
	cp fixGateway.dll $(METATRADER_DIR2)/experts/libraries/

