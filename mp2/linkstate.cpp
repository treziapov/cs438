#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <map>
#include <pthread.h>
#include "topology.h"
#include "utility.h"
#include <cstdio>
#include <ctime>
#include <queue>

#define START_PORT 20000
#define SERVER_PORT "6000"
#define BUFFER_SIZE 255
#define PRINT_INFO 0

using namespace std;

class DistanceVectorLink : public Link
{
	public:
		int next_hop;	

		DistanceVectorLink() : Link()
		{
			next_hop = -1;
		};
};

/*
 *	Globals
 */
int virtual_id;
int manager_socket;
int node_socket;
map<int, DistanceVectorLink> neighbor_links;
map<int, DistanceVectorLink> vector_map;
char buffer[BUFFER_SIZE];
bool neighbor_change = true;

struct addrinfo hints;
struct timeval waitd = {10, 0}; 

pthread_mutex_t neighbor_links_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t vector_map_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t neighbor_change_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Converts the given vector for a given node into a message string
 */
string serialize_vector(int id, map<int, DistanceVectorLink>* links)
{
	stringstream ss;
	ss << "node-" << id << ";";

	for (map<int, DistanceVectorLink>::iterator it = links->begin(); it != links->end(); it++)
	{
		if (it->second.cost <= -1)
		{
			continue;
		}

		ss << it->second.target_id << "-" << it->second.cost << ";";
	}

	#if PRINT_INFO == 1
		cout << endl << "serialize_vector: " << ss.str() << endl << endl;
	#endif
	return ss.str();
}

/*
 * Converts the given vector for a given node into a message string
 */
list<DistanceVectorLink> deserialize_vector(int id, string vector_string)
{
	list<DistanceVectorLink> links;

	DistanceVectorLink link;
	char* str = strdup(vector_string.c_str());
	char* token = strtok(str, "- ;");	
	token = strtok(NULL, "- ;");
	int neighbor_id = atoi(token);
	link.source_id = neighbor_id;
	token = strtok(NULL, "- ;");

	while(token != NULL)
	{
		link.target_id = atoi(token);
		token = strtok(NULL, "- ;");
		link.cost = atoi(token);
		token = strtok(NULL, "- ;");
		links.push_back(link);
	}
	
	free(str);

	#if PRINT_INFO == 1
		cout << "deserialize_vector: string - " << vector_string;
		cout << ", result list size - " << links.size() << endl;
	#endif
	return links;
}

/*
 *
 */
void update_neighbors(map<int, DistanceVectorLink>* link_map, list<Link> links)
{
	DistanceVectorLink dv_link;
	for (list<Link>::iterator it = links.begin(); it != links.end(); it++)
	{
		dv_link.source_id = it->source_id;
		dv_link.target_id = it->target_id;
		dv_link.cost = it->cost;
		dv_link.ip = it->ip;
		dv_link.port = it->port;
		dv_link.next_hop = it->target_id;
		(*link_map)[it->target_id] = dv_link;
	}

	#if PRINT_INFO == 1
		cout << "updated_neighbors: ";
		for (map<int, DistanceVectorLink>::iterator it = link_map->begin(); it != link_map->end(); it++)
		{
			cout << it->first << " ";
		}
		cout << endl;
	#endif
}

/*
 *
 */
void initialize_distance_vector(map<int, DistanceVectorLink>* link_map, map<int, DistanceVectorLink>* neighbor_map)
{
	link_map->clear();

	for (map<int, DistanceVectorLink>::iterator it = neighbor_map->begin(); it != neighbor_map->end(); it++)
	{
		(*link_map)[it->first] = it->second;
	}

	#if PRINT_INFO == 1
		cout << "initialize_distance_vector" << endl;
	#endif
}

/*
 * 	Print the current distance vector for the given map
 */
void print_distance_vector(map<int, DistanceVectorLink>* link_map)
{
	#if PRINT_INFO == 1
		cout << "print_distance_vector" << endl;
	#endif
	cout << endl;
	for (map<int, DistanceVectorLink>::iterator it = link_map->begin(); it != link_map->end(); it++)
	{
		cout << it->first << " "; 
		cout << it->second.next_hop << " ";
		cout << it->second.cost << endl;
	}	
	cout << endl;
}

/*
 * Updates the distance vector given neighbor's vector 
 */
bool update_distance_vector(int id, map<int, DistanceVectorLink>* link_map, list<DistanceVectorLink> neighbor_vector)
{
	bool updated = false;
	DistanceVectorLink* link_to_neighbor;
	int current_cost;

	for (list<DistanceVectorLink>::iterator it = neighbor_vector.begin(); it != neighbor_vector.end(); it++)
	{
		link_to_neighbor = &(*link_map)[it->source_id];
		current_cost = it->cost + link_to_neighbor->cost;

		// If the target wasn't reachable from this node before, add it
		if (link_map->count(it->target_id) <= 0 || (*link_map)[it->target_id].cost <= -1)
		{
			DistanceVectorLink new_link;
			new_link.source_id = id;
			new_link.target_id = it->target_id;
			new_link.cost = current_cost;
			new_link.next_hop = it->source_id;

			(*link_map)[it->target_id] = new_link;
			updated = true;
		}
		// If there is a shorter path to some node through this neighbor, update
		else if (current_cost < (*link_map)[it->target_id].cost)
		{
			DistanceVectorLink* link = &(*link_map)[it->target_id];
			link->cost = current_cost;
			link->next_hop = it->source_id;
			updated = true;
		}
	}

	#if PRINT_INFO == 1
		cout << "update_distance_vector: update - " << (updated ? "yes" : "no") << endl;
		print_distance_vector(&vector_map);
	#endif
	return updated;
}

/*
 *
 */
void* manager_thread_handle(void* data)
{
	fd_set read_manager_flags;

	while (true)
	{

		FD_ZERO(&read_manager_flags);
        FD_SET(manager_socket, &read_manager_flags);

        int sel = select(manager_socket + 1, &read_manager_flags, (fd_set*)0, (fd_set*)0, &waitd);

        // If an error with select
        if (sel < 0)
        {
        	perror("select error");
           	continue;
        }

        // Socket ready for reading
        if (FD_ISSET(manager_socket, &read_manager_flags)) 
        {
            // Clear set
            FD_CLR(manager_socket, &read_manager_flags);
            
			Utility::receive(manager_socket, buffer, BUFFER_SIZE, NULL, NULL);

			pthread_mutex_lock(&neighbor_change_lock);
			neighbor_change = true;
			pthread_mutex_unlock(&neighbor_change_lock);

			pthread_mutex_lock(&neighbor_links_mutex);
			Topology::process_link_message(virtual_id, string(buffer), (map<int, Link>*)&neighbor_links);
			pthread_mutex_unlock(&neighbor_links_mutex);
        }
	}
}

void process_routing()
{
	#if PRINT_INFO == 1
		cout << "process_routing" << endl;
	#endif

	// Send your vector to neighbors and listen for theirs
	fd_set read_flags,write_flags; 		
    int sel;                      		// holds return value for select();
	
    double duration;
    bool distance_vector_change = true;
    clock_t start;

	while(true)
	{	
		if (distance_vector_change == true)
		{
			start = clock();
		}

		duration = (clock() - start) / (double)CLOCKS_PER_SEC;

		if (duration >= 5.0)
		{
			#if PRINT_INFO == 1
				cout << "process_routing: no changes for 5 seconds" << endl;
			#endif
			break;
		}
		FD_ZERO(&read_flags);
        FD_ZERO(&write_flags);
        FD_SET(node_socket, &read_flags);
        FD_SET(node_socket, &write_flags);

        sel = select(node_socket + 1, &read_flags, &write_flags, (fd_set*)0, &waitd);

        // If an error with select
        if (sel < 0)
        {
            continue;
        }

        // Select Timed out
        if (sel == 0)
        {
        	break;
        }

        // Socket ready for writing
        if (FD_ISSET(node_socket, &write_flags)) 
        {
            FD_CLR(node_socket, &write_flags);

            if (distance_vector_change == true)
            {
            	distance_vector_change = false;

	            pthread_mutex_lock(&neighbor_links_mutex);
	            for (map<int, DistanceVectorLink>::iterator it = neighbor_links.begin(); it != neighbor_links.end(); it++)
	            {
	            	// Skip itself or missing ip addresses
	            	if (it->first == virtual_id || it->second.ip == "")
	            	{
	            		continue;
	            	}

		    		DistanceVectorLink link = it->second;
		    		string links_string = serialize_vector(virtual_id, &vector_map);
					const char* links_cstring = links_string.c_str();
					
					struct addrinfo* node_addr;
					getaddrinfo(link.ip.c_str(), link.port.c_str(), &hints, &node_addr);
					Utility::send(node_socket, links_cstring, node_addr->ai_addr, node_addr->ai_addrlen);
				}
				pthread_mutex_unlock(&neighbor_links_mutex);
			}
        }

        // Socket ready for reading
        if (FD_ISSET(node_socket, &read_flags)) 
        {
            // Clear set
            FD_CLR(node_socket, &read_flags);
            
			Utility::receive(node_socket, buffer, BUFFER_SIZE, NULL, NULL);
			list<DistanceVectorLink> dv_links = deserialize_vector(virtual_id, string(buffer));

			distance_vector_change = update_distance_vector(virtual_id, &vector_map, dv_links); 
        }
	}

	// Tell manager that you converged, and display your table
	Utility::send(manager_socket, "converged", NULL, NULL);
	print_distance_vector(&vector_map);
}


void process_messages()
{
	#if PRINT_INFO == 1
		cout << "process_messages" << endl;
	#endif

	while (true)
	{
		Utility::receive(node_socket, buffer, BUFFER_SIZE, NULL, NULL);

		if (string(buffer) == "change")
		{
			pthread_mutex_lock(&neighbor_change_lock);
			neighbor_change = true;
			pthread_mutex_unlock(&neighbor_change_lock);
			break;
		}

		Message m = Message::deserialize(string(buffer));
		m.append_sender(virtual_id);
		cout << m.to_string() << endl;

		// Pass the message along if this node is not the target
		if (m.target_id != virtual_id)
		{

			string text = Message::serialize(m);
			const char* cstr = text.c_str();

			int hop = vector_map[m.target_id].next_hop;

			pthread_mutex_lock(&neighbor_links_mutex);
			DistanceVectorLink link = neighbor_links[hop];
			pthread_mutex_unlock(&neighbor_links_mutex);
			

			#if PRINT_INFO == 1
				cout << "process_messages: passing message to: " << hop << endl;
			#endif

			struct addrinfo* node_addr;
			getaddrinfo(link.ip.c_str(), link.port.c_str(), &hints, &node_addr);
			Utility::send(node_socket, cstr, node_addr->ai_addr, node_addr->ai_addrlen);
		}
	}
}

/*
 * Communicates with the Manager and neighbors only
 * Waits for convergence before sending messages
 * Run as:
 * 	./distvec [manager host name]
 */
