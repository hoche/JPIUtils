CC=gcc $(CFLAGS)
CXX=g++ $(CFLAGS)


INCLUDES=
LIBS=

CFLAGS=-g -Wall
CPPFLAGS=-std=c++17
LDFLAGS=

PROG= tllogparser

OBJS=$(SRCS:.cpp=.o)
#OBJS= 

SRCS= logparser.cpp \
      main.cpp

all: 
	@make clean
	@make $(PROG)


$(PROG): $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

clean:
	rm -f $(OBJS) $(PROG)

.SUFFIXES: .c .cpp .o

.c.o:
	$(CC) $(CPPFLAGS) $(INCLUDES) -c $<

.cpp.o:
	$(CXX) $(CPPFLAGS) $(INCLUDES) -c $<
