
SRCS = 	amiga_hunk_parser.c  test.c

OBJS := $(patsubst %,%.o,$(basename $(SRCS)))

DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

CFLAGS = -Wall -Werror -g
LDFLAGS = -lm

CC = gcc

.PHONY: clean all
all:	ahp
clean:
	rm -f *.o ahp

%.o : %.c $(DEPDIR)/%.d | $(DEPDIR)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) $< -o $@

ahp:	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.c=$(DEPDIR)/%.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

