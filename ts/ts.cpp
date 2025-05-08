#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <algorithm>
#include <thread>
#include <errno.h>

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#ifdef WIN32
#include <ws2tcpip.h>
#endif

#ifdef WIN32
    void myerror(const char* msg) {
    fprintf(stderr, "%s %lu\n", msg, GetLastError());
}
#else
    void myerror(const char* msg) {
    fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno);
}
#endif

void usage() {
    printf("tcp server v1.0.1.1\n");
    printf("syntax: ts <port> [-e] [-b] [-si <src ip>]\n");
    printf("  -e : echo\n");
    printf("  -b : broadcast\n");
    printf("sample: ts 1234 -e -b\n");
}

std::vector<int> clientSocks;
std::mutex clientMutex;

struct Param {
    bool echo{false};
    bool broadcast{false};
    uint16_t port{0};
    uint32_t srcIp{0};

    bool parse(int argc, char* argv[]) {
        for (int i = 1; i < argc;) {
            if (strcmp(argv[i], "-e") == 0) {
                echo = true;
                i++;
                continue;
            }
            if (strcmp(argv[i], "-b") == 0) {
                broadcast = true;
                i++;
                continue;
            }
            if (strcmp(argv[i], "-si") == 0) {
                int res = inet_pton(AF_INET, argv[i + 1], &srcIp);
                if (res <= 0) {
                    myerror("inet_pton");
                    return false;
                }
                i += 2;
                continue;
            }
            if (i < argc) port = atoi(argv[i++]);
        }
        return port != 0;
    }
} param;

void recvThread(int sd) {
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        clientSocks.push_back(sd);
    }

    printf("connected\n");
    fflush(stdout);

    static const int BUFSIZE = 65536;
    char buf[BUFSIZE];

    while (true) {
        ssize_t res = recv(sd, buf, BUFSIZE - 1, 0);
        if (res <= 0) {
            break;
        }

        buf[res] = '\0';
        printf("%s", buf);
        fflush(stdout);

        if (param.echo) {
            if (param.broadcast) {
                std::lock_guard<std::mutex> lock(clientMutex);
                for (int csd : clientSocks) {
                    if (csd != sd) {
                        send(csd, buf, res, 0);
                    }
                }
            } else {
                send(sd, buf, res, 0);
            }
        }
    }

    printf("disconnected\n");
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        clientSocks.erase(std::remove(clientSocks.begin(), clientSocks.end(), sd), clientSocks.end());
    }

    fflush(stdout);
    close(sd);
}

int main(int argc, char* argv[]) {
    if (!param.parse(argc, argv)) {
        usage();
        return -1;
    }

#ifdef WIN32
    WSAData wsaData;
    WSAStartup(0x0202, &wsaData);
#endif

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        myerror("socket");
        return -1;
    }

#ifdef __linux__
    int optval = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = param.srcIp;
    addr.sin_port = htons(param.port);

    if (bind(sd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        myerror("bind");
        return -1;
    }

    if (listen(sd, 5) == -1) {
        myerror("listen");
        return -1;
    }

    while (true) {
        sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        int newsd = accept(sd, (sockaddr*)&clientAddr, &len);
        if (newsd == -1) {
            myerror("accept");
            continue;
        }
        std::thread(recvThread, newsd).detach();
    }

    close(sd);
    return 0;
}