int main (int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("usage: ./distvec [manager host name]");
		return 1;
	}

	pthread_t manager_thread;

	char* manager_host_name = argv[1];
	
	struct addrinfo *servinfo, *p;
	int rv;
	int numbytes;
	struct sockaddr_storage their_addr;
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];

	// Set to AF_INET to force IPv4, and use my IP
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; 
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; 

	if ((rv = getaddrinfo(manager_host_name, SERVER_PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// Make a socket
	if ((manager_socket = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1) {
		perror("distvec: socket");
		exit(1);
	}

	// Connect to the Manager
	if (connect(manager_socket, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
	{
		perror("distvec: connect");
		exit(1);
	}	

	#if PRINT_INFO == 1
		Utility::get_listening_port(manager_socket);
	#endif

	// Send connection message to the Manager
	strncpy(buffer, "connection\0", sizeof("connection\0"));
	Utility::send(manager_socket, buffer, NULL, NULL);

	// Wait for virtual id assignment from Manager
	numbytes = Utility::receive(manager_socket, buffer, BUFFER_SIZE, 
			NULL, NULL);
	if (numbytes == -1)
	{
		perror("recv");
		exit(1);
	}
	virtual_id = atoi(buffer);
	
	// Calculate the new port
	string port = Utility::int_to_string(START_PORT + virtual_id);
	getaddrinfo(NULL, port.c_str(), &hints, &servinfo);	

	#if PRINT_INFO == 1
		cout << "node id: " << virtual_id;
		cout << ", port: " << port << endl;
	#endif

	// Loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((node_socket = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("manager: socket");
			continue;
		}

		if (bind(node_socket, p->ai_addr, p->ai_addrlen) == -1) {
			close(node_socket);
			perror("manager: bind");
			continue;
		}

		break;
	}

	// Node binded to the new port, notify Manager
	strncpy(buffer, "binded\0", sizeof("binded\0"));
	Utility::send(manager_socket, buffer, NULL, NULL);

	// Get ip addresses and ports
	Utility::receive(manager_socket, buffer, BUFFER_SIZE, NULL, NULL);
	list<Link> links = Topology::deserialize_node_links(virtual_id, string(buffer));
	update_neighbors(&neighbor_links, l#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <map>
#include <pthread.h>
#include "topology.h"
#include "utility.h"
#include <cstdio>
#include <ctime>

#define START_PORT 20000
#define SERVER_PORT "6000"
#define BUFFER_SIZE 255
#define PRINT_INFO 0

using namespace std;

struct LSA
{
	int node_id;
	string ecoded_message;
};
/*
 *	Globals
 */
int virtual_id;
int manager_socket;
int node_socket;
map<int, Link> neighbor_links;
map<int, Link> topology_map;
map<int, list<Link> routing_table;
list<LSA> lsa_to_send;

char buffer[BUFFER_SIZE];
bool neighbor_change = true;

struct addrinfo hints;
struct timeval waitd = {10, 0}; 

pthread_mutex_t neighbor_links_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t topology_map_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t neighbor_change_lock = PTHREAD_MUTEX_INITIALIZER;


void build_routing_table(map<int, Link> link_map)
{

}

print_routing_table(&topology_map);

/*
 * Converts the given vector for a given node into a message string
 */
string serialize_lsa(int id, map<int, Link>* links)
{
	stringstream ss;
	ss << "node-" << id << ";";

	for (map<int, Link>::iterator it = links->begin(); it != links->end(); it++)
	{
		if (it->second.cost <= -1)
		{
			continue;
		}

		ss << it->second.target_id << "-" << it->second.cost << ";";
	}

	#if PRINT_INFO == 1
		cout << "serialize_lsa: " << ss.str() << endl;
	#endif
	return ss.str();
}

/*
 * Converts the given vector for a given node into a message string
 */
list<Link> deserialize_lsa(int id, string vector_string)
{
	list<Link> links;
	Link link;
	char* str = strdup(vector_string.c_str());
	char* token = strtok(str, "- ;");	
	token = strtok(NULL, "- ;");
	int neighbor_id = atoi(token);
	link.source_id = neighbor_id;
	token = strtok(NULL, "- ;");

	while(token != NULL)
	{
		link.target_id = atoi(token);
		token = strtok(NULL, "- ;");
		link.cost = atoi(token);
		token = strtok(NULL, "- ;");
		links.push_back(link);
	}
	
	free(str);

	#if PRINT_INFO == 1
		cout << "deserialize_lsa: string - " << vector_string;
		cout << ", result list size - " << links.size() << endl;
	#endif
	return links;
}

/*
 *
 */
void update_neighbors(map<int, Link>* link_map, list<Link> links)
{
	Link link;
	for (list<Link>::iterator it = links.begin(); it != links.end(); it++)
	{
		link.source_id = it->source_id;
		link.target_id = it->target_id;
		link.cost = it->cost;
		link.ip = it->ip;
		link.port = it->port;
		link.next_hop = it->target_id;
		(*link_map)[it->target_id] = link;
	}

	#if PRINT_INFO == 1
		cout << "updated_neighbors: ";
		for (map<int, Link>::iterator it = link_map->begin(); it != link_map->end(); it++)
		{
			cout << it->first << " ";
		}
		cout << endl;
	#endif
}

/*
 *
 */
void* manager_thread_handle(void* data)
{
	fd_set read_manager_flags;

	while (true)
	{

		FD_ZERO(&read_manager_flags);
        FD_SET(manager_socket, &read_manager_flags);

        int sel = select(manager_socket + 1, &read_manager_flags, (fd_set*)0, (fd_set*)0, &waitd);

        // If an error with select
        if (sel < 0)
        {
        	perror("select error");
           	continue;
        }

        // Socket ready for reading
        if (FD_ISSET(manager_socket, &read_manager_flags)) 
        {
            // Clear set
            FD_CLR(manager_socket, &read_manager_flags);
            
			Utility::receive(manager_socket, buffer, BUFFER_SIZE, NULL, NULL);

			pthread_mutex_lock(&neighbor_change_lock);
			neighbor_change = true;
			pthread_mutex_unlock(&neighbor_change_lock);

			pthread_mutex_lock(&neighbor_links_mutex);
			Topology::process_link_message(virtual_id, string(buffer), (map<int, Link>*)&neighbor_links);
			pthread_mutex_unlock(&neighbor_links_mutex);
        }
	}
}

void process_routing()
{
	#if PRINT_INFO == 1
		cout << "process_routing" << endl;
	#endif

	// Send your vector to neighbors and listen for theirs
	fd_set read_flags,write_flags; 		
    int sel;                      		// holds return value for select();
	
    double duration;
    bool topology_change = true;
    clock_t start;

	while(true)
	{	
		if (topology_change == true)
		{
			start = clock();
		}

		duration = (clock() - start) / (double)CLOCKS_PER_SEC;

		if (duration >= 5.0)
		{
			#if PRINT_INFO == 1
				cout << "process_routing: no changes for 5 seconds" << endl;
			#endif
			break;
		}

		FD_ZERO(&read_flags);
        FD_ZERO(&write_flags);
        FD_SET(node_socket, &read_flags);
        FD_SET(node_socket, &write_flags);

        sel = select(node_socket + 1, &read_flags, &write_flags, (fd_set*)0, &waitd);

        // If an error with select
        if (sel < 0)
        {
            continue;
        }

        // Select Timed out
        if (sel == 0)
        {
        	break;
        }

        // Socket ready for writing
        if (FD_ISSET(node_socket, &write_flags)) 
        {
            FD_CLR(node_socket, &write_flags);

            if (topology_change == true)
            {
            	topology_change = false;

            	// Send your LSA to neighbors
	            pthread_mutex_lock(&neighbor_links_mutex);
	            for (map<int, Link>::iterator it = neighbor_links.begin(); it != neighbor_links.end(); it++)
	            {
	            	// Skip itself or missing ip addresses
	            	if (it->first == virtual_id || it->second.ip == "")
	            	{
	            		continue;
	            	}

	            	string lsa = serialize_lsa(&neighbor_links);
					
					struct addrinfo* node_addr;
					getaddrinfo(it->second.ip.c_str(), it->second.port.c_str(), &hints, &node_addr);
					Utility::send(node_socket, lsa.c_str(), node_addr->ai_addr, node_addr->ai_addrlen);
				}
				pthread_mutex_unlock(&neighbor_links_mutex);

				// Send the received LSAs to all neighbors except the sender
				for (list<LSA>::iterator it = lsa_to_send.begin(); it != lsa_to_send.end(); it++)
				{
					int sender_id = it->node_id;

					pthread_mutex_lock(&neighbor_links_mutex);
		            for (map<int, Link>::iterator it = neighbor_links.begin(); it != neighbor_links.end(); it++)
		            {
		            	// Skip itself or missing ip addresses
		            	if (it->first == virtual_id || it->first == sender_id || it->second.ip == "")
		            	{
		            		continue;
		            	}
						
						struct addrinfo* node_addr;
						getaddrinfo(it->second.ip.c_str(), it->second.port.c_str(), &hints, &node_addr);
						Utility::send(node_socket, lsa.c_str(), node_addr->ai_addr, node_addr->ai_addrlen);
					}
					pthread_mutex_unlock(&neighbor_links_mutex);
				}
				lsa_to_send.clear();
			}
        }

        // Socket ready for reading
        if (FD_ISSET(node_socket, &read_flags)) 
        {
            // Clear set
            FD_CLR(node_socket, &read_flags);
            
			Utility::receive(node_socket, buffer, BUFFER_SIZE, NULL, NULL);
			list<Link> links = deserialize_lsa(virtual_id, string(buffer));
			topology_change = update_link_map(virtual_id, &topology_map, links); 

			LSA lsa = { links.front().source_id, string(buffer) };
			lsa_to_send.push_back(lsa);
        }
	}

	// Tell manager that you converged, and display your table
	Utility::send(manager_socket, "converged", NULL, NULL);
	build_routing_table(&topology_map);
	print_routing_table(&topology_map);
}


void process_messages()
{
	#if PRINT_INFO == 1
		cout << "process_messages" << endl;
	#endif

	while (true)
	{
		Utility::receive(node_socket, buffer, BUFFER_SIZE, NULL, NULL);

		if (string(buffer) == "change")
		{
			pthread_mutex_lock(&neighbor_change_lock);
			neighbor_change = true;
			pthread_mutex_unlock(&neighbor_change_lock);
			break;
		}

		Message m = Message::deserialize(string(buffer));
		m.append_sender(virtual_id);
		cout << m.to_string() << endl;

		// Pass the message along if this node is not the target
		if (m.target_id != virtual_id)
		{

			string text = Message::serialize(m);
			const char* cstr = text.c_str();

			Link link = routing_table[m.target_id].front();

			#if PRINT_INFO == 1
				cout << "process_messages: passing message to: " << hop << endl;
			#endif

			struct addrinfo* node_addr;
			getaddrinfo(link.ip.c_str(), link.port.c_str(), &hints, &node_addr);
			Utility::send(node_socket, cstr, node_addr->ai_addr, node_addr->ai_addrlen);
		}
	}
}

/*
 * Communicates with the Manager and neighbors only
 * Waits for convergence before sending messages
 * Run as:
 * 	./distvec [manager host name]
 */
int main (int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("usage: ./distvec [manager host name]");
		return 1;
	}

	pthread_t manager_thread;

	char* manager_host_name = argv[1];
	
	struct addrinfo *servinfo, *p;
	int rv;
	int numbytes;
	struct sockaddr_storage their_addr;
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];

	// Set to AF_INET to force IPv4, and use my IP
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; 
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; 

	if ((rv = getaddrinfo(manager_host_name, SERVER_PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// Make a socket
	if ((manager_socket = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1) {
		perror("distvec: socket");
		exit(1);
	}

	// Connect to the Manager
	if (connect(manager_socket, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
	{
		perror("distvec: connect");
		exit(1);
	}	

	#if PRINT_INFO == 1
		Utility::get_listening_port(manager_socket);
	#endif

	// Send connection message to the Manager
	strncpy(buffer, "connection\0", sizeof("connection\0"));
	Utility::send(manager_socket, buffer, NULL, NULL);

	// Wait for virtual id assignment from Manager
	numbytes = Utility::receive(manager_socket, buffer, BUFFER_SIZE, 
			NULL, NULL);
	if (numbytes == -1)
	{
		perror("recv");
		exit(1);
	}
	virtual_id = atoi(buffer);
	
	// Calculate the new port
	string port = Utility::int_to_string(START_PORT + virtual_id);
	getaddrinfo(NULL, port.c_str(), &hints, &servinfo);	

	#if PRINT_INFO == 1
		cout << "node id: " << virtual_id;
		cout << ", port: " << port << endl;
	#endif

	// Loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((node_socket = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("manager: socket");
			continue;
		}

		if (bind(node_socket, p->ai_addr, p->ai_addrlen) == -1) {
			close(node_socket);
			perror("manager: bind");
			continue;
		}

		break;
	}

	// Node binded to the new port, notify Manager
	strncpy(buffer, "binded\0", sizeof("binded\0"));
	Utility::send(manager_socket, buffer, NULL, NULL);

	// Get ip addresses and ports
	Utility::receive(manager_socket, buffer, BUFFER_SIZE, NULL, NULL);
	list<Link> links = Topology::deserialize_node_links(virtual_id, string(buffer));
	update_neighbors(&neighbor_links, links);
	for (list<Link>::iterator it = links.begin(); it != links.end(); it++)
	{
		cout << "now linked to node " << it->target_id;
		cout << " with cost " << it->cost << endl; 
	}

	pthread_create(&manager_thread, NULL, manager_thread_handle, NULL);

	while (true)
	{
		pthread_mutex_lock(&neighbor_change_lock);
		bool flag = neighbor_change;
		neighbor_change = false;
		pthread_mutex_unlock(&neighbor_change_lock);

		if (flag)
		{
			process_routing();

			process_messages();
		}
	}

	pthread_join(manager_thread, NULL);

	freeaddrinfo(servinfo);
	close(manager_socket);
	return 0;
}inks);
	for (list<Link>::iterator it = links.begin(); it != links.end(); it++)
	{
		cout << "now linked to node " << it->target_id;
		cout << " with cost " << it->cost << endl; 
	}

	pthread_create(&manager_thread, NULL, manager_thread_handle, NULL);


	while (true)
	{
		pthread_mutex_lock(&neighbor_change_lock);
		bool flag = neighbor_change;
		neighbor_change = false;
		pthread_mutex_unlock(&neighbor_change_lock);

		if (flag)
		{
			pthread_mutex_lock(&neighbor_links_mutex);
			initialize_distance_vector(&vector_map, &neighbor_links);
			pthread_mutex_unlock(&neighbor_links_mutex);

			process_routing();

			process_messages();
		}
	}

	pthread_join(manager_thread, NULL);

	freeaddrinfo(servinfo);
	close(manager_socket);
	return 0;
}