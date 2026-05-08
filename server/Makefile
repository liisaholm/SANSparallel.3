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
 FC = gfortran
 CC_MPI =  /usr/lib64/openmpi/bin/mpicc
 FC_MPI =  /usr/lib64/openmpi/bin/mpif90
 OPT = -O3                  # uncomment this for optimization
 DEB = # -g                 # uncomment this for debugging
 DEFS =                     # place other -D types of things here
 CFLAGS = $(OPT) $(DEB) $(DEFS)
 LFLAGS =
#
# # objects
 OBJS = server_c.o sais.o  
# modules
 MODS = altschul.mod  keyspace.mod  maxheap.mod  myhash.mod  sanslocaldata.mod  sans.mod  socket_interface.mod 
#
 EXES = server
#
#
# # Top-level targets
all: $(OBJS) $(EXES) 
install: all

#-----------------------------
# sans program suite
# #-----------------------------
#
# # parameters module
sais.o: $(SRC)/sais.c $(SRC)/sais.h
	$(CC) $(CFLAGS) -c $(SRC)/sais.c

server_c.o: $(SRC)/server_c.c
	$(CC_MPI) $(CFLAGS) -c $(SRC)/server_c.c

# server
server: $(SRC)/server.f server_c.o sais.o 
	$(FC_MPI) $(SRC)/server.f sais.o server_c.o -o server

# clean
clean: 
	rm -rf $(OBJS) $(MODS) $(EXES)

 
