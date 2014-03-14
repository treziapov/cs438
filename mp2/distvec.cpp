#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <map>
#include "topology.h"
#include "utility.h"

#define START_PORT 20000
#define SERVER_PORT "6000"
#define BUFFER_SIZE 255

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
 * Converts the given vector for a given node into a message string
 */
string serialize_vector(int id, map<int, DistanceVectorLink>* neighbor_links)
{
	stringstream ss;
	ss << "node-" << id << ";";

	for (map<int, DistanceVectorLink>::iterator it = neighbor_links->begin(); it != neighbor_links->end(); it++)
	{
		ss << it->second.target_id << "-" << it->second.cost << ";";
	}

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
	return links;
}

/*
 *
 */
void update_neighbors(map<int, DistanceVectorLink>* neighbor_links, list<Link> links)
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
		(*neighbor_links)[it->target_id] = dv_link;
	}
}

/*
 *
 */
void initialize_distance_vector(map<int, DistanceVectorLink>* vector_map, map<int, DistanceVectorLink>* neighbor_links)
{
	for (map<int, DistanceVectorLink>::iterator it = neighbor_links->begin(); it != neighbor_links->end(); it++)
	{
		(*vector_map)[it->first] = it->second;
	}
}

/*
 * Updates the distance vector given neighbor's vector 
 */
bool update_distance_vector(int id, map<int, DistanceVectorLink>* vector_map, list<DistanceVectorLink> neighbor_vector)
{
	bool updated = false;
	DistanceVectorLink* link_to_neighbor;
	int current_cost;

	for (list<DistanceVectorLink>::iterator it = neighbor_vector.begin(); it != neighbor_vector.end(); it++)
	{
		link_to_neighbor = &(*vector_map)[it->source_id];
		current_cost = it->cost + link_to_neighbor->cost;

		// If the target wasn't reachable from this node before, add it
		if (vector_map->count(it->target_id) == 0)
		{
			DistanceVectorLink new_link;
			new_link.source_id = id;
			new_link.target_id = it->target_id;
			new_link.cost = current_cost;
			new_link.next_hop = it->source_id;

			(*vector_map)[it->target_id] = new_link;
			updated = true;
		}
		// If there is a shorter path to some node through this neighbor, update
		else if (current_cost < (*vector_map)[it->target_id].cost)
		{
			DistanceVectorLink* link = &(*vector_map)[it->target_id];
			link->cost = current_cost;
			link->next_hop = it->source_id;
			updated = true;
		}
	}

	return updated;
}

/*
 *
 */
void print_distance_vector(map<int, DistanceVectorLink>* vector_map)
{
	for (map<int, DistanceVectorLink>::iterator it = vector_map->begin(); it != vector_map->end(); it++)
	{
		cout << it->first << " " 
			<< it->second.next_hop << " " 
			<< it->second.cost << endl;
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

	int virtual_id;
	map<int, DistanceVectorLink> neighbor_links;
	map<int, DistanceVectorLink> vector_map;

	char * manager_host_name = argv[1];
	int manager_socket;
	int node_socket;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	struct sockaddr_storage their_addr;
	char buffer[BUFFER_SIZE];
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

	// Send connection message to the Manager
	sprintf(buffer, "connection");
	send(manager_socket, buffer, strlen(buffer), 0);

	// Wait for virtual id assignment from Manager
	if ((numbytes = recv(manager_socket, buffer, BUFFER_SIZE - 1 , 0)) == -1)
	{
		perror("recv");
		exit(1);
	}
	buffer[numbytes] = '\0';
	virtual_id = atoi(buffer);

	// Calculate the new port
	const char* port = Utility::int_to_string(START_PORT + virtual_id).c_str();
	getaddrinfo(NULL, port, &hints, &servinfo);	

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

	// Node binded to the new port, notify Manager and close old socket
	sprintf(buffer, "binded");
	send(manager_socket, buffer, strlen(buffer), 0);

	// Get information about node vector
	numbytes = recv(manager_socket, buffer, BUFFER_SIZE - 1 , 0);
	buffer[numbytes] = '\0';
	list<Link> links = Topology::deserialize_node_links(virtual_id, string(buffer));
	update_neighbors(&neighbor_links, links);

	// Get ip addresses and ports
	numbytes = recv(manager_socket, buffer, BUFFER_SIZE - 1 , 0);
	buffer[numbytes] = '\0';
	links = Topology::deserialize_node_links(virtual_id, string(buffer));
	update_neighbors(&neighbor_links, links);
	initialize_distance_vector(&vector_map, &neighbor_links);

	// Send your vector to neighbors and listen for theirs
	fd_set read_flags,write_flags; 		// the flag sets to be used
    struct timeval waitd = {10, 0};     // the max wait time for an event
    int sel;                      		// holds return value for select();
	int unchanged_counter = 0;

	while(true)
	{	
		FD_ZERO(&read_flags);
        FD_ZERO(&write_flags);
        FD_SET(manager_socket, &read_flags);
        FD_SET(node_socket, &read_flags);
        FD_SET(node_socket, &write_flags);

        sel = select(node_socket + 1, &read_flags, &write_flags, (fd_set*)0, &waitd);

        // If an error with select
        if(sel < 0)
        {
            continue;
        }

        // Socket ready for reading
        if (FD_ISSET(node_socket, &read_flags)) 
        {
            // Clear set
            FD_CLR(node_socket, &read_flags);
            
			numbytes = recv(node_socket, buffer, BUFFER_SIZE - 1 , 0);
			buffer[numbytes] = '\0';
			list<DistanceVectorLink> dv_links = deserialize_vector(virtual_id, string(buffer));
			bool updated =  update_distance_vector(virtual_id, &vector_map, dv_links); 

			if (updated == true)
			{
				unchanged_counter = 0;
			}
			else
			{
				if (++unchanged_counter == neighbor_links.size() * 5)
				{
					break;
				}
			}
        }

        // Socket ready for writing
        if (FD_ISSET(node_socket, &write_flags)) 
        {
            FD_CLR(node_socket, &write_flags);

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
				sendto(node_socket, links_cstring, strlen(links_cstring), 0,
						node_addr->ai_addr, node_addr->ai_addrlen);
			}
        }

		sleep(1);
	}

	struct addrinfo* server_addr;
	getaddrinfo(manager_host_name, SERVER_PORT, &hints, &server_addr);

	// Tell manager that you converged, and display your table
	sendto(node_socket, "converged", strlen("converged"), 0,
		server_addr->ai_addr, server_addr->ai_addrlen);
	print_distance_vector(&vector_map);


	// Clear neighbor announcements
	while (true)
	{
		numbytes = recv(node_socket, buffer, BUFFER_SIZE - 1 , 0);
		buffer[numbytes] = '\0';

		if (string(buffer).find("node") == string::npos)
		{
			break;
		}
	}

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

		struct addrinfo* node_addr;
		getaddrinfo(link.ip.c_str(), link.port.c_str(), &hints, &node_addr);
		sendto(node_socket, cstr, strlen(cstr), 0,
			node_addr->ai_addr, node_addr->ai_addrlen);
	}

	freeaddrinfo(servinfo);
	close(manager_socket);
	return 0;
}