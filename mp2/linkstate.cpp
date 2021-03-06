#include <stdlib.h>
#include <unistd.h>
#include <map>
#include <pthread.h>
#include "topology.h"
#include "utility.h"
#include <cstdio>
#include <ctime>
#include <queue>
#include <climits>

#define START_PORT 20000
#define SERVER_PORT "6000"
#define BUFFER_SIZE 255
#define PRINT_INFO 0

using namespace std;

struct LSA
{
	int node_id;
	string encoded_message;
};

class Node
{
public:
	int id;
	int distance;

	map<int, int> neighbor_costs;
	Node* parent;

	Node() 
	{
		parent = NULL;
		distance = INT_MAX;
	};

	int operator<(const Node& other) const
	{
		return distance > other.distance;
	}
};

class Graph
{
public:
	map<int, Node> nodes;

	bool add_link_nodes(list<Link> links)
	{
		#if PRINT_INFO == 1
			cout << "add_link_nodes: change";
		#endif
		bool changed = false;

		for (list<Link>::iterator it = links.begin(); it != links.end(); it++)
		{
			Link link = (*it);

			if (nodes.count(link.source_id) == 0)
			{
				Node new_node;
				new_node.id = link.source_id;

				nodes[link.source_id] = new_node;
				changed = true;
			}
			if (nodes[link.source_id].neighbor_costs.count(link.target_id) == 0)
			{
				nodes[link.source_id].neighbor_costs[link.target_id] = link.cost;
				changed = true;
			}

			if (nodes.count(link.target_id) == 0)
			{
				Node new_node;
				new_node.id = link.target_id;
				new_node.neighbor_costs[link.source_id] = link.cost;
				nodes[link.target_id] = new_node;
				changed = true;
			}
			if (nodes[link.target_id].neighbor_costs.count(link.source_id) == 0)
			{
				nodes[link.target_id].neighbor_costs[link.source_id] = link.cost;
				changed = true;
			}
		}

		#if PRINT_INFO == 1
			cout << " - " << (changed ? "yes" : "no") << endl;
		#endif
		return changed;
	};

	void clear()
	{
		#if PRINT_INFO == 1
			cout << "graph clear" << endl;
		#endif
		for (map<int, Node>::iterator it = nodes.begin(); it != nodes.end(); it++)
		{
			it->second.neighbor_costs.clear();
			it->second.parent = NULL;
		}
		nodes.clear();
	}

};

/*
 *	Globals
 */
int virtual_id;
int manager_socket;
int node_socket;
string previous_table = "";
map<int, Link> neighbor_links;
map<int, Link> topology_map;
map<int, list<int> > routing_table;
Graph graph;
list<LSA> lsa_to_send;

char buffer[BUFFER_SIZE];
bool neighbor_change = true;

struct addrinfo hints;
struct timeval waitd = {10, 0}; 

pthread_mutex_t neighbor_links_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t topology_map_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t neighbor_change_lock = PTHREAD_MUTEX_INITIALIZER;

priority_queue<Node> update_priority(priority_queue<Node>* pqueue, Node node)
{
	#if PRINT_INFO == 1
		cout << "update_priority: node";
	#endif

	priority_queue<Node> new_pqueue;
	while (!pqueue->empty())
	{
		Node curr = pqueue->top();
		pqueue->pop();

		if (curr.id == node.id)
		{
			new_pqueue.push(node);
			#if PRINT_INFO == 1
				cout << " - " << node.id;
				cout << ", changed from " << curr.distance;
				cout << " to " << node.distance << endl;
			#endif
		}
		else
		{
			new_pqueue.push(curr);
		}
	}

	return new_pqueue;
}

/*
 * Uses Djikstra's Algorithm
 */
void build_routing_table()
{
	#if PRINT_INFO == 1
		cout << "build_routing_table" << endl;
	#endif
	map<int, int> distance_map;
	priority_queue<Node> pqueue;

	// Initialize priority queue
	for (map<int, Node>::iterator it = graph.nodes.begin(); it != graph.nodes.end(); it++)
	{
		it->second.distance = INT_MAX;
		distance_map[it->first] = INT_MAX;
		pqueue.push(it->second);
	}

	// Initialize start node
	Node* node = &graph.nodes[virtual_id];
	distance_map[virtual_id] = 0;
	node->distance = 0;
	pqueue = update_priority(&pqueue, *node);

	while(!pqueue.empty())
	{
		Node curr = pqueue.top();
		pqueue.pop();

		#if PRINT_INFO == 1
			cout << "djikstra: current node - " << curr.id;
			cout << ", priority - " << curr.distance << endl;
		#endif

		if (distance_map[curr.id] == INT_MAX)
		{
			perror("djikstra: INFINITY distance encountered");
			break;
		}

		for (map<int, int>::iterator it = curr.neighbor_costs.begin(); it != curr.neighbor_costs.end(); it++)
		{
			int neighbor_id = it->first;
			int distance = it->second;
			int alternative = distance + distance_map[curr.id];

			if (alternative < distance_map[neighbor_id])
			{
				distance_map[neighbor_id] = alternative;
				graph.nodes[neighbor_id].distance = alternative;
				graph.nodes[neighbor_id].parent = &graph.nodes[curr.id];
				pqueue = update_priority(&pqueue, graph.nodes[neighbor_id]);
			}
		}
	}

	#if PRINT_INFO == 1
		cout << "build_routing_table: djikstra's finished" << endl;
	#endif

	for (map<int, list<int> >::iterator it = routing_table.begin(); it != routing_table.end(); it++)
	{
		it->second.clear();
	}

	Node* parent;
	for (map<int, Node>::iterator it = graph.nodes.begin(); it != graph.nodes.end(); it++)
	{
		if (it->first == virtual_id)
		{
			continue;
		}

		int target_id = it->first;
		parent = graph.nodes[target_id].parent;
		routing_table[target_id].push_front(target_id);

		while (parent != NULL)
		{
			routing_table[target_id].push_front(parent->id);
			parent = parent->parent;
		}
	}
}

