#include <stdio.h>
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
#define DEBUG true

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
struct timeval waitd = {5, 0}; 

pthread_mutex_t neighbor_links_mutex;
pthread_mutex_t vector_map_mutex;
pthread_mutex_t neighbor_change_lock;

/*
 * Converts the given vector for a given node into a message string
 */
string serialize_vector(int id, map<int, DistanceVectorLink>* links)
{
	stringstream ss;
	ss << "node-" << id << ";";

	for (map<int, DistanceVectorLink>::iterator it = links->begin(); it != links->end(); it++)
	{
		if (it->second.cost == -1)
		{
			continue;
		}

		ss << it->second.target_id << "-" << it->second.cost << ";";
	}

	#ifdef DEBUG
		cout << "serialize_vector: " << ss.str() << endl;
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

	#ifdef DEBUG
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

	#ifdef DEBUG
		cout << "updated_neighbors" << endl;
	#endif
}

/*
 *
 */
void initialize_distance_vector(map<int, DistanceVectorLink>* link_map, map<int, DistanceVectorLink>* neighbor_map)
{
	for (map<int, DistanceVectorLink>::iterator it = neighbor_map->begin(); it != neighbor_map->end(); it++)
	{
		(*link_map)[it->first] = it->second;
	}

	#ifdef DEBUG
		cout << "initialize_distance_vector" << endl;
	#endif
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
		if (link_map->count(it->target_id) <= 0 || (*link_map)[it->target_id].cost == -1)
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

	#ifdef DEBUG
		cout << "update_distance_vector: update - " << (updated ? "yes" : "no") << endl;
	#endif
	return updated;
}

/*
 * 	Print the current distance vector for the given map
 */
void print_distance_vector(map<int, DistanceVectorLink>* link_map)
{
	#ifdef DEBUG
		cout << "print_distance_vector" << endl;
	#endif

	for (map<int, DistanceVectorLink>::iterator it = link_map->begin(); it != link_map->end(); it++)
	{
		cout << it->first << " "; 
		cout << it->second.next_hop << " ";
		cout << it->second.cost << endl;
	}	
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
            
			// Get information about node vector
			Utility::receive(manager_socket, buffer, BUFFER_SIZE, NULL, NULL);

			pthread_mutex_lock(&neighbor_links_mutex);
			Topology::process_link_message(virtual_id, string(buffer), (map<int, Link>*)&neighbor_links);
			pthread_mutex_unlock(&neighbor_links_mutex);

			pthread_mutex_lock(&neighbor_change_lock);
			neighbor_change = true;
			pthread_mutex_unlock(&neighbor_change_lock);

			pthread_mutex_lock(&vector_map_mutex);
			initialize_distance_vector(&vector_map, &neighbor_links);
			pthread_mutex_unlock(&vector_map_mutex);
        }
	}
}

void process_routing()
{
	#ifdef DEBUG
		cout << "process_routing" << endl;
	#endif

	// Send your vector to neighbors and listen for theirs
	fd_set read_flags,write_flags; 		
    int sel;                      		// holds return value for select();
	
    double duration;
    bool distance_vector_change = true;
    clock_t start = clock();

	while(true)
	{	
		duration = (clock() - start) / (double) CLOCKS_PER_SEC;

		if (duration >= 10.0)
		{
			#ifdef DEBUG
				cout << "process_routing: no changes for 10 seconds" << endl;
			#endif
			break;
		}

		FD_ZERO(&read_flags);
        FD_ZERO(&write_flags);
        FD_SET(node_socket, &read_flags);
        FD_SET(node_socket, &write_flags);

        sel = select(node_socket + 1, &read_flags, &write_flags, (fd_set*)0, &waitd);

        // If an error with select
        if(sel < 0)
        {
            continue;
        }

        // Socket ready for writing
        if (FD_ISSET(node_socket, &write_flags) && distance_vector_change == true) 
        {
            FD_CLR(node_socket, &write_flags);

            pthread_mutex_lock(&neighbor_links_mutex);
            
            for (map<int, DistanceVectorLink>::iterator it = neighbor_links.begin(); it != neighbor_links.end(); it++)
            {
            	// Skip itself
            	if (it->first == virtual_id)
            	{
            		continue;
            	}

	    		DistanceVectorLink link = it->second;
	    		string links_string = serialize_vector(virtual_id, &neighbor_links);
				const char* links_cstring = links_string.c_str();
				
				struct addrinfo* node_addr;
				getaddrinfo(link.ip.c_str(), link.port.c_str(), &hints, &node_addr);
				Utility::send(node_socket, links_cstring, node_addr->ai_addr, node_addr->ai_addrlen);
			}

			pthread_mutex_unlock(&neighbor_links_mutex);
        }

        // Socket ready for reading
        if (FD_ISSET(node_socket, &read_flags)) 
        {
            // Clear set
            FD_CLR(node_socket, &read_flags);
            
			Utility::receive(node_socket, buffer, BUFFER_SIZE, NULL, NULL);
			list<DistanceVectorLink> dv_links = deserialize_vector(virtual_id, string(buffer));

			pthread_mutex_lock(&vector_map_mutex);
			distance_vector_change = update_distance_vector(virtual_id, &vector_map, dv_links); 
			pthread_mutex_unlock(&vector_map_mutex);
        }
	}

	// Tell manager that you converged, and display your table
	Utility::send(manager_socket, "converged", NULL, NULL);
	print_distance_vector(&vector_map);
}


void process_messages()
{
	#ifdef DEBUG
		cout << "process_messages" << endl;
	#endif

	Message m = Message::deserialize(string(buffer));
	cout <<  m.to_string() << endl;

	// Pass the message along if this node is not the target
	if (m.target_id != virtual_id)
	{
		m.append_sender(virtual_id);
		string text = Message::serialize(m);
		const char* cstr = text.c_str();

		int hop = vector_map[m.target_id].next_hop;
		DistanceVectorLink link = neighbor_links[hop];

		#ifdef DEBUG
			cout << "process_messages: passing message to: " << hop << endl;
		#endif

		struct addrinfo* node_addr;
		getaddrinfo(link.ip.c_str(), link.port.c_str(), &hints, &node_addr);
		Utility::send(node_socket, cstr, node_addr->ai_addr, node_addr->ai_addrlen);
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

	#ifdef DEBUG
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

	#ifdef DEBUG
		cout << "node id: " << virtual_id;
		cout << ", port: " << port << endl;
	#endif

	// Loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
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

	// Get information about node vector
	numbytes = Utility::receive(manager_socket, buffer, BUFFER_SIZE, NULL, NULL);
	list<Link> links = Topology::deserialize_node_links(virtual_id, string(buffer));
	update_neighbors(&neighbor_links, links);

	// Get ip addresses and ports
	Utility::receive(manager_socket, buffer, BUFFER_SIZE, NULL, NULL);
	links = Topology::deserialize_node_links(virtual_id, string(buffer));
	update_neighbors(&neighbor_links, links);

	initialize_distance_vector(&vector_map, &neighbor_links);

	pthread_create(&manager_thread, NULL, manager_thread_handle, NULL);

	while (true)
	{
		bool neighbor_change_flag;
		pthread_mutex_lock(&neighbor_change_lock);
		neighbor_change_flag = neighbor_change;
		neighbor_change = false;
		pthread_mutex_unlock(&neighbor_change_lock);

		if (neighbor_change_flag == true)
		{
			process_routing();

			process_messages();
		}
		else
		{
			sleep(1);
		}
	}

	pthread_join(manager_thread, NULL);

	freeaddrinfo(servinfo);
	close(manager_socket);
	return 0;
}