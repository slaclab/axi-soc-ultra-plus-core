# Output and specific RFDC object files (to be passed as arguments)
OUTS =
RFDC_OBJS =

# Compiler and flags
CFLAGS += -I${STAGING_INCDIR} -D__aarch64__ -Wall -Wextra -O2
LDFLAGS += -L${STAGING_LIBDIR} -lrfdc -lmetal -Wl,--hash-style=gnu

all: $(OUTS)

$(OUTS): $(RFDC_OBJS)
	$(CC) $(CFLAGS) $(RFDC_OBJS) -o $(OUTS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OUTS) $(RFDC_OBJS)
