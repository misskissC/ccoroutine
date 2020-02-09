# Makefile for making object-files of src.
#
# usage:
# 	make CCTX OBJS
# usage e.g.
# 	make CCTX=CCTX_ASM OBJS=objs
# CCTX_ASM, objs defaulted.
#
# the object files would be output to $(OBJS).
#
# lxr, 2020.02

ROOT_PATH = ..
CCTX=CCTX_ASM
OBJS=objs

#PRE_MACRO=-DRUNNING_WHEN_CREATING=1 -DMEMORY_ALLOC_PRE=0
-include make_p

all: $(OBJS) sub_all curr_all
$(OBJS):
	@mkdir -p $(OBJS)

sub_make=context
SUB_OBJS=objs
sub_all:
	@make -C $(sub_make) CCTX=$(CCTX) \
		OBJS=$(SUB_OBJS)

CC = gcc
SRCS = $(wildcard ./*.c)
INCLUDE = -I$(ROOT_PATH)/include -I$(sub_make)
CFLAGS += -Wall -g $(INCLUDE) $(PRE_MACRO) $(LN_PRE_MACRO)
TARGET = ${patsubst %.c, %.o, $(SRCS)}

curr_all: $(TARGET)

%.o:%.c
	$(CC) $(CFLAGS) \
	-c $< -o $@
	@cp $@ $(OBJS)	
	@cp $(sub_make)/$(SUB_OBJS)/*.o $(OBJS)

-include make_d

# don't name file ends .d or .o
clean:
	@make -C $(sub_make) clean CCTX=$(CCTX) \
		OBJS=$(SUB_OBJS)
	-rm -f -r $(OBJS)
	-rm -f ./*.[do]*
