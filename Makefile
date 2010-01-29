CC	= gcc
CFLAGS = -Wall -g -DMULTITHREADED
LDFLAGS = -lpthread
TARGET = ethttpd


all: $(TARGET)


$(TARGET): ethttpd_http.o ethttpd.o


ethttpd_http.o: ethttpd.h


ethttpd.o: ethttpd.h


clean:
	rm -f *.o $(TARGET)

