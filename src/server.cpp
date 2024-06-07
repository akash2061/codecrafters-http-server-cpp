#include <alloca.h>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <list>
#include <thread>
#include <regex>
#include <sstream>

// Function to split a string by a delimiter
std::list<char*> token_split_all(const char *token, void *string_to_split) {
    std::list<char*> token_list;
    char *pch;
    pch = strtok((char*)string_to_split, token);
    if (pch) token_list.push_back(pch);
    while (pch != NULL) {
        pch = strtok(NULL, token);
        if (pch) token_list.push_back(pch);
    }
    return token_list;
}

// Function to extract the path from the HTTP request
char* http_path(char *http_index) {
    auto tokens = token_split_all(" ", http_index);
    if (tokens.size() < 2) return nullptr;
    return *(++tokens.begin());
}

int handle_http(int fd, struct sockaddr_in client_addr) {
    char data[1024];
    const char *data_sent_root_index = "HTTP/1.1 200 OK\r\n\r\n";
    char *pch, *http_index, *uri;

    std::cout << "Connection from: " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << "\n";
    recv(fd, data, 1024, 0);
    std::list<char*> lf_split_data = token_split_all("\r\n", data);
    http_index = *lf_split_data.begin();
    uri = http_path(http_index);
    printf("http path: %s\n", uri);

    if (strcmp(uri, "/") == 0) {
        send(fd, data_sent_root_index, strlen(data_sent_root_index), 0);
    } else if (strcmp(uri, "/user-agent") == 0) {
        lf_split_data.pop_back();
        for (auto rit = lf_split_data.rbegin(); rit != lf_split_data.rend(); ++rit) {
            if (strncmp((const char*)*rit, "User-Agent:", 11) == 0) {
                char sent_data[256];
                pch = strchr(*rit, ' ');
                if (!pch) {
                    std::cout << "User Agent HeaderFormat Error!" << '\n';
                    break;
                }
                pch++;
                unsigned user_agent_len = strlen(pch);
                strcpy(sent_data, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ");
                strcat(sent_data, std::to_string(user_agent_len).c_str());
                strcat(sent_data, "\r\n\r\n");
                strcat(sent_data, pch);
                send(fd, sent_data, strlen(sent_data), 0);
                break;
            }
        }
    } else {
        pch = strtok(uri, "/");
        const char *data_sent_404 = "HTTP/1.1 404 Not Found\r\n\r\n";
        if (pch && strcmp(pch, "echo") == 0) {
            char sent_data[256];
            pch = strtok(NULL, "/");
            strcpy(sent_data, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ");
            strcat(sent_data, std::to_string(strlen(pch)).c_str());
            strcat(sent_data, "\r\n\r\n");
            strcat(sent_data, pch);
            send(fd, sent_data, strlen(sent_data), 0);
        } else {
            send(fd, data_sent_404, strlen(data_sent_404), 0);
        }
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    std::cout << "Waiting for a client to connect...\n";

    while (true) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "Error in accepting client\n";
        } else {
            std::cout << "Client connected\n";
            std::thread th(handle_http, client_fd, client_addr);
            th.detach();
        }
    }

    close(server_fd);
    return 0;
}
