#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <map>
#include <pthread.h>
#include "topology.h"
#include "utility.h"

#define TOPOLOGY_FILE "topology.txt"
#define MESSAGE_FILE "message.txt"
#define START_PORT 20000
#define MANAGER_PORT "6000"
#define BUFFER_SIZE 255

using namespace std;

/*
 * Globals
 */
Topology topology;
map<int, Link> node_links;
list<Message> node_messages;
int server_socket;
char buffer[BUFFER_SIZE];
struct addrinfo hints;

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
 *	Tell nodes to send message according to the message file
 */
void send_node_messages()
{
	#if PRINT_INFO == 1
		cout << "debug: sending node_messages to nodes" << endl;
	#endif

	for (list<Message>::iterator it = node_messages.begin(); it != node_messages.end(); it++)
	{
		int id = it->source_id;
		Link* node = &topology.matrix[id][id];
		string text = Message::serialize(*it);
		const char* cstr = text.c_str();

		struct addrinfo* node_addr;
		getaddrinfo(node->ip.c_str(), node->port.c_str(), &hints, &node_addr);
		Utility::send(server_socket, cstr, node_addr->ai_addr, node_addr->ai_addrlen);
	}
}

/*
 *	Wait for all nodes to converge
 */
void wait_for_convergence()
{	
	#if PRINT_INFO == 1
		cout << "debug: wait for convergence" << endl;
	#endif

	for (int id = 1; id < topology.num_nodes; id++)
	{
		Link* node = &topology.matrix[id][id];
		struct addrinfo* node_addr;
		getaddrinfo(node->ip.c_str(), node->port.c_str(), &hints, &node_addr);
		Utility::receive(server_socket, buffer, BUFFER_SIZE,
			(struct sockaddr*)&node_addr, &node_addr->ai_addrlen);
	}
}

