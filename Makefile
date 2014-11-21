CFLAGS=-O3
CC=gcc

all : locktest-thin locktest-thin-cas locktest-thin-halfword locktest-fat

locktest-thin : locktest.c Makefile
	$(CC) $(CFLAGS) -o $@ $<

locktest-thin-cas : locktest.c Makefile
	$(CC) $(CFLAGS) -DCAS_ON_EXIT -o $@ $<

locktest-thin-halfword : locktest.c Makefile
	$(CC) $(CFLAGS) -DHALFWORD_EXIT -o $@ $<

locktest-fat : locktest.c Makefile
	$(CC) $(CFLAGS) -DFAT_LOCK -o $@ $<
