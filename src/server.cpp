#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <fstream>
#include <algorithm>
#include <vector>
std::string directory_path = "/tmp/";
void handle_client(int client_fd)
{
    char buffer[1024] = {0};
    int read_bytes = read(client_fd, buffer, sizeof(buffer) - 1);
    if (read_bytes < 0)
    {
        std::cerr << "Failed to read from client\n";
        close(client_fd);
        return;
    }
    std::string request(buffer);
    std::string::size_type pos = request.find(' ');
    if (pos != std::string::npos)
    {
        std::string method = request.substr(0, pos);
        std::string::size_type pos2 = request.find(' ', pos + 1);
        if (pos2 != std::string::npos)
        {
            std::string path = request.substr(pos + 1, pos2 - pos - 1);
            std::string accept_encoding;

            std::string::size_type ae_pos = request.find("Accept-Encoding: ");
            if (ae_pos != std::string::npos)
            {
                ae_pos += 17;
                std::string::size_type ae_end = request.find("\r\n", ae_pos);
                if (ae_end != std::string::npos)
                {
                    accept_encoding = request.substr(ae_pos, ae_end - ae_pos);
                }
            }
            std::vector<std::string> encodings;
            std::string::size_type start = 0;
            std::string::size_type end = accept_encoding.find(',');
            while (end != std::string::npos)
            {
                encodings.push_back(accept_encoding.substr(start, end - start));
                start = end + 1;
                end = accept_encoding.find(',', start);
            }
            encodings.push_back(accept_encoding.substr(start));
            for (auto &encoding : encodings)
            {
                encoding.erase(std::remove_if(encoding.begin(), encoding.end(), isspace), encoding.end());
            }
            bool gzip_supported = std::find(encodings.begin(), encodings.end(), "gzip") != encodings.end();
            std::string http_response;
            if (path == "/")
            {
                http_response = "HTTP/1.1 200 OK\r\n\r\n";
            }
            else if (path.find("/echo/") == 0)
            {
                std::string echo_string = path.substr(6);
                std::string content_length = std::to_string(echo_string.size());
                http_response = "HTTP/1.1 200 OK\r\n";
                if (gzip_supported)
                {
                    http_response += "Content-Encoding: gzip\r\n";
                }
                http_response += "Content-Type: text/plain\r\n";
                http_response += "Content-Length: " + content_length + "\r\n\r\n";
                http_response += echo_string;
            }
            else if (path == "/user-agent")
            {
                std::string user_agent;
                std::string::size_type ua_pos = request.find("User-Agent: ");
                if (ua_pos != std::string::npos)
                {
                    ua_pos += 12;
                    std::string::size_type ua_end = request.find("\r\n", ua_pos);
                    if (ua_end != std::string::npos)
                    {
                        user_agent = request.substr(ua_pos, ua_end - ua_pos);
                    }
                }
                std::string content_length = std::to_string(user_agent.size());
                http_response = "HTTP/1.1 200 OK\r\n";
                http_response += "Content-Type: text/plain\r\n";
                http_response += "Content-Length: " + content_length + "\r\n\r\n";
                http_response += user_agent;
            }
            else if (path.find("/files/") == 0)
            {
                std::string filename = path.substr(7);
                std::string file_path = directory_path + filename;
                if (method == "GET")
                {
                    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
                    if (file.is_open())
                    {
                        std::streamsize file_size = file.tellg();
                        file.seekg(0, std::ios::beg);
                        std::string content_length = std::to_string(file_size);
                        http_response = "HTTP/1.1 200 OK\r\n";
                        http_response += "Content-Type: application/octet-stream\r\n";
                        http_response += "Content-Length: " + content_length + "\r\n\r\n";
                        http_response += std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    }
                    else
                    {
                        http_response = "HTTP/1.1 404 Not Found\r\n\r\n";
                    }
                }
                else if (method == "POST")
                {
                    std::string::size_type content_length_pos = request.find("Content-Length: ");
                    int content_length = 0;
                    if (content_length_pos != std::string::npos)
                    {
                        content_length_pos += 16;
                        std::string::size_type content_length_end = request.find("\r\n", content_length_pos);
                        if (content_length_end != std::string::npos)
                        {
                            content_length = std::stoi(request.substr(content_length_pos, content_length_end - content_length_pos));
                        }
                    }
                    std::string::size_type body_pos = request.find("\r\n\r\n");
                    if (body_pos != std::string::npos)
                    {
                        body_pos += 4;
                        std::string body = request.substr(body_pos, content_length);
                        std::ofstream outfile(file_path, std::ios::binary);
                        outfile.write(body.c_str(), body.size());
                        outfile.close();
                        http_response = "HTTP/1.1 201 Created\r\n\r\n";
                    }
                    else
                    {
                        http_response = "HTTP/1.1 400 Bad Request\r\n\r\n";
                    }
                }
                else
                {
                    http_response = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
                }
            }
            else
            {
                http_response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }
            write(client_fd, http_response.c_str(), http_response.size());
        }
    }
    close(client_fd);
}
int main(int argc, char **argv)
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    if (argc >= 3 && std::string(argv[1]) == "--directory")
    {
        directory_path = argv[2];
    }
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "setsockopt failed\n";
        return 1;
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
    while (true)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        std::cout << "Waiting for a client to connect...\n";
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0)
        {
            std::cerr << "Failed to accept client connection\n";
            continue;
        }
        std::cout << "Client connected\n";
        std::thread client_thread(handle_client, client_fd);
        client_thread.detach();
    }
    close(server_fd);
    return 0;
}