CC	= gcc
CFLAGS = -Wall -g -DMULTITHREADED
LDFLAGS = -lpthread
TARGET = idefix


all: $(TARGET)


$(TARGET): idefix_http.o idefix.o


idefix_http.o: idefix.h


idefix.o: idefix.h


clean:
	rm -f *.o $(TARGET)

