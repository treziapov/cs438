#include "topology.h"

string Message::to_string()
{
	stringstream ss;
	ss << "from " << this->source_id; 
	ss << " to " << this->target_id;
	ss << " hops " << this->senders;
	ss << this->text;
	return ss.str();
}

void Message::append_sender(int id)
{
	stringstream ss;
	ss << senders << id << " ";
	this->senders = ss.str();
}

string Message::serialize(Message message)
{
	stringstream ss;
	ss << message.source_id << ";";
	ss << message.target_id << ";";
	ss << (message.senders == "" ? "-" : message.senders) << ";";
	ss << message.text; 
	return ss.str();
}

Message Message::deserialize(string text)
{
	Message message;
	char* token;
	char* str = strdup(text.c_str());
	token = strtok(str, ";");
	message.source_id = atoi(token);
	token = strtok(NULL, ";");
	message.target_id = atoi(token);
	token = strtok(NULL, ";");
	message.senders = (token[0] == '-' ? "" : string(token));
	token = strtok(NULL, "\0\n");
	message.text = string(token);
	free(str);
	return message;
}

/*
 * Parses the message file, returns a list of Message structs
 */
list<Message> Message::parse_message_file(string filename)
{
	list<Message> messages;
	string line;
	ifstream in(filename.c_str());
	char* token;
	Message message;

	if (in.is_open())
	{
		while (getline (in, line))
		{
			char* str = strdup(line.c_str());
			token = strtok(str, " ");
			message.source_id = atoi(token);
			token = strtok(NULL, " ");
			message.target_id = atoi(token);
			token = strtok(NULL, "\0\n");
			message.text = string(token);
			message.senders = "";
			messages.push_back(message);
			free(str);
		}
	}

	return messages;
}

/*
 * Constructor
 */
Topology::Topology()
{
	this->num_nodes = 0;
	this->matrix = NULL;
}

/*
 * Destructor
 */
Topology::~Topology()
{
	for (int i = 0; i < this->num_nodes; i++)
	{
		delete[] this->matrix[i];
	}
	delete[] this->matrix;
}

/*
 * Parses the topology file into a matrix
 * Format for each line:
 * 		[Source ID] [Destination ID] [Cost]
 */
void Topology::parse_topology_file(string file)
{
	int max_node_id = -1;
	list<Link> links;
	string line;
	ifstream in(file.c_str());

	// Parse the link and store it to build the matrix later
	if (in.is_open())
	{
		char * token;	
		while (getline (in, line))
		{
			char* str = strdup(line.c_str());
			Link link;
			token = strtok(str, " ");
			link.source_id = atoi(token);
			token = strtok(NULL, " ");
			link.target_id = atoi(token);
			token = strtok(NULL, " ");
			link.cost = atoi(token);
			free(str);

			links.push_back(link);

			// Update the max node number
			if (link.target_id > max_node_id) 
			{
				max_node_id = link.target_id;
			}
			if (link.source_id > max_node_id)
			{
				max_node_id = link.source_id;
			}
		}
	}

	// Build the topology matrix
	this->num_nodes = max_node_id + 1;
	this->matrix = new Link* [this->num_nodes];

	for (int i = 0; i < this->num_nodes; i++)
	{
		this->matrix[i] = new Link[this->num_nodes];
		Link* self = &this->matrix[i][i];
		self->cost = 0;
		self->source_id = i;
		self->target_id = i;
	}

	// Add topology links which are bidirectional
	for (list<Link>::iterator it = links.begin(); it != links.end(); it++)
	{
		Link* source_link = &this->matrix[it->source_id][it->target_id];
		Link* target_link = &this->matrix[it->target_id][it->source_id];
		source_link->source_id = it->source_id;
		source_link->target_id = it->target_id;
		target_link->source_id = it->target_id;
		target_link->target_id = it->source_id;
		source_link->cost = it->cost;
		target_link->cost = it->cost;
	}
}

/*
 * Returns a list of Links in the topology for the given node id
 */
list<Link> Topology::get_node_links(int id)
{
	list<Link> links;
	Link link;

	// Add the neighbors accroding to topology
	for (int i = 1; i < this->num_nodes; i++)
	{
		if (this->matrix[id][i].cost != -1)
		{		
			links.push_back(this->matrix[id][i]);
		}
	}

	return links;
}

void Topology::update_node_net_info(int id, string host, string port)
{
	Link* link;
	for (int i = 1; i < this->num_nodes; i++)
	{
		link = &this->matrix[i][id];
		link->ip = host;
		link->port = port;
	}
}

/*
 * Converts a list of Links into a string
 */
string Topology::serialize_node_links(list<Link> links)
{
	stringstream ss;
	
	for (list<Link>::iterator it = links.begin(); it != links.end(); it++)
	{
		ss << it->target_id << "-" 
			<< it->cost << "-" 
			<< (it->ip == "" ? "0" : it->ip) << "-" 
			<< (it->port == "" ? "0" : it->port) << ";"; 
	}
	
	return ss.str();
}

/*
 * Converts a string of Links into a list of Links
 */
list<Link> Topology::deserialize_node_links(int id, string links_string)
{
	list<Link> links;
	Link link;
	link.source_id = id;
	char* str = strdup(links_string.c_str());
	char* token = strtok(str, "- ;");	

	while(token != NULL)
	{
		link.target_id = atoi(token);
		token = strtok(NULL, "- ;");
		link.cost = atoi(token);
		token = strtok(NULL, "- ;");
		link.ip = string(token);
		token = strtok(NULL, "- ;");
		link.port = string(token);
		token = strtok(NULL, "- ;");
		links.push_back(link);
	}
	
	free(str);
	return links;
}