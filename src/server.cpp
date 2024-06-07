#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <fstream>
#ifdef __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

using namespace std;

struct HttpRequest
{
    string method;
    string path;
    string version;
    unordered_map<string, string> headers;
    string body;
};
HttpRequest parseHttpRequest(const string request)
{
    HttpRequest httpRequest;
    istringstream requestStream(request);
    string line;
    getline(requestStream, line);
    istringstream lineStream(line);
    lineStream >> httpRequest.method >> httpRequest.path >> httpRequest.version;
    // Parse headers
    while (getline(requestStream, line) && line != "\r")
    {
        auto colonPos = line.find(':');
        if (colonPos != string::npos)
        {
            string headerName = line.substr(0, colonPos);
            string headerValue = line.substr(colonPos + 2); // Skip the colon and space
            httpRequest.headers[headerName] = headerValue;
        }
    }
    // Parse body (if any)
    while (getline(requestStream, line))
    {
        httpRequest.body += line + "\n";
    }
    return httpRequest;
}
string extractEchoString(const string &path)
{
    const string echoPrefix = "/echo/";
    auto startPos = path.find(echoPrefix);
    if (startPos != string::npos)
    {
        return path.substr(startPos + echoPrefix.size());
    }
    return string(); // Return an empty string
}
int main(int argc, char **argv)
{
    // You can use print statements as follows for debugging, they'll be visible when running tests.
    cout << "Logs from your program will appear here!\n";
    string dir;
    if (argc == 3 && strcmp(argv[1], "--directory") == 0)
    {
        dir = argv[2];
    }

    // Uncomment this block to pass the first stage
    //
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        cerr << "Failed to create server socket\n";
        return 1;
    }

    // Since the tester restarts your program quite often, setting REUSE_PORT
    // ensures that we don't run into 'Address already in use' errors
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
    int client_addr_len = sizeof(client_addr);

    cout << "Waiting for a client to connect...\n";
    while (true)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
        cout << "Client connected\n";
        char buffer[1024] = {0};
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0)
        {
            cerr << "Failed to receive data\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }
        buffer[bytes_received] = '\0';
        HttpRequest request = parseHttpRequest(string(buffer));
        string responsePath = extractEchoString(request.path);
        string http_response;
        if (request.path == "/user-agent")
        {
            string userAgent = request.headers["User-Agent"];
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
            http_response += "Content-Length: " + to_string(userAgent.length() - 1) + "\r\n\r\n" + userAgent;
        }
        else if (request.path == "/")
        {
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"; // Return 200 for root path
        }
        else if (request.path.substr(0, 6) == "/echo/")
        {
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
            http_response += "Content-Length: " + to_string(responsePath.length()) + "\r\n\r\n" + responsePath;
        }
        else if (request.path.substr(0, 7) == "/files/")
        {
            string fileName = request.path.substr(7);
            ifstream ifs(dir + fileName);
            if (ifs.good())
            {
                stringstream content;
                content << ifs.rdbuf();
                stringstream respond("");
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + to_string(content.str().length()) + "\r\n\r\n" + content.str() + "\r\n";
            }
            else
            {
                http_response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }
        }
        else
        {
            http_response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }

        send(client_fd, http_response.c_str(), http_response.size(), 0);
    }

    close(server_fd);
    return 0;
}