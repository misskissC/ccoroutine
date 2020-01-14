# compiling rules for experiencing yield mechanism
# lxr, 2020.01
ROOT_PATH=../..
SRC=$(ROOT_PATH)/src
INCLUDE=$(ROOT_PATH)/include
SRCS=$(wildcard $(SRC)/*.c ./*.c)
#ASM=$(subst .c,.s $(SRCS))
HDRS=$(wildcard $(INCLUDE)/*.h $(SRC)/*.h)
PRE_MACRO= -DRUNNING_WHEN_CREATING=0 -DMEMORY_ALLOC_PRE=0

CC=gcc
CFLAGS +=-Wall -g -I$(INCLUDE) $(PRE_MACRO)
TARGET=yield_experience

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS) Makefile
	$(CC) $(CFLAGS) \
    -o $@ \
    $(SRCS)

asm: $(SRCS) $(HDRS) Makefile
	$(CC) $(CFLAGS) \
    -S $(SRCS)

asm_clean:
	-rm -f *.s

clean:
	-rm -f *.s
	-rm -f $(TARGET)
