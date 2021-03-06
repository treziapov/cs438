#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PRINT_INFO 0

using namespace std;

class Utility
{
	public:
		// General
		static string int_to_string(int number);

		// Networking
		static int get_listening_port(int socket);
		static void* get_in_addr(struct sockaddr* sa);
		static void set_addrinfo(const char* host, const char* port, 
			struct addrinfo* hints, struct addrinfo* info);

		static int receive(int socket, char* buffer, int buffer_size, 
			struct sockaddr* their_addr, socklen_t* addr_len);
		static int send(int socket, const char* message, 
			struct sockaddr* addr, socklen_t addr_len);		
};
