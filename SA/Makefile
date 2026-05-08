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
 DATA=/data/uniprot/

# ##############################################################################################
# # end of configuration section                                                               #
# ##############################################################################################
#
# # Compiler options
 CC = gcc
 FC = gfortran
 #CC_MPI = /home/luholm/opt/openmpi/bin/mpicc                   # C compiler
 #FC_MPI = /home/luholm/opt/openmpi/bin/mpif90              # Fortran-95 compiler
 OPT = -O3                  # uncomment this for optimization
 DEB = # -g                 # uncomment this for debugging
 DEFS =                     # place other -D types of things here
 CFLAGS = $(OPT) $(DEB) $(DEFS)
 LFLAGS =
#
# # objects
 OBJS =  
# modules
#
 EXES = isa8 sap8 
#
#
# # Top-level targets
all: $(OBJS) $(EXES) 
install: all

#-----------------------------
# sans program suite
# #-----------------------------
#
isa8: $(SRC)/isa8.f 
	$(FC) $(CFLAGS) $(SRC)/isa8.f -o isa8

sap8: $(SRC)/sap8.f 
	$(FC) $(CFLAGS) $(SRC)/sap8.f -o sap8

# clean
clean: 
	rm -rf $(OBJS) $(MODS) $(EXES)

 
