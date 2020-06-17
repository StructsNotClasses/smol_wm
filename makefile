CC=gcc
LNK=-lX11 -lXinerama
SRC=test_wm.c
TRGT=test_wm
DBG=test_wm_debug
DFLAGS=-g -O0

$(TRGT) : $(SRC) 
	$(CC) -o $@ $(LNK) $(SRC)

$(DBG) : $(SRC) 
	$(CC) $(DFLAGS) -o $@ $(LNK) $(SRC)