void print_routing_table()
{
	#if PRINT_INFO == 1
		cout << "print_routing_table: size - " << routing_table.size() << endl;
	#endif

	stringstream ss;
	ss << endl;

	for (int i = 1; i <= routing_table.size() + 1; i++)
	{
		if (i == virtual_id)
		{
			ss << i << " 0: " << i << endl;
			continue;
		}

		ss << i << " " << graph.nodes[i].distance << ":";
		
		#if PRINT_INFO == 2
			cout << "print_routing_table: node - " << i;
			cout <<  ", table size - " << routing_table[i].size() << endl;
		#endif
		for (list<int>::iterator it = routing_table[i].begin(); it != routing_table[i].end(); it++)
		{
			ss << " " << (*it); 
		}

		ss << endl;
	}

	string table = ss.str();
	#if PRINT_INFO == 1
		cout << table << endl;
	#endif
	if (table != previous_table)
	{
		cout << table;
	}

	previous_table = table;
}


void clear_routing_table()
{
	#if PRINT_INFO == 1
		cout << "clear routing table" << endl;
	#endif
	for (map<int, list<int> >::iterator it = routing_table.begin(); it != routing_table.end(); it++)
	{
		it->second.clear();
	}
	routing_table.clear();
}

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
	#if PRINT_INFO == 1
		cout << "deserialize_lsa" << endl;
	#endif

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

			Topology::process_link_message(virtual_id, string(buffer), &neighbor_links);

			#if PRINT_INFO == 1		
				cout << "manager thread handle: message" << endl;
			#endif

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

	            string lsa = serialize_lsa(virtual_id, &neighbor_links);

	            for (map<int, Link>::iterator it = neighbor_links.begin(); it != neighbor_links.end(); it++)
	            {
	            	// Skip itself or missing ip addresses
	            	if (it->first == virtual_id || it->second.ip == "")
	            	{
	            		continue;
	            	}
	
					struct addrinfo* node_addr;
					getaddrinfo(it->second.ip.c_str(), it->second.port.c_str(), &hints, &node_addr);
					Utility::send(node_socket, lsa.c_str(), node_addr->ai_addr, node_addr->ai_addrlen);
				}
				pthread_mutex_unlock(&neighbor_links_mutex);

				// Send the received LSAs to all neighbors except the sender
				for (list<LSA>::iterator lsa_it = lsa_to_send.begin(); lsa_it != lsa_to_send.end(); lsa_it++)
				{
					int sender_id = lsa_it->node_id;

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
						Utility::send(node_socket, lsa_it->encoded_message.c_str(), node_addr->ai_addr, node_addr->ai_addrlen);
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
			
			topology_change = graph.add_link_nodes(links);

			LSA lsa = { links.front().source_id, string(buffer) };
			lsa_to_send.push_back(lsa);
        }
	}

	lsa_to_send.clear();
	build_routing_table();
	print_routing_table();

	// Tell manager that you converged, and display your table
	Utility::send(manager_socket, "converged", NULL, NULL);
}


void process_messages()
{
	#if PRINT_INFO == 1
		cout << "process_messages" << endl;
	#endif

	while (true) 
	{
		Utility::receive(node_socket, buffer, BUFFER_SIZE, NULL, NULL);
		if (string(buffer) == "start messages")
		{
			break;
		}
		else 
		{
			continue;
		}
	}
	
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

			if (routing_table.size() < 2)
			{
				perror("routing table is too short");
				exit(1);
			}

			list<int>::iterator it = routing_table[m.target_id].begin();
			it++;

			pthread_mutex_lock(&neighbor_links_mutex);
			Link link = neighbor_links[*it];
			pthread_mutex_unlock(&neighbor_links_mutex);

			#if PRINT_INFO == 1
				cout << "process_messages: passing message to: " << link.target_id << endl;
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
		if (it->target_id == virtual_id) continue;

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
			#if PRINT_INFO == 1
				cout << "neighbor change" << endl;
			#endif
			process_routing();
			process_messages();
			graph.clear();
		}
		else
		{
			#if PRINT_INFO == 1
				cout << "no neighbor change" << endl;
			#endif
		}

		sleep(1);
	}

	pthread_join(manager_thread, NULL);

	freeaddrinfo(servinfo);
	close(manager_socket);
	return 0;
}
