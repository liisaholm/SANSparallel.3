# Makefile for  SANS-server.
# # Copyright (C) 2014 Liisa Holm.
# 
# ##############################################################################################
# # start of configuration section                                                             #
# ##############################################################################################
#
 SHELL = /bin/sh
# Directories <--- modify these!!
 HOME=./
 SRC=$(HOME)
 BIN=$(HOME)

# ##############################################################################################
# # end of configuration section                                                               #
# ##############################################################################################
#
# # Compiler options
 CC = gcc
 OPT = -O3                  # uncomment this for optimization
 DEB = # -g                 # uncomment this for debugging
 DEFS =                     # place other -D types of things here
 CFLAGS = $(OPT) $(DEB) $(DEFS)
 LFLAGS =
#
# # objects
 OBJS = client.o 
# modules
 MODS =  
#
 EXES = client 
#
#
# # Top-level targets
all: $(OBJS) $(EXES) 
install: all

#-----------------------------
# sans program suite
# #-----------------------------
#
# client
client: $(SRC)/client.c
	$(CC) $(CFLAGS) $(SRC)/client.c -o client

# clean
clean: 
	rm -rf $(OBJS) $(MODS) $(EXES)

 
