#ifndef _UTILS_H_
#define _UTILS_H_

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/sendfile.h>
#include <sys/epoll.h>
#include <time.h>

#define PORT 7777
#define TCP_PROTOCOL 6
#define MAX_CONNECTIONS 16
#define MAX_EPOLLEVENTS 32
#define SIGINT 2

#define LIST 0x0
#define DOWNLOAD 0x1
#define UPLOAD 0x2
#define DELETE 0x4
#define MOVE 0x8
#define UPDATE 0x10
#define SEARCH 0x20

#define SUCCESS 0x0
#define FILE_NOT_FOUND 0x1
#define PERMISSION_DENIED 0x2
#define OUT_OF_MEMEORY 0x4
#define SERVER_BUSY 0x8
#define UNKNOWN_OPERATION 0x10
#define BAD_ARGUMENTS 0x20
#define OTHER_ERROR 0x40

#endif