#include "utils.h"

int isRunning = 1;
pthread_mutex_t isRunningMutex = PTHREAD_MUTEX_INITIALIZER;

struct epoll_event stdin_ev;
struct epoll_event ev, events[MAX_EPOLLEVENTS];
int listen_sock, conn_sock, nfds, epollfd;

struct sockaddr_in server_addr;

pthread_t connections_thread;
pthread_t logging_thread;

typedef struct fileSystem
{
	char path[100];
	int isFile;					  // 0 for directory, 1 for file
	struct fileSystem *parent;	  // parent directory
	struct fileSystem **children; // children directories
	int childrenCount;
} fileSystem;

fileSystem root = {"./root", 0, NULL, NULL, 0};

typedef struct
{
	char logMessage[100];
	pthread_mutex_t mutex;
	int isMessageAvailable;
} LogBuffer;

LogBuffer logBuffer = {{0}, PTHREAD_MUTEX_INITIALIZER, 0};

char bufferToSend[1024];
int bufferToSendSize = 0;

int logfd;

void endServer()
{
	char logMsg[100];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	pthread_mutex_lock(&isRunningMutex);
	if (isRunning)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tServer closed\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		sendLogMessage(logMsg);
		printf("Closing server\n");
		isRunning = 0;

		pthread_join(connections_thread, NULL);
		pthread_join(logging_thread, NULL);

		close(listen_sock);
		close(conn_sock);
		close(epollfd);
		close(logfd);
	}
	pthread_mutex_unlock(&isRunningMutex);

	exit(0);
}

static void siq_handler(int sig)
{
	printf("Received signal %s\n", strsignal(sig));

	if (sig == SIGINT || sig == SIGTERM)
	{
		endServer();
	}
}

void signalHandler()
{
	char logMsg[100];
	struct sigaction sa;
	sigset_t mask;

	sigfillset(&mask);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = siq_handler;
	sa.sa_mask = mask;

	if (sigaction(SIGINT, &sa, NULL) == -1)
	{
		perror("sigaction");
		exit(1);
	}
	if (sigaction(SIGTERM, &sa, NULL) == -1)
	{
		perror("sigaction");
		exit(1);
	}
}

void *loggingThread(void *arg)
{
	while (isRunning)
	{
		pthread_mutex_lock(&logBuffer.mutex);
		if (logBuffer.isMessageAvailable == 1)
		{
			write(logfd, logBuffer.logMessage, strlen(logBuffer.logMessage));
			memset(logBuffer.logMessage, 0, 100);
			logBuffer.isMessageAvailable = 0;
		}
		pthread_mutex_unlock(&logBuffer.mutex);
	}
	return NULL;
}

void *handleConnection(void *arg)
{
	int conn_sock = *(int *)arg;
	uint32_t code;
	uint32_t status;
	char *buffer = (char *)malloc(100);
	char *thrash = (char *)malloc(1);
	memset(buffer, 0, 100);
	int bufferSize;
	int n;
	time_t t;
	struct tm tm;
	char logMsg[100];

	while (isRunning)
	{
		n = recv(conn_sock, &code, 4, 0);
		t = time(NULL);
		tm = *localtime(&t);
		if (n == -1)
		{
			printf("Connection closed: %d\n", conn_sock);
			sprintf(logMsg, "%d-%d-%d %d:%d:%d\tConnection closed:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
			sendLogMessage(logMsg);
			close(conn_sock);
			break;
		}
		else if (n == 0)
		{
			printf("Connection closed: %d\n", conn_sock);
			sprintf(logMsg, "%d-%d-%d %d:%d:%d\tConnection closed:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
			sendLogMessage(logMsg);
			close(conn_sock);
			break;
		}
		else
		{
			recv(conn_sock, thrash, 1, 0);

			char logMsg[100];
			memset(logMsg, 0, 100);
			if (code == LIST)
			{
				sprintf(logMsg, "%d-%d-%d %d:%d:%d\tLIST\t\tClient:%d\tSUCCESS\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
				sendLogMessage(logMsg);

				list(&root);
				uint32_t status = SUCCESS;
				write(conn_sock, &status, 4);
				write(conn_sock, ";", 1);
				write(conn_sock, &bufferToSendSize, 4);
				write(conn_sock, ";", 1);
				send(conn_sock, bufferToSend, bufferToSendSize, 0);
				write(conn_sock, ";", 1);
				memset(bufferToSend, 0, 1024);
				bufferToSendSize = 0;
			}
			else if (code == DOWNLOAD)
			{
				recv(conn_sock, &bufferSize, 4, 0);
				recv(conn_sock, thrash, 1, 0);
				memset(buffer, 0, 100);
				recv(conn_sock, buffer, bufferSize, 0);
				recv(conn_sock, thrash, 1, 0);
				status = download(buffer);
				if (status != SUCCESS)
				{
					write(conn_sock, &status, 4);
					write(conn_sock, ";", 1);
					if (status == FILE_NOT_FOUND)
					{
						sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tFILE_NOT_FOUND\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
					}
					else if (status == PERMISSION_DENIED)
					{
						sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tPERMISSION_DENIED\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
					}
					else
					{
						sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tUNKNOWN_ERROR\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
					}
				}
				else
				{
					sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tSUCCESS\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
				}
				sendLogMessage(logMsg);
			}
		}
	}
	free(buffer);
	free(thrash);

	return NULL;
}

void UpdateList(char *folder, fileSystem *parent)
{
	DIR *dir;
	struct dirent *file;

	if (!(dir = opendir(folder)))
	{
		perror("opendir");
		return;
	}

	while ((file = readdir(dir)) != NULL)
	{
		if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0)
		{
			char path[100];
			memset(path, 0, 100);
			sprintf(path, "%s/%s", folder, file->d_name);

			parent->children = (fileSystem **)realloc(parent->children, (parent->childrenCount + 1) * sizeof(fileSystem *));
			parent->children[parent->childrenCount] = (fileSystem *)malloc(sizeof(fileSystem));
			parent->children[parent->childrenCount]->parent = parent;
			strcpy(parent->children[parent->childrenCount]->path, path);
			parent->children[parent->childrenCount]->children = NULL;
			parent->children[parent->childrenCount]->childrenCount = 0;

			if (file->d_type == DT_DIR)
			{
				parent->children[parent->childrenCount]->isFile = 0;
				UpdateList(path, parent->children[parent->childrenCount]);
			}
			else
			{
				parent->children[parent->childrenCount]->isFile = 1;
			}
			parent->childrenCount++;
		}
	}

	closedir(dir);
}

void initFileSystem()
{
	char *rootName = "./root";
	UpdateList(rootName, &root);
}

int initServer()
{
	if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		return -1;
	}
	bzero(&server_addr, sizeof(struct sockaddr_in));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	inet_aton("localhost", &server_addr.sin_addr);

	// for reusing the same address
	if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
		perror("setsockopt(SO_REUSEADDR) failed");

	while (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
	{
		perror("Rebinding");
		sleep(1);
	}
	printf("Binded\n");

	listen(listen_sock, MAX_CONNECTIONS);

	if ((epollfd = epoll_create1(0)) == -1)
	{
		perror("epoll_create");
		return -1;
	}

	ev.events = EPOLLIN;
	ev.data.fd = listen_sock;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1)
	{
		perror("epoll_ctl: listen_sock");
		return -1;
	}

	stdin_ev.events = EPOLLIN;
	stdin_ev.data.fd = STDIN_FILENO;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_ev) == -1)
	{
		perror("epoll_ctl: stdin");
		return -1;
	}

	initFileSystem();

	return 0;
}

void getFileName(char *command)
{
	// // get the filename from the path
	// int len = strlen(command);
	// int i = len - 1;
	// while (command[i] != '/')
	// {
	// 	i--;
	// }
	// memcpy(fileName, command + i + 1, len - i - 1);
	// fileName[len - i - 1] = '\0';
}

void list(fileSystem *parent)
{
	// print list of directories and files like ls -R
	char aux[256];
	int currsor = 0;
	memset(aux, 0, 100);
	memcpy(aux, parent->path, strlen(parent->path));
	currsor = strlen(parent->path);
	memcpy(aux + currsor, "\n\t\0", 3);
	currsor += 3;
	for (int i = 0; i < parent->childrenCount; i++)
	{
		// strcpy(aux + currsor, parent->children[i]->path);
		memcpy(aux + currsor, parent->children[i]->path, strlen(parent->children[i]->path));
		currsor += strlen(parent->children[i]->path);
		memcpy(aux + currsor, "\t\0", 2);
		currsor += 2;
	}
	memcpy(aux + currsor, "\n\n\0", 3);
	currsor += 3;
	memcpy(bufferToSend + bufferToSendSize, aux, currsor);
	bufferToSendSize += currsor;
	for (int i = 0; i < parent->childrenCount; i++)
	{
		if (parent->children[i]->isFile == 0)
		{
			list(parent->children[i]);
		}
	}
}

int download(char *filePath)
{
	int accessStat = access(filePath, F_OK);
	if (accessStat == -1)
	{
		return FILE_NOT_FOUND;
	}
	accessStat = access(filePath, R_OK);
	if (accessStat == -1)
	{
		return PERMISSION_DENIED;
	}

	int fd = open(filePath, O_RDONLY);
	if (fd == -1)
	{
		return FILE_NOT_FOUND;
	}
	uint32_t status = SUCCESS;
	int size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	char *content = (char *)malloc(size);

	send(conn_sock, &status, 4, 0);
	send(conn_sock, ";", 1, 0);
	send(conn_sock, &size, 4, 0);
	send(conn_sock, ";", 1, 0);
	sendfile(conn_sock, fd, NULL, size);
	send(conn_sock, ";", 1, 0);
	close(fd);

	return SUCCESS;
}

void listenForConnection()
{
	char logMsg[100];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	while (isRunning)
	{
		nfds = epoll_wait(epollfd, events, MAX_EPOLLEVENTS, -1);
		if (nfds == -1)
		{
			perror("epoll_wait");
			pthread_mutex_lock(&isRunningMutex);
			isRunning = 0;
			pthread_mutex_unlock(&isRunningMutex);
			exit(1);
		}

		for (int i = 0; i < nfds; i++)
		{
			if (events[i].data.fd == listen_sock)
			{
				conn_sock = accept(listen_sock, NULL, NULL);
				if (conn_sock == -1)
				{
					perror("accept");
					exit(1);
				}

				sprintf(logMsg, "%d-%d-%d %d:%d:%d\tConnection accepted:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
				sendLogMessage(logMsg);

				printf("Connection accepted: %d\n", conn_sock);

				ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
				ev.data.fd = conn_sock;
				if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1)
				{
					perror("epoll_ctl: conn_sock");
					exit(1);
				}

				pthread_create(&connections_thread, NULL, handleConnection, (void *)&conn_sock);
			}
			// listen from STDIN
			else if (events[i].data.fd == STDIN_FILENO)
			{
				char *buffer = (char *)malloc(100);
				int n = read(STDIN_FILENO, buffer, 100);
				if (n == -1)
				{
					perror("read");
					exit(1);
				}
				else if (n == 0)
				{
					printf("EOF\n");
					exit(0);
				}
				else
				{
					buffer[n] = '\0';
					if (strcmp(buffer, "EXIT\n") == 0)
					{
						endServer();
					}
				}
			}
		}
	}
}

void sendLogMessage(char *message)
{
	pthread_mutex_lock(&logBuffer.mutex);
	memset(logBuffer.logMessage, 0, 100);
	memcpy(logBuffer.logMessage, message, strlen(message));
	logBuffer.isMessageAvailable = 1;
	pthread_mutex_unlock(&logBuffer.mutex);
}

int main(int agc, char **argv)
{
	signalHandler();
	logfd = open("log.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (logfd == -1)
	{
		perror("open(log.log)");
		return -1;
	}

	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	char logMsg[100];
	memset(logMsg, 0, 100);

	if (pthread_create(&logging_thread, NULL, loggingThread, NULL) != 0)
	{
		perror("pthread_create(logging_thread)");
		return -1;
	}

	if (initServer() == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tFailed to initialize server\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		sendLogMessage(logMsg);
		printf("Failed to initialize server\n");
		return -1;
	}

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tServer started\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	sendLogMessage(logMsg);
	printf("Server initialized\n");

	listenForConnection();

	if (logfd != -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tServer closed\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		sendLogMessage(logMsg);
		close(logfd);
	}

	return 0;
}