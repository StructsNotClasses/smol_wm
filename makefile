CC=gcc
LNK=-lX11 -lXinerama
SRC=test_wm.c
TRGT=test_wm
CFLAGS=-Os -w
DBG=test_wm_debug
DFLAGS=-g -O0

$(TRGT) : $(SRC) 
	$(CC) $(CFLAGS) -o $@ $(LNK) $(SRC)

$(DBG) : $(SRC) 
	$(CC) $(DFLAGS) -o $@ $(LNK) $(SRC)
