#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace std;

class Utility
{
	public:
		// General
		static string int_to_string(int number);

		// Networking
		static void* get_in_addr(struct sockaddr* sa);
		static void set_addrinfo(const char* host, const char* port, 
			struct addrinfo* hints, struct addrinfo* info);
		// static int receive(int socket, char* buffer, int buffer_size);
		// static int send(int socket, char* message, 
		// 	struct sockaddr* node_addr, size_t addr_len);
		static int get_listening_port(int socket);
};