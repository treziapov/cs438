CC = g++
CC_OPTS = -Wall -Wextra -Wchar-subscripts -Wimplicit -Wparentheses -Wreturn-type -Wmissing-braces -Wundef -Wshadow

all: reliable_sender reliable_receiver

reliable_sender: sender_main.cpp
	$(CC) $(CC_OPTS) sender_main.cpp -o reliable_sender

reliable_receiver: receiver_main.cpp
	$(CC) $(CC_OPTS) receiver_main.cpp -o reliable_receiver

clean:
	rm -f reliable_sender reliable_receiver