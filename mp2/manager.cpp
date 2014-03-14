#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <map>
#include "topology.h"
#include "utility.h"

#define TOPOLOGY_FILE "topology.txt"
#define MESSAGE_FILE "message.txt"
#define START_PORT 20000
#define MANAGER_PORT "6000"
#define BUFFER_SIZE 255

using namespace std;

/*
 * Allocates the next available virtual id
 */
 int get_free_virtual_id()
 {
 	static int next_virtual_id = 0;
 	next_virtual_id++;
 	return next_virtual_id;
 }

/*
 * Reads topology and message files
 * Listens for connections from nodes
 * Assigns nodes their vitual ids
 * Lets update toplogy through stdin
 */
int main(int argc, char* arv[])
{
	// Get information about the topology
	Topology topology;
	topology.parse_topology_file(TOPOLOGY_FILE);
	list<Message> messages = Message::parse_message_file(MESSAGE_FILE);
	map<int, Link> node_links;

	// Set up networking
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	struct sockaddr_storage their_addr;
	char buf[BUFFER_SIZE];
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];

	// Set to AF_INET to force IPv4, use my IP
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; 
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(NULL, MANAGER_PORT, &hints, &servinfo);

	// Loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("manager: socket");
			continue;
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("manager: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "manager: failed to bind socket\n");
		return 2;
	}

	freeaddrinfo(servinfo);

	// TODO: start listening for stdin changes

	// Loop, waiting for connections
	while (true)
	{
		// Wait for new connections
		addr_len = sizeof(their_addr);
		recvfrom(sockfd, buf, BUFFER_SIZE - 1 , 0, 
			(struct sockaddr *)&their_addr, &addr_len);

		
		// Get its IP and assign port for later communication
		const char* node_ip = inet_ntop(their_addr.ss_family,
			Utility::get_in_addr((struct sockaddr*)&their_addr), s, sizeof s);
		
		cout << Utility::get_listening_port(sockfd)	<< endl;
	    
		// Assign the node a virtual ID and send it
		int virtual_id = get_free_virtual_id();
		string assigned_port = Utility::int_to_string(START_PORT + virtual_id);
		topology.update_node_net_info(virtual_id, string(node_ip), assigned_port);
		const char* message = Utility::int_to_string(virtual_id).c_str();
		sendto(sockfd, message, strlen(message), 0,
			(struct sockaddr*)&their_addr, addr_len);

		// Wait for the node to bind to the new port
		recvfrom(sockfd, buf, BUFFER_SIZE - 1 , 0,
			(struct sockaddr *)&their_addr, &addr_len);

		// Send information about links
		list<Link> links = topology.get_node_links(virtual_id);
		string links_string = Topology::serialize_node_links(links);
		const char* links_cstring = links_string.c_str();
		sendto(sockfd, links_cstring, strlen(links_cstring), 0,
	 		(struct sockaddr*)&their_addr, addr_len);

		// Go to next stage if all nodes connected
		if (virtual_id == topology.num_nodes - 1)
		{
			break;
		}
		if (virtual_id >= topology.num_nodes)
		{
			perror("manager: too many nodes");
			exit(1);
		}
	}

	for (int id = 1; id < topology.num_nodes; id++)
	{
		// Send information about links
		Link* self = &topology.matrix[id][id];
		list<Link> links = topology.get_node_links(id);
		string links_string = Topology::serialize_node_links(links);
		const char* links_cstring = links_string.c_str();

		struct addrinfo* node_addr;
		getaddrinfo(self->ip.c_str(), self->port.c_str(), &hints, &node_addr);
		sendto(sockfd, links_cstring, strlen(links_cstring), 0,
			node_addr->ai_addr, node_addr->ai_addrlen);
	}

	// Wait for all nodes to converge
	for (int id = 1; id < topology.num_nodes; id++)
	{
		Link* node = &topology.matrix[id][id];
		struct addrinfo* node_addr;
		getaddrinfo(node->ip.c_str(), node->port.c_str(), &hints, &node_addr);
		recvfrom(sockfd, buf, BUFFER_SIZE - 1, 0,
			(struct sockaddr*)&their_addr, &addr_len);
	}

	// Tell nodes to send message according to the message file
	for (list<Message>::iterator it = messages.begin(); it != messages.end(); it++)
	{
		int id = it->source_id;
		Link* node = &topology.matrix[id][id];
		string text = Message::serialize(*it);
		const char* cstr = text.c_str();

		struct addrinfo* node_addr;
		getaddrinfo(node->ip.c_str(), node->port.c_str(), &hints, &node_addr);
		sendto(sockfd, cstr, strlen(cstr), 0,
			node_addr->ai_addr, node_addr->ai_addrlen);
	}

	while (true)
	{
		sleep(1);
	}

	close(sockfd);
	return 0;
}