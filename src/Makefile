IDIR =../include
IEXTRADIR= #/data/john/libs/include
CC=gcc
CFLAGS=-I$(IDIR) # -I$(IEXTRADIR) -g -fsanitize=address -static-libasan

# define DEBUG in the source if we are debugging
ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG
endif

ODIR=obj
LDIR=

LIBS=-lm -lpthread #-lzbd

_DEPS = invokegc.h util.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = invokegc.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))


$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

invokegc: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS) $(LDIR)

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~