/*
 *
 */
 void* input_thread_handle(void* data)
 {
 	while (true)
 	{
 		Link link;

 		cout << "manager: enter topology change " << endl;
 		cout << "format: [source id] [target_id] [cost]" << endl;
 		cout << "\tcost - positive to add/update a link, negative to remove" << endl;

 		string change;
 		getline(cin, change);

 		// Parse stdin command
 		char* str = strdup(change.c_str());
 		char* token = strtok(str, " \n\0");
 		link.source_id = atoi(token);
 		token = strtok(NULL, " \n\0");
 		link.target_id = atoi(token);
 		token = strtok(NULL, " \n\0");
 		link.cost = atoi(token);
 		free(str);

 		Link* source = &topology.matrix[link.source_id][link.target_id];
 		Link* target = &topology.matrix[link.target_id][link.source_id];
 		source->source_id = link.source_id;
 		source->target_id = link.target_id;
 		source->cost = link.cost;
 		target->source_id = link.target_id;
 		target->target_id = link.source_id;
 		target->cost = link.cost;
 		struct addrinfo* source_addr;
		struct addrinfo* target_addr;
		string source_message;
		string target_message;

 		if (link.cost > 0)
 		{
 			// Add the new link
			source->cost = link.cost;
			target->cost = link.cost;	
			source_message = Topology::create_add_link_message(source);	
			target_message = Topology::create_add_link_message(target);
 		}
 		else
 		{
 			// Remove the link
 			source->cost = -1;
 			target->cost = -1;
 			source_message = Topology::create_remove_link_message(link.target_id);
 			target_message = Topology::create_remove_link_message(link.source_id);
 		}

 		// Send the nodes their node_messages
 		Link source_link = node_links[link.source_id];
 		Link target_link = node_links[link.target_id];
 		getaddrinfo(source_link.ip.c_str(), source_link.port.c_str(), &hints, &source_addr);
 		getaddrinfo(target_link.ip.c_str(), target_link.port.c_str(), &hints, &target_addr);
		Utility::send(server_socket, source_message.c_str(), source_addr->ai_addr, source_addr->ai_addrlen);
		Utility::send(server_socket, target_message.c_str(), target_addr->ai_addr, target_addr->ai_addrlen);

		#if PRINT_INFO == 1
			cout << "input_thread_handle: " << "source - " << source->ip;
			cout << ":" << source->port;
			cout << ", target - " << target->ip; 
			cout << ":" << source->port << endl;
		#endif

		// Send change message to every node to start building routing tables again
		for (int id = 1; id < topology.num_nodes; id++)
		{
			Link* node = &topology.matrix[id][id];
			string text = "change";
			const char* cstr = text.c_str();

			struct addrinfo* node_addr;
			getaddrinfo(node->ip.c_str(), node->port.c_str(), &hints, &node_addr);
			Utility::send(server_socket, cstr, node_addr->ai_addr, node_addr->ai_addrlen);
		}

		wait_for_convergence();

		send_node_messages();
 	}
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
	topology.parse_topology_file(TOPOLOGY_FILE);
	node_messages = Message::parse_message_file(MESSAGE_FILE);
	pthread_t input_thread;

	// Set up networking
	struct addrinfo *servinfo, *p;
	int rv;
	int numbytes;
	struct sockaddr_storage their_addr;
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
		if ((server_socket = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("manager: server_socket");
			continue;
		}

		if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
			close(server_socket);
			perror("manager: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "manager: failed to bind server_socket\n");
		return 2;
	}

	freeaddrinfo(servinfo);

	// Loop, waiting for connections
	while (true)
	{
		// Wait for new connections
		addr_len = sizeof(their_addr);
		Utility::receive(server_socket, buffer, BUFFER_SIZE, 
			(struct sockaddr *)&their_addr, &addr_len);

		// Assign the node a virtual ID
		int virtual_id = get_free_virtual_id();

		// Get its IP and assign port for neighbor communication
		const char* node_ip = inet_ntop(their_addr.ss_family,
			Utility::get_in_addr((struct sockaddr*)&their_addr), s, sizeof s);
		string node_port = Utility::int_to_string(
			ntohs(((struct sockaddr_in*)&their_addr)->sin_port));
	    
	    Link link;
	    link.source_id = -1;
	    link.ip = string(node_ip);
	    link.port = node_port;
	    node_links[virtual_id] = link;

		#if PRINT_INFO == 1 == true
			cout << "node id: " << virtual_id;
			cout << ", port: " << node_port << endl;
		#endif
		
		string assigned_port = Utility::int_to_string(START_PORT + virtual_id);
		topology.update_node_net_info(virtual_id, string(node_ip), assigned_port);

		const char* message = Utility::int_to_string(virtual_id).c_str();
		Utility::send(server_socket, message, 
			(struct sockaddr*)&their_addr, addr_len);

		// Wait for the node to bind to the new port
		Utility::receive(server_socket, buffer, BUFFER_SIZE, 
			(struct sockaddr*)&their_addr, &addr_len);

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

	for (map<int, Link>::iterator it = node_links.begin(); it != node_links.end(); it++)
	{
		// Send final information about links
		Link* self = &it->second;
		list<Link> links = topology.get_node_links(it->first);
		string links_string = Topology::serialize_node_links(links);
		const char* links_cstring = links_string.c_str();

		struct addrinfo* node_addr;
		#if PRINT_INFO == 1
			cout << "sending neighbor info to " << self->ip;
			cout << ":" << self->port << endl;
		#endif
		getaddrinfo(self->ip.c_str(), self->port.c_str(), &hints, &node_addr);
		Utility::send(server_socket, links_cstring, node_addr->ai_addr, node_addr->ai_addrlen);
	}

	pthread_create(&input_thread, NULL, input_thread_handle, NULL);

	wait_for_convergence();
	send_node_messages();

	pthread_join(input_thread, NULL);

	close(server_socket);
	return 0;
}