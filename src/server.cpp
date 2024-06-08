#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <map>
using namespace std;

string compress_string(const string &str)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        throw(runtime_error("deflateInit2 failed while compressing."));
    }
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(str.data()));
    zs.avail_in = str.size();
    int ret;
    char outbuffer[32768];
    string outstring;
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
        throw(runtime_error("Exception during zlib compression: (" + to_string(ret) + ") " + zs.msg));
    }
    return outstring;
}

typedef map<string, string> Headers;

struct Request
{
    // start line
    string method;
    string path;
    string http_version;
    // headers
    Headers headers;
    // body
    string body;
};

const int MAX_BUFFER_SIZE = 1000;
const regex ROOT_PATH_REGEX("/");
const regex ECHO_PATH_REGEX("/echo/(.+)");
const regex USER_AGENT_PATH_REGEX("/user-agent");
const regex FILE_PATH_REGEX("/files/(.+)");

string base_directory;

string read_file(string file_name)
{
    ifstream f(file_name);
    if (!f.is_open())
    {
        return "";
    }
    stringstream ss;
    ss << f.rdbuf();
    f.close();
    return ss.str();
}

void write_file(string file_name, string content)
{
    ofstream f(file_name);
    f << content;
    f.close();
}

void parse_request_start_line(string &start_line, struct Request &request)
{
    stringstream ss(start_line);
    string token;
    getline(ss, token, ' ');
    request.method = token;
    getline(ss, token, ' ');
    request.path = token;
    getline(ss, token, ' ');
    request.http_version = token;
}

void parse_request_headers(vector<string>::iterator begin, vector<string>::iterator end, struct Request &request)
{
    for (auto it = begin; it != end; ++it)
    {
        stringstream ss(*it);
        string name, value;
        getline(ss, name, ':');
        getline(ss, value, ':');
        value = value.substr(1);
        transform(name.begin(), name.end(), name.begin(), [](auto c)
                  { return tolower(c); });
        request.headers[name] = value;
    }
}

struct Request parse_request(char *buffer, size_t buffer_size)
{
    string message(buffer);
    vector<string> lines;
    size_t i = 0;
    while (i < buffer_size)
    {
        string line;
        while (!(buffer[i] == '\r' && buffer[i + 1] == '\n'))
        {
            line += buffer[i];
            i++;
        }
        i += 2;
        if (line.empty())
        {
            continue;
        }
        lines.push_back(line);
        if (buffer[i] == '\r' && buffer[i + 1] == '\n')
        {
            i += 2;
            break;
        }
    }
    struct Request request;
    parse_request_start_line(lines[0], request);
    parse_request_headers(lines.begin() + 1, lines.end(), request);
    request.body = string(buffer + i);
    return request;
}

size_t send_response(int socket_fd, string status, string body, Headers &headers)
{
    string msg = "HTTP/1.1 " + status + "\r\n";
    if (body.length() > 0)
    {
        if (headers.count("Content-Type") == 0)
        {
            msg += "Content-Type: text/plain\r\n";
        }
        msg += "Content-Length: " + to_string(body.length()) + "\r\n";
    }
    for (const auto &[name, value] : headers)
    {
        msg += name + ": " + value + "\r\n";
    }
    msg += "\r\n";
    if (body.length() > 0)
    {
        msg += body;
    }
    return send(socket_fd, msg.data(), msg.size(), 0);
}

size_t send_response(int socket_fd, string status, string body)
{
    Headers headers = {};
    return send_response(socket_fd, status, body, headers);
}

size_t send_response(int socket_fd, string status)
{
    return send_response(socket_fd, status, "");
}

size_t send_response_ok(int socket_fd)
{
    return send_response(socket_fd, "200 OK");
}

size_t send_response_ok(int socket_fd, string &content)
{
    return send_response(socket_fd, "200 OK", content);
}

size_t send_response_ok(int socket_fd, string &content, Headers &headers)
{
    return send_response(socket_fd, "200 OK", content, headers);
}

size_t send_response_created(int socket_fd)
{
    return send_response(socket_fd, "201 Created");
}

size_t send_response_not_found(int socket_fd)
{
    return send_response(socket_fd, "404 Not Found");
}

set<string> find_encodings(Headers &headers)
{
    string value = headers["accept-encoding"];
    stringstream ss(value);
    string encoding;
    set<string> encodings;
    while (getline(ss, encoding, ','))
    {
        if (encoding[0] == ' ')
        {
            encoding = encoding.substr(1);
        }
        encodings.insert(encoding);
    }
    return encodings;
}

void handle_request(int socket_fd, char *buffer, size_t buffer_size)
{
    auto request = parse_request(buffer, buffer_size);
    smatch parameters;
    if (regex_match(request.path, parameters, ROOT_PATH_REGEX))
    {
        send_response_ok(socket_fd);
    }
    else if (regex_match(request.path, parameters, ECHO_PATH_REGEX))
    {
        string msg = parameters[1];
        set<string> encodings = find_encodings(request.headers);
        Headers headers;
        if (encodings.count("gzip") > 0)
        {
            headers["Content-Encoding"] = "gzip";
            msg = compress_string(msg);
        }
        send_response_ok(socket_fd, msg, headers);
    }
    else if (regex_match(request.path, parameters, USER_AGENT_PATH_REGEX))
    {
        string user_agent = request.headers["user-agent"];
        send_response_ok(socket_fd, user_agent);
    }
    else if (regex_match(request.path, parameters, FILE_PATH_REGEX))
    {
        string file_name = parameters[1];
        if (request.method == "GET")
        {
            string file_content = read_file(base_directory + "/" + file_name);
            if (file_content.empty())
            {
                send_response_not_found(socket_fd);
            }
            else
            {
                Headers headers = {
                    {"Content-Type", "application/octet-stream"}};
                send_response_ok(socket_fd, file_content, headers);
            }
        }
        else if (request.method == "POST")
        {
            string file_content = request.body;
            write_file(base_directory + "/" + file_name, file_content);
            send_response_created(socket_fd);
        }
    }
    else
    {
        send_response_not_found(socket_fd);
    }
}

void respond(int socket_fd)
{
    char buffer[MAX_BUFFER_SIZE];
    size_t bytes_received = recv(socket_fd, buffer, MAX_BUFFER_SIZE, 0);
    cout << "Received " << bytes_received << " bytes.\n";
    handle_request(socket_fd, buffer, bytes_received);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--directory") == 0 && i + 1 < argc)
        {
            base_directory = argv[i + 1];
        }
    }
    cout << unitbuf;
    cerr << unitbuf;
    cout << "Logs from your program will appear here!\n";
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        cerr << "Failed to create server socket\n";
        return 1;
    }
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
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
        int socket_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
        cout << "Client connected\n";
        thread t(respond, socket_fd);
        t.detach();
    }
    close(server_fd);
    return 0;
}
