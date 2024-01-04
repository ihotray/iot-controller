PROG ?= apmgr
DEFS ?= -llua -liot-base-nossl -liot-json
EXTRA_CFLAGS ?= -Wall -Werror
CFLAGS += $(DEFS) $(EXTRA_CFLAGS)

all: $(PROG)

SRCS = main.c apmgr.c mqtt.c state.c callback.c

$(PROG):
	$(CC) $(SRCS) $(CFLAGS) -o $@


clean:
	rm -rf $(PROG) *.o
