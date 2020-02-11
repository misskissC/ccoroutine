# Makefile for yield_form_e
#
# usage brief:
### TARGET - executable program of yield_from_e
### CCTX   - CCTX_UC, CCTX_ASM
### ROOT_PATH - root path of ccoroutines
###	PRE_MACRO - -DRUNNING_WHEN_CREATEING=1/0,
# 				-DMEMORY_ALLOC_PRE=1/0
#
###	CCTX decides the support for coroutine switching
# 	[1] CCTX_UC  - System V ucontext;
# 	[2] CCTX_ASM - assembly-instructions, 
# 		self implement.
#
### PRE_MACRO decides ccoroutines to own more 
#	efficient when scheduling coroutines.
#  [1] RUNNING_WHEN_CREATEING=1, running old 
#  		coroutines when creating new one;
#  [2] MEMORY_ALLOC_PRE=1, pre-alloc all memories 
#  		for coroutines.
#
# lxr, 2020.02
TARGET=yield_from_e

CCTX=CCTX_ASM
ROOT_PATH=../../
PRE_MACRO=#-DRUNNING_WHEN_CREATING=0 -DMEMORY_ALLOC_PRE=0

-include ../make_e
