#include <stdio.h>
#include <iostream>
#include <string>
#include <fstream>
#include <list>
#include <sstream>
#include <map>

#define PRINT_INFO 0

using namespace std;

class Message
{
	public:
		int source_id;
		int target_id;
		string text;
		string senders;

		string to_string();
		void append_sender(int id);
		static string serialize(Message message);
		static Message deserialize(string text);
		static list<Message> parse_message_file(string filename);
};

class Link
{
	public:
		int source_id;
		int target_id;
		int cost;
		string ip;
		string port;

		Link()
		{
			this->source_id = 0;
			this->target_id = 0;
			this->cost = -1;
			this->ip = "";
			this->port = "";
		};
};

class Topology
{
	public:
		int num_nodes;
		Link** matrix;

		Topology();
		~Topology();

		void parse_topology_file(string file);
		list<Link> get_node_links(int id);
		void update_node_net_info(int id, string host, string port);

		static string create_add_link_message(Link* link);
		static string create_remove_link_message(int id);
		static bool process_link_message(int id, string message, map<int, Link>* node_links);
		static string serialize_node_links(list<Link> links);
		static list<Link> deserialize_node_links(int id, string links_string);

};