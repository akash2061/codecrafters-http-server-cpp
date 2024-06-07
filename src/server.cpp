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
#include <zlib.h>
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
struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};
std::string compress_string(const std::string &str, int compressionlevel = Z_BEST_COMPRESSION)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, compressionlevel, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw(std::runtime_error("deflateInit failed while compressing."));
    zs.next_in = (Bytef *)str.data();
    zs.avail_in = str.size();
    int ret;
    char outbuffer[32768];
    std::string outstring;
    do
    {
        zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        ret = deflate(&zs, Z_FINISH);
        if (outstring.size() < zs.total_out)
        {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);
    deflateEnd(&zs);
    if (ret != Z_STREAM_END)
    {
        throw(std::runtime_error("Exception during zlib compression: " + std::to_string(ret)));
    }

    return outstring;
}
HttpRequest parseHttpRequest(const std::string request)
{
    HttpRequest httpRequest;
    std::istringstream requestStream(request);
    std::string line;
    std::getline(requestStream, line);
    std::istringstream lineStream(line);
    lineStream >> httpRequest.method >> httpRequest.path >> httpRequest.version;
    // Parse headers
    while (std::getline(requestStream, line) && line != "\r")
    {
        auto colonPos = line.find(':');
        if (colonPos != std::string::npos)
        {
            std::string headerName = line.substr(0, colonPos);
            std::string headerValue = line.substr(colonPos + 2); // Skip the colon and space
            httpRequest.headers[headerName] = headerValue;
        }
    }

    // Parse body (if any)
    while (std::getline(requestStream, line))
    {
        httpRequest.body += line;
    }

    return httpRequest;
}
std::string extractEchoString(const std::string &path)
{
    const std::string echoPrefix = "/echo/";
    auto startPos = path.find(echoPrefix);
    if (startPos != std::string::npos)
    {
        return path.substr(startPos + echoPrefix.size());
    }
    return std::string(); // Return an empty string
}
int main(int argc, char **argv)
{
    std::cout << "Logs from your program will appear here!\n";
    std::string dir;
    if (argc == 3 && strcmp(argv[1], "--directory") == 0)
    {
        dir = argv[2];
    }
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }
    // Since the tester restarts your program quite often, setting REUSE_PORT
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "setsockopt failed\n";
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0)
    {
        std::cerr << "listen failed\n";
        return 1;
    }
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    std::cout << "Waiting for a client to connect...\n";
    while (true)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
        std::cout << "Client connected\n";
        char buffer[1024] = {0};
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0)
        {
            std::cerr << "Failed to receive data\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }
        buffer[bytes_received] = '\0';
        HttpRequest request = parseHttpRequest(std::string(buffer));
        std::string responsePath = extractEchoString(request.path);
        std::string http_response;
        if (request.method == "POST" && request.path.substr(0, 7) == "/files/")
        {
            std::string fileName = request.path.substr(7);
            std::ofstream outfile(dir + fileName);
            outfile << request.body;
            outfile.close();
            http_response = "HTTP/1.1 201 Created\r\n\r\n";
        }
        else if (request.path == "/user-agent")
        {
            std::string userAgent = request.headers["User-Agent"];
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
            http_response += "Content-Length: " + std::to_string(userAgent.length() - 1) + "\r\n\r\n" + userAgent;
        }
        else if (request.path == "/")
        {
            http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"; // Return 200 for root path
        }
        else if (request.path.substr(0, 6) == "/echo/")
        {
            std::string encoding = "";
            if (request.headers["Accept-Encoding"].find("gzip") != std::string::npos)
                if (request.headers["Accept-Encoding"].find("gzip") != std::string::npos)
                {
                    responsePath = compress_string(responsePath);
                    encoding = "Content-Encoding: gzip\r\n";
                }
            http_response = "HTTP/1.1 200 OK\r\n";
            http_response += encoding;
            http_response += "Content-Type: text/plain\r\n";
            http_response += "Content-Length: " + std::to_string(responsePath.length()) + "\r\n\r\n" + responsePath;
        }
        else if (request.path.substr(0, 7) == "/files/")
        {
            std::string fileName = request.path.substr(7);
            std::ifstream ifs(dir + fileName);
            if (ifs.good())
            {
                std::stringstream content;
                content << ifs.rdbuf();
                std::stringstream respond("");
                http_response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(content.str().length()) + "\r\n\r\n" + content.str() + "\r\n";
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