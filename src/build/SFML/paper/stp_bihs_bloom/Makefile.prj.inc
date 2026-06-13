#-----------------------------------------------------------------------------
# GNU Makefile for static libraries: project dependent part
#-----------------------------------------------------------------------------

NAME = stp_bihs_bloom
DBG_NAME = $(NAME)
REL_NAME = $(NAME)

ROOT = ../../../..
VPATH = $(ROOT)

DBG_OBJDIR = $(ROOT)/objs/$(NAME)/debug
REL_OBJDIR = $(ROOT)/objs/$(NAME)/release
TEST_OBJDIR = $(ROOT)/objs/$(NAME)/test
DBG_BINDIR = $(ROOT)/bin/debug
REL_BINDIR = $(ROOT)/bin/release
TEST_BINDIR = $(REL_BINDIR)

PROJ_CXXFLAGS = -I$(ROOT)/absmapalgorithms -I$(ROOT)/graphalgorithms -I$(ROOT)/gui -I$(ROOT)/simulation -I$(ROOT)/environments -I$(ROOT)/mapalgorithms -I$(ROOT)/algorithms -I$(ROOT)/generic -I$(ROOT)/utils -I$(ROOT)/graph -I$(ROOT)/search -I$(ROOT)/envutil

PROJ_DBG_CXXFLAGS = $(PROJ_CXXFLAGS)
PROJ_REL_CXXFLAGS = $(PROJ_CXXFLAGS)
PROJ_TEST_CXXFLAGS = $(PROJ_CXXFLAGS) -DSTP_BIHS_BLOOM_TEST

PROJ_DBG_LNFLAGS = -L$(DBG_BINDIR)
PROJ_REL_LNFLAGS = -L$(REL_BINDIR)
PROJ_TEST_LNFLAGS = -L$(TEST_BINDIR)

PROJ_DBG_LIB = -lenvironments -lmapalgorithms -lalgorithms -lgraphalgorithms -lgui -lgraph -lutils -lenvutil -lenvironments -lutils
PROJ_REL_LIB = -lenvironments -lmapalgorithms -lalgorithms -lgraphalgorithms -lgui -lgraph -lutils -lenvutil -lenvironments -lutils
PROJ_TEST_LIB = -lenvironments -lmapalgorithms -lalgorithms -lgraphalgorithms -lgui -lgraph -lutils -lenvutil -lenvironments -lutils

PROJ_DBG_DEP = \
  $(DBG_BINDIR)/libutils.a \
  $(DBG_BINDIR)/libgraph.a \
  $(DBG_BINDIR)/libgui.a \
  $(DBG_BINDIR)/libenvironments.a \
  $(DBG_BINDIR)/libmapalgorithms.a \
  $(DBG_BINDIR)/libgraphalgorithms.a \
  $(DBG_BINDIR)/libalgorithms.a 


PROJ_REL_DEP = \
  $(REL_BINDIR)/libutils.a \
  $(REL_BINDIR)/libgraph.a \
  $(REL_BINDIR)/libgui.a \
  $(REL_BINDIR)/libenvironments.a \
  $(REL_BINDIR)/libmapalgorithms.a \
  $(REL_BINDIR)/libgraphalgorithms.a \
  $(REL_BINDIR)/libalgorithms.a 

PROJ_TEST_DEP = $(PROJ_REL_DEP)

ifeq ("$(OPENGL)", "STUB")
PROJ_DBG_LIB += -lSTUB
PROJ_REL_LIB += -lSTUB
PROJ_TEST_LIB += -lSTUB
PROJ_DBG_DEP +=   $(DBG_BINDIR)/libSTUB.a
PROJ_REL_DEP +=   $(REL_BINDIR)/libSTUB.a
PROJ_TEST_DEP +=   $(REL_BINDIR)/libSTUB.a
endif

default : all

SRC_CPP = \
	paper/stp_bihs_bloom/Driver.cpp \
	paper/stp_bihs_bloom/Bloom.cpp

TEST_NAME = $(NAME)_test
TEST_SRC_CPP = \
	paper/stp_bihs_bloom/test.cpp \
	paper/stp_bihs_bloom/Driver.cpp \
	paper/stp_bihs_bloom/Bloom.cpp
