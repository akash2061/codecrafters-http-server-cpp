#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <sstream>

using namespace std;
// Function to split a string by a delimiter
vector<string> split_message(const string &message, const string &delim)
{
	vector<string> toks;
	stringstream ss = stringstream{message};
	string line;
	while (getline(ss, line, *delim.begin()))
	{
		toks.push_back(line);
		ss.ignore(delim.length() - 1);
	}
	return toks;
}

// Function to extract the path from the HTTP request
string get_path(const string &request)
{
	vector<string> toks = split_message(request, "\r\n");
	vector<string> path_toks = split_message(toks[0], " ");
	return path_toks[1];
}

int main(int argc, char **argv)
{
	cout << "Logs from your program will appear here!\n";

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0)
	{
		cerr << "Failed to create server socket\n";
		return 1;
	}

	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
	{
		cerr << "setsockopt failed\n";
		return 1;
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(4221);

	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
	{
		cerr << "Failed to bind to port 4221\n";
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0)
	{
		cerr << "listen failed\n";
		return 1;
	}

	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	cout << "Waiting for a client to connect...\n";

	int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	if (client_fd < 0)
	{
		cerr << "Error in accepting client\n";
	}
	else
	{
		cout << "Client connected\n";
	}

	char buffer[1024];
	int ret = read(client_fd, buffer, sizeof(buffer));
	if (ret < 0)
	{
		cerr << "Error in reading from client socket\n";
	}
	else if (ret == 0)
	{
		cout << "No bytes read\n";
	}
	else
	{
		string request(buffer);
		cout << "Request: " << request << endl;
		string path = get_path(request);

		vector<string> split_paths = split_message(path, "/");
		string response;
		if (path == "/")
		{
			response = "HTTP/1.1 200 OK\r\n\r\n";
		}
		else if (split_paths.size() > 1 && split_paths[1] == "echo")
		{
			string echo_string = split_paths.size() > 2 ? split_paths[2] : "";
			response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + to_string(echo_string.length()) + "\r\n\r\n" + echo_string;
		}
		else
		{
			response = "HTTP/1.1 404 Not Found\r\n\r\n";
		}

		cout << "Response: " << response << endl;
		write(client_fd, response.c_str(), response.length());
	}

	close(client_fd);
	close(server_fd);

	return 0;
}
