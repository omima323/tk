#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <stdio.h>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <cstring>
#include <pthread.h>
#include <cstdlib>
#include <bitset>
#include <time.h>
#include "string.h"


using namespace std;

mutex m;

//defining structs

//req example M1
struct Request
{
    char req_type;
    int req_len;
};

//for every server we need to maintain the current load, this data will be used to choose between servers
struct Server
{
    string server_ip_add;
    int socket_fd;
    int server_load = 0;
    char type;
};

Server program_servers[3];
int listen_socket_fd;

void InitServers()
{
    program_servers[0].server_ip_add = "192.168.0.101";
    program_servers[0].type = 'V';
    program_servers[1].server_ip_add = "192.168.0.102";
    program_servers[1].type = 'V';
    program_servers[2].server_ip_add = "192.168.0.103";
    program_servers[2].type = 'M';

};

//get the server FD
int aux2(struct addrinfo *res)
{
    struct addrinfo *tmp;
    int server_fd;
    for(tmp = res; tmp != NULL; tmp = tmp->ai_next)
    {
        server_fd = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol);
        if(server_fd < 0)
        {
            //failed to create socket, try next option
            continue;
        }
        bool connected = connect(server_fd, tmp->ai_addr, tmp->ai_addrlen);
        if(connected == 0)
        {
            //connected successfully
            break;
        }
        //if failed to connect close the fd
        close(server_fd);
        server_fd = -1;
    }
    return server_fd;
}

void aux1(int server)
{
    int server_fd;
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *tmp;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;

    getaddrinfo(program_servers[server].server_ip_add.c_str(), "80", &hints, &res);
    //res holds all possible connctions
    int curr_fd = aux2(res);
    if(curr_fd == -1)
    {
        cout << "Connecting to server " << server + 1 << " failed!" << endl;
        exit(1);
    }
    freeaddrinfo(res);
    program_servers[server].socket_fd = curr_fd;
}


int aux3(struct addrinfo *res)
{
    struct addrinfo *tmp;
    int listen_fd;
    for(tmp = res; tmp != NULL; tmp = tmp->ai_next)
    {
        listen_fd = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol);
        if(listen_fd < 0)
        {
            //failed to create socket, try next option
            continue;
        }
        bool binded = bind(listen_fd, tmp->ai_addr, tmp->ai_addrlen);
        if(binded == 0)
        {
            //binded successfully
            break;
        }
        //if failed to bind close the fd
        close(listen_fd);
        listen_fd = -1;
    }
    return listen_fd;
}

void aux4(int target_server, char* req)
{
    cout << "Received request: " << req << ", sending to  -->  " << program_servers[target_server].server_ip_add << endl;
}

void ConnectSToLB()
{
    cout << "Connecting to servers..." << endl;
    for(int i = 0; i < 3; i++)
    {
        aux1(i);
    }
}

void LBListen()
{
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *tmp;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    getaddrinfo(NULL, "80", &hints, &res);

    int curr_fd = aux3(res);

    if(curr_fd == -1)
    {
        cout << "Binding failed!" << endl;
        exit(1);
    }
    freeaddrinfo(res);

    bool l = listen(curr_fd, 5);
    if (l != 0)
    {
        close(curr_fd);
        cout << "Listen failed!" << endl;
        exit(1);
    }
    listen_socket_fd = curr_fd;
}

int ComputeServerLoad(int server, char req_type, int req_len)
{
    if(program_servers[server].type == 'V')
    {
        if(req_type == 'M')
        {
            return program_servers[server].server_load + req_len * 2;
        }

        else
        {
            return program_servers[server].server_load + req_len;
        }
    }
    else
    {
        if(req_type == 'M')
        {
            return program_servers[server].server_load + req_len;
        }

        if(req_type == 'V')
        {
            return program_servers[server].server_load + req_len * 3;
        }
        else
        {
            return program_servers[server].server_load + req_len * 2;
        }
    }
}


int GetTargetServer(char* req)
{
    m.lock();
    Request r;
    r.req_type = req[0];
    r.req_len = req[1] - '0';

    int curr_load[3] = {0};

    for(int i = 0; i < 3; i++)
    {
        curr_load[i] = ComputeServerLoad(i, r.req_type, r.req_len);
    }

    //choose server
    if (curr_load[0] <= curr_load[1])
    {
        if (curr_load[0] < curr_load[2] || (r.req_type != 'M' && (curr_load[0] == curr_load[2])))
        {
            program_servers[0].server_load = curr_load[0];
            m.unlock();
            return 0;
        }
    }
    else
    {
        if (curr_load[1] < curr_load[2] || (r.req_type != 'M' && (curr_load[1] == curr_load[2])))
        {
            program_servers[1].server_load = curr_load[1];
            m.unlock();
            return 1;
        }
    }
    program_servers[2].server_load = curr_load[2];
    m.unlock();
    return 2;
}


void* HostRequestHandle(int host_fd)
{
    char req[5] = {0};

    while(recv(host_fd, req, 2, 0) > 0)
    {
        int target_server = GetTargetServer(req);
        send(program_servers[target_server].socket_fd, req, 2, 0);
        aux4(target_server, req);
        recv(program_servers[target_server].socket_fd, req, 5, 0);
        send(host_fd, req, 5, 0);
    }
    close(host_fd);
    return NULL;
}


int main() {
    //initialize servers
    InitServers();

    //connect program servers to LB
    ConnectSToLB();

    LBListen();

    //accepting requests
    while (true)
    {
        int host_fd = accept(listen_socket_fd, NULL, NULL);
        thread t(HostRequestHandle, host_fd);
        t.detach();
    }

    return 0;
}
