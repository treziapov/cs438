#include "utility.h"

/*
 * Convert given int to a string
 */
string Utility::int_to_string(int number)
{
	stringstream ss;
	ss << number;
	#ifdef DEBUG
		cout << "int_to_string: " << number << " to " << ss.str() << endl;
	#endif
	return ss.str();
}

/*
 * Get sockaddr, IPv4 or IPv6
 */
void* Utility::get_in_addr(struct sockaddr* sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	#ifdef DEBUG
		cout << "get_in_addr" << endl;
	#endif
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 * Updates the passed info with networking information for given host and port
 */
void Utility::set_addrinfo(const char* host, const char* port, 
	struct addrinfo* hints, struct addrinfo* info)
{
	int rv;
	if ((rv = getaddrinfo(host, port, hints, &info)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
	}

	#ifdef DEBUG
		cout << "set_addrinfo" << endl;
	#endif
}

/*
 * Receives data on the given bound socket
 * Stores data into the given buffer
 * Returns the size of data in bytes
 */
int Utility::receive(int socket, char* buffer, int buffer_size, 
	struct sockaddr* their_addr, socklen_t* addr_len)
{
	#ifdef DEBUG
		cout << "receive: started ... ";
	#endif

	int numbytes = recvfrom(socket, buffer, buffer_size - 1 , 0, 
		their_addr, addr_len);

	if (numbytes == -1) 
	{
		perror("recvfrom");
	}

	buffer[numbytes] = '\0';
	
	#ifdef DEBUG
		cout << "message - " << buffer << endl;
	#endif
		
	return numbytes;
}

/*
 * Sends the given message through the given socket
 * Returns the number of bytes sent
 */
int Utility::send(int socket, const char* message, 
		struct sockaddr* addr, socklen_t addr_len)
{
	int numbytes = sendto(socket, message, strlen(message), 0,
		addr, addr_len);

	if (numbytes == -1) 
	{
		perror("manager: send addresses");
	}

	#ifdef DEBUG
		cout << "send: message - " << message;
		cout << ", bytes sent - " << numbytes << endl;
	#endif
	return numbytes;
}

/*
 * Returns the port the given soket is listening on
 */
 int Utility::get_listening_port(int socket)
 {
	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);
	if (getsockname(socket, (struct sockaddr *)&sin, &len) == -1)
	{
	    perror("getsockname");
	    return -1;
	}
	else
	{
		int result = ntohs(sin.sin_port);
		#ifdef DEBUG
			cout << "get_listening_port: " << result << endl;
		#endif
  		return result;  	
    }
 }