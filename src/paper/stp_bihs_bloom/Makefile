
# Makefile for stp_bihs_bloom

CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -I../.. -I../../environments -I../../generic -I../../search -I../../utils -I../../abstraction -I../../algorithms -I../../graph -I../../simulation

# Adjust paths as necessary based on where headers are located
# Based on grep results:
# MNPuzzle.h -> environments/
# IncrementalIDA.h -> generic/
# Bloom.h -> local directory

TARGET = stp_bihs_bloom
SRCS = Driver.cpp Bloom.cpp bihsBloom.cpp
OBJS = $(SRCS:.cpp=.o)



LDFLAGS = -L../../bin/release -L/usr/lib -L/usr/local/lib
LIBS = -lenvironments -lmapalgorithms -lalgorithms -lgraphalgorithms -lutils -lpthread -lGL


all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@



clean:
	rm -f $(TARGET) $(OBJS)
