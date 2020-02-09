# Makefile for context.
#
# usage:  
# 	 make CCTX OBJS
# 	 make clean CCTX OBJS
# usage e.g.
# 	make CCTX=CCTX_ASM OBJS=objs
# CCTX_ASM, ./objs defaulted.
# 
# the object-files of context 
# would be output to $(OBJS).
# 
# lxr, 2020.02

ROOT_PATH=../..

CCTX=CCTX_ASM
-include ../make_p
SRCS = $(wildcard $(CTX_PATH)/*.c)
INCLUDE = -I$(ROOT_PATH)/include -I./ -I$(CTX_PATH)

CC = gcc
CFLAGS += -Wall -g $(INCLUDE) $(PRE_MACRO)

OBJS=objs
TARGET = ${patsubst %.c, %.o, $(SRCS)}
all: $(OBJS) $(TARGET)

$(OBJS):
	@mkdir -p $(OBJS)

%.o:%.c
	$(CC) $(CFLAGS) \
	-c $< -o $@
	@cp $@ $(OBJS)

-include ../make_d

clean:
	-rm -f -r $(OBJS)
	-rm -f $(CTX_PATH)/*.[do]*
