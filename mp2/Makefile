CC = g++
LINKER = g++
WARNINGS = -Wchar-subscripts -Wimplicit -Wparentheses -Wreturn-type -Wmissing-braces -Wundef -Wshadow
CC_OPTS = -Wall -Wextra $(WARNINGS)

EXECS = manager distvec
OBJS = manager.o distvec.o topology.o utility.o

CODE_CLEAN = *.o $(EXECS)

all: $(OBJS) $(EXECS)

manager: manager.o 
	$(LINKER) manager.o topology.o utility.o -o manager

distvec: distvec.o
	$(LINKER) distvec.o topology.o utility.o -o distvec

manager.o: manager.cpp topology.o utility.o
	$(CC) $(CC_OPTS) -c manager.cpp

distvec.o: distvec.cpp topology.o utility.o
	$(CC) $(CC_OPTS) -c distvec.cpp

topology.o: topology.h topology.cpp
	$(CC) $(CC_OPTS) -c topology.cpp

utility.o: utility.h utility.cpp
	$(CC) $(CC_OPTS) -c utility.cpp

clean: 
	rm -f $(CODE_CLEAN)