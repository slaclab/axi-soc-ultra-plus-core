# Output and specific RFDC object files (to be passed as arguments)
OUTS =
RFDC_OBJS =

all: $(OUTS)

$(OUTS): $(RFDC_OBJS)
	$(CC) $(CFLAGS) $(RFDC_OBJS) -o $(OUTS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OUTS) $(RFDC_OBJS)
