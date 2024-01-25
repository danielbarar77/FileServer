#include "utils.h"

int isRunning = 1;
pthread_mutex_t isRunningMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t updateMutex = PTHREAD_MUTEX_INITIALIZER;

struct epoll_event stdin_ev;
struct epoll_event ev, events[MAX_EPOLLEVENTS];
struct epoll_event signal_ev;
int listen_sock, conn_sock, nfds, epollfd;

struct sockaddr_in server_addr;

pthread_t connections_thread;
pthread_t logging_thread;

typedef struct wordFrequency
{
	char word[100];
	int frequency;
} wordFrequency;

typedef struct fileSystem
{
	char path[100];
	int isFile;					  // 0 for directory, 1 for file
	struct fileSystem *parent;	  // parent directory
	struct fileSystem **children; // children directories
	int childrenCount;
	struct wordFrequency words[10];
	int wordsCount;
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
char fileName[100]; // name of the file to be uploaded

int signal_fd;

void endServer()
{
	char logMsg[100];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	// graceful termination - waits for all threads to finish
	pthread_mutex_lock(&isRunningMutex);
	if (isRunning)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tServer closed\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		sendLogMessage(logMsg);
		printf("Closing server\n");
		isRunning = 0;

		// wait for all the connections to finish
		pthread_join(connections_thread, NULL);
		pthread_join(logging_thread, NULL);

		close(listen_sock);
		close(conn_sock);
		close(epollfd);
		close(logfd);
		EmptyTree(&root);
	}
	pthread_mutex_unlock(&isRunningMutex);

	exit(0);
}

void *loggingThread(void *arg)
{
	// thread that writes to the log file
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
	char *thrash = (char *)malloc(1);
	int n;
	time_t t;
	struct tm tm;
	char logMsg[1024];

	while (isRunning)
	{
		memset(logMsg, 0, 100);

		n = recv(conn_sock, &code, 4, 0);
		recv(conn_sock, thrash, 1, 0);
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
			if (code == LIST)
			{
				list();
			}
			else if (code == DOWNLOAD)
			{
				download();
			}
			else if (code == UPLOAD)
			{
				upload();
			}
			else if (code == DELETE)
			{
				deleteFile();
			}
			else if (code == MOVE)
			{
				move();
			}
			else if (code == UPDATE)
			{
				update();
			}
			else if (code == SEARCH)
			{
				search();
			}
			else
			{
				status = UNKNOWN_OPERATION;
				write(conn_sock, &status, 4);
				write(conn_sock, ";", 1);
			}
		}
	}
	free(thrash);

	return NULL;
}

void findWordInFileSystem(char *word, fileSystem *parent)
{
	if (parent->isFile == 1)
	{
		for (int i = 0; i < parent->wordsCount; i++)
		{
			if (strcmp(parent->words[i].word, word) == 0)
			{
				char aux[256];
				memset(aux, 0, 256);
				sprintf(aux, "%d\t%s\n", parent->words[i].frequency, parent->path);
				memcpy(bufferToSend + bufferToSendSize, aux, strlen(aux));
				bufferToSendSize += strlen(aux);
			}
		}
	}
	else
	{
		for (int i = 0; i < parent->childrenCount; i++)
		{
			findWordInFileSystem(word, parent->children[i]);
		}
	}
}

int compare(const void *a, const void *b)
{
	wordFrequency *wordA = (wordFrequency *)a;
	wordFrequency *wordB = (wordFrequency *)b;
	return wordB->frequency - wordA->frequency;
}

void findTopFrequencyWords(fileSystem *parent)
{
	// find the top 10 most frequent words in the file
	char *buffer = (char *)malloc(4096);
	wordFrequency tempWords[256];
	int tempWordsCount = 0;
	int fd = open(parent->path, O_RDONLY);
	int n = read(fd, buffer, 4096);
	buffer[n] = '\0';
	char *word = strtok(buffer, " \n\t");
	while (word != NULL)
	{
		int found = 0;
		for (int i = 0; i < tempWordsCount; i++)
		{
			if (strcmp(tempWords[i].word, word) == 0)
			{
				tempWords[i].frequency++;
				found = 1;
				break;
			}
		}
		if (found == 0)
		{
			strcpy(tempWords[tempWordsCount].word, word);
			tempWords[tempWordsCount].frequency = 1;
			tempWordsCount++;
		}
		word = strtok(NULL, " \n\t");
	}

	qsort(tempWords, tempWordsCount, sizeof(wordFrequency), compare);

	for (int i = 0; i < 10 && i < tempWordsCount; i++)
	{
		strcpy(parent->words[i].word, tempWords[i].word);
		parent->words[i].frequency = tempWords[i].frequency;
	}
	if (tempWordsCount < 10)
	{
		parent->wordsCount = tempWordsCount;
	}
	else
	{
		parent->wordsCount = 10;
	}

	close(fd);
	free(buffer);
}

void initTree(char *folder, fileSystem *parent)
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
				initTree(path, parent->children[parent->childrenCount]);
			}
			else
			{
				parent->children[parent->childrenCount]->isFile = 1;
				findTopFrequencyWords(parent->children[parent->childrenCount]);
			}
			parent->childrenCount++;
		}
	}

	closedir(dir);
}

void EmptyTree(fileSystem *parent)
{
	for (int i = 0; i < parent->childrenCount; i++)
	{
		if (parent->children[i]->isFile == 0)
		{
			EmptyTree(parent->children[i]);
		}
		free(parent->children[i]);
	}
	free(parent->children);
	parent->children = NULL;
	parent->childrenCount = 0;
}

void AddFileToTree(char *path, fileSystem *parent)
{
	parent->children = (fileSystem **)realloc(parent->children, (parent->childrenCount + 1) * sizeof(fileSystem *));
	parent->children[parent->childrenCount] = (fileSystem *)malloc(sizeof(fileSystem));

	parent->children[parent->childrenCount]->parent = parent;
	strcpy(parent->children[parent->childrenCount]->path, path);
	parent->children[parent->childrenCount]->children = NULL;
	parent->children[parent->childrenCount]->childrenCount = 0;
	parent->children[parent->childrenCount]->isFile = 1;
	parent->childrenCount++;
}

void RemoveFileFromTree(char *path, fileSystem *parent)
{
	int index = -1;
	for (int i = 0; i < parent->childrenCount; i++)
	{
		if (strcmp(parent->children[i]->path, path) == 0)
		{
			index = i;
			break;
		}
	}
	if (index == -1)
	{
		return;
	}
	free(parent->children[index]);
	for (int i = index; i < parent->childrenCount - 1; i++)
	{
		parent->children[i] = parent->children[i + 1];
	}
	parent->childrenCount--;
	parent->children = (fileSystem **)realloc(parent->children, parent->childrenCount * sizeof(fileSystem *));
}

void initFileSystem()
{
	char *rootName = "./root";
	root.children = NULL;
	root.childrenCount = 0;
	root.isFile = 0;
	root.parent = NULL;
	root.wordsCount = 0;
	strcpy(root.path, rootName);
	initTree(rootName, &root);
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

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
	{
		perror("sigprocmask");
		return -1;
	}
	signal_fd = signalfd(-1, &mask, 0);
	if (signal_fd == -1)
	{
		perror("signalfd");
		return -1;
	}
	signal_ev.data.fd = signal_fd;
	signal_ev.events = EPOLLIN;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, signal_fd, &signal_ev) == -1)
	{
		perror("epoll_ctl: signal_fd");
		return -1;
	}

	initFileSystem();

	return 0;
}

void getFileName(char *buffer)
{
	// get the filename from the path
	int len = strlen(buffer);
	int i = len - 1;
	while (buffer[i] != '/' && i >= 0)
	{
		i--;
	}
	memcpy(fileName, buffer + i + 1, len - i - 1);
	fileName[len - i - 1] = '\0';
}

int checkExistanceFile(char *filepath)
{
	int accessStatus = access(filepath, F_OK);
	if (accessStatus == -1)
	{
		return 0;
	}
	return 1;
}

void recursiveMapping(fileSystem *parent)
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
			recursiveMapping(parent->children[i]);
		}
	}
}

void list()
{
	char logMsg[1024];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	recursiveMapping(&root);

	if (bufferToSendSize == 0)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tLIST\t\tClient:%d\tOTHER_ERROR\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
		sendLogMessage(logMsg);
		uint32_t status = OTHER_ERROR;
		write(conn_sock, &status, 4);
		write(conn_sock, ";", 1);
		return;
	}

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tLIST\t\tClient:%d\tSUCCESS\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock);
	sendLogMessage(logMsg);

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

void download()
{
	char logMsg[1024];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	char thrash[1];
	char *filePath;
	uint32_t len;
	uint32_t status = SUCCESS;

	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	filePath = (char *)malloc(len);
	memset(filePath, 0, len);
	recv(conn_sock, filePath, len, 0);
	recv(conn_sock, thrash, 1, 0);
	filePath[len] = '\0';

	if (access(filePath, F_OK))
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tFILE_NOT_FOUND\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = FILE_NOT_FOUND;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}
	if (access(filePath, R_OK) == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tPERMISSION_DENIED\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = PERMISSION_DENIED;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}
	int fd = open(filePath, O_RDONLY);
	if (fd == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tFILE_NOT_FOUND\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = FILE_NOT_FOUND;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}
	int size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	status = SUCCESS;

	send(conn_sock, &status, 4, 0);
	send(conn_sock, ";", 1, 0);
	send(conn_sock, &size, 4, 0);
	send(conn_sock, ";", 1, 0);
	sendfile(conn_sock, fd, NULL, size);
	send(conn_sock, ";", 1, 0);
	close(fd);

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDOWNLOAD\tClient:%d\tSUCCESS\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
	sendLogMessage(logMsg);
	free(filePath);
	return;
}

void upload()
{
	char logMsg[1024];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	uint32_t status = SUCCESS;

	char thrash[1];
	char *buffer;
	char *filePath;
	char *content;
	uint32_t len;

	// filePath
	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	buffer = (char *)malloc(len);
	memset(buffer, 0, len);
	recv(conn_sock, buffer, len, 0);
	recv(conn_sock, thrash, 1, 0);
	buffer[len] = '\0';

	getFileName(buffer);
	filePath = (char *)malloc(strlen(root.path) + strlen(fileName) + 2);
	memset(filePath, 0, strlen(root.path) + strlen(fileName) + 2);
	sprintf(filePath, "%s/%s", root.path, fileName);
	filePath[strlen(root.path) + strlen(fileName) + 1] = '\0';

	// file content
	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	content = (char *)malloc(len);
	memset(content, 0, len);
	recv(conn_sock, content, len, 0);
	recv(conn_sock, thrash, 1, 0);

	if (access(filePath, F_OK) == 0)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPLOAD\t\tClient:%d\tPERMISSION_DENIED(File already exists)\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = PERMISSION_DENIED;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		free(buffer);
		free(filePath);
		free(content);
		return;
	}

	int fd = open(filePath, O_WRONLY | O_CREAT, 0666);
	if (fd == -1)
	{
		if (errno == EDQUOT)
		{
			sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPLOAD\t\tClient:%d\tOUT_OF_MEMORY\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
			sendLogMessage(logMsg);
			status = OUT_OF_MEMEORY;
			send(conn_sock, &status, 4, 0);
			send(conn_sock, ";", 1, 0);
			free(buffer);
			free(filePath);
			free(content);
			return;
		}
		else
		{
			sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPLOAD\t\tClient:%d\tOTHER_ERROR\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
			sendLogMessage(logMsg);
			status = OTHER_ERROR;
			send(conn_sock, &status, 4, 0);
			send(conn_sock, ";", 1, 0);
			free(buffer);
			free(filePath);
			free(content);
			return;
		}
	}

	int wc = write(fd, content, len);
	if (wc == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPLOAD\t\tClient:%d\tOTHER_ERROR\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = OTHER_ERROR;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		free(buffer);
		free(filePath);
		free(content);
		return;
	}
	close(fd);

	AddFileToTree(filePath, &root);

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPLOAD\t\tClient:%d\tSUCCESS\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
	sendLogMessage(logMsg);

	status = SUCCESS;
	send(conn_sock, &status, 4, 0);
	send(conn_sock, ";", 1, 0);
	free(buffer);
	free(filePath);
	free(content);
	return;
}

void deleteFile()
{
	char logMsg[1024];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	uint32_t status = SUCCESS;
	char thrash[1];
	char *filePath;

	uint32_t len;
	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	filePath = (char *)malloc(len);
	memset(filePath, 0, len);
	recv(conn_sock, filePath, len, 0);
	recv(conn_sock, thrash, 1, 0);
	filePath[len] = '\0';

	if (strcmp(filePath, "./root") == 0)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDELETE\t\tClient:%d\tPERMISSION_DENIED(Cannot delete root directory)\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = PERMISSION_DENIED;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	if (checkExistanceFile(filePath) == 0)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDELETE\t\tClient:%d\tFILE_NOT_FOUND\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = FILE_NOT_FOUND;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	if (access(filePath, W_OK) == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDELETE\t\tClient:%d\tPERMISSION_DENIED\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = PERMISSION_DENIED;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	if (unlink(filePath) == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDELETE\t\tClient:%d\tOTHER_ERROR\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = OTHER_ERROR;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	EmptyTree(&root);
	initFileSystem();

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tDELETE\t\tClient:%d\tSUCCESS\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
	sendLogMessage(logMsg);
	status = SUCCESS;
	send(conn_sock, &status, 4, 0);
	send(conn_sock, ";", 1, 0);
	free(filePath);
	return;
}

void move()
{
	char logMsg[1024];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	uint32_t status = SUCCESS;
	char thrash[1];
	char *oldPath;
	char *newPath;
	uint32_t len;

	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	oldPath = (char *)malloc(len);
	memset(oldPath, 0, len);
	recv(conn_sock, oldPath, len, 0);
	recv(conn_sock, thrash, 1, 0);
	oldPath[len] = '\0';

	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	newPath = (char *)malloc(len);
	memset(newPath, 0, len);
	recv(conn_sock, newPath, len, 0);
	recv(conn_sock, thrash, 1, 0);
	newPath[len] = '\0';

	if (strcmp(oldPath, "./root") == 0)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tMOVE\t\tClient:%d\tPERMISSION_DENIED(Cannot move root directory)\t%s\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, oldPath, newPath);
		sendLogMessage(logMsg);
		status = PERMISSION_DENIED;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	if (checkExistanceFile(oldPath) == 0)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tMOVE\t\tClient:%d\tFILE_NOT_FOUND(Old path)\t%s\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, oldPath, newPath);
		sendLogMessage(logMsg);
		status = FILE_NOT_FOUND;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	if (checkExistanceFile(newPath) == 1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tMOVE\t\tClient:%d\tPERMISSION_DENIED(New path already exists)\t%s\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, oldPath, newPath);
		sendLogMessage(logMsg);
		status = PERMISSION_DENIED;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	if (rename(oldPath, newPath) == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tMOVE\t\tClient:%d\tOTHER_ERROR(Rename failed)\t%s\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, oldPath, newPath);
		sendLogMessage(logMsg);
		status = OTHER_ERROR;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		return;
	}

	EmptyTree(&root);
	initFileSystem();

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tMOVE\t\tClient:%d\tSUCCESS\t%s\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, oldPath, newPath);
	sendLogMessage(logMsg);
	status = SUCCESS;
	send(conn_sock, &status, 4, 0);
	send(conn_sock, ";", 1, 0);
	free(oldPath);
	free(newPath);
	return;
}

void update()
{
	char logMsg[1024];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	uint32_t status = SUCCESS;
	char thrash[1];
	char *content;
	char *filePath;
	uint32_t len;
	uint32_t offset;

	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	filePath = (char *)malloc(len);
	memset(filePath, 0, len);
	recv(conn_sock, filePath, len, 0);
	recv(conn_sock, thrash, 1, 0);
	filePath[len] = '\0';

	recv(conn_sock, &offset, 4, 0);
	recv(conn_sock, thrash, 1, 0);

	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	content = (char *)malloc(len);
	memset(content, 0, len);
	recv(conn_sock, content, len, 0);
	recv(conn_sock, thrash, 1, 0);
	content[len] = '\0';

	pthread_mutex_lock(&updateMutex);

	if (access(filePath, F_OK) == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPDATE\t\tClient:%d\tFILE_NOT_FOUND\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = FILE_NOT_FOUND;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		free(content);
		free(filePath);
		pthread_mutex_unlock(&updateMutex);
		return;
	}
	if (access(filePath, W_OK) == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPDATE\t\tClient:%d\tPERMISSION_DENIED\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = PERMISSION_DENIED;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		free(content);
		free(filePath);
		pthread_mutex_unlock(&updateMutex);
		return;
	}

	int fd = open(filePath, O_WRONLY);
	if (fd == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPDATE\t\tClient:%d\tOTHER_ERROR(Open failed)\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = OTHER_ERROR;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		free(content);
		free(filePath);
		pthread_mutex_unlock(&updateMutex);
		return;
	}

	if (offset < 0)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPDATE\t\tClient:%d\tOTHER_ERROR(Offset is negative)\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = OTHER_ERROR;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		free(content);
		free(filePath);
		pthread_mutex_unlock(&updateMutex);
		return;
	}
	if (offset > lseek(fd, 0, SEEK_END))
	{
		offset = lseek(fd, 0, SEEK_END);
	}
	lseek(fd, offset, SEEK_SET);
	int wc = write(fd, content, len);
	if (wc == -1)
	{
		sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPDATE\t\tClient:%d\tOTHER_ERROR(Write failed)\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
		sendLogMessage(logMsg);
		status = OTHER_ERROR;
		send(conn_sock, &status, 4, 0);
		send(conn_sock, ";", 1, 0);
		free(content);
		free(filePath);
		pthread_mutex_unlock(&updateMutex);
		return;
	}
	close(fd);
	pthread_mutex_unlock(&updateMutex);

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tUPDATE\t\tClient:%d\tSUCCESS\t%s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, filePath);
	sendLogMessage(logMsg);

	status = SUCCESS;
	send(conn_sock, &status, 4, 0);
	send(conn_sock, ";", 1, 0);
	free(content);
	free(filePath);
	return;
}

void search()
{
	char logMsg[1024];
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	uint32_t status = SUCCESS;
	char thrash[1];
	char *word;
	uint32_t len;

	recv(conn_sock, &len, 4, 0);
	recv(conn_sock, thrash, 1, 0);
	word = (char *)malloc(len);
	memset(word, 0, len);
	recv(conn_sock, word, len, 0);
	recv(conn_sock, thrash, 1, 0);
	word[len] = '\0';

	memset(bufferToSend, 0, 1024);
	bufferToSendSize = 0;
	findWordInFileSystem(word, &root);

	sprintf(logMsg, "%d-%d-%d %d:%d:%d\tSEARCH\t\tClient:%d\tSUCCESS\tWORD: %s\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, conn_sock, word);
	sendLogMessage(logMsg);
	status = SUCCESS;
	send(conn_sock, &status, 4, 0);
	send(conn_sock, ";", 1, 0);
	send(conn_sock, &bufferToSendSize, 4, 0);
	send(conn_sock, ";", 1, 0);
	send(conn_sock, bufferToSend, bufferToSendSize, 0);
	send(conn_sock, ";", 1, 0);
	memset(bufferToSend, 0, 1024);
	bufferToSendSize = 0;
	free(word);
	return;
}

void printWordFrequency(fileSystem *parent)
{
	if (parent->isFile == 1)
	{
		for (int i = 0; i < parent->wordsCount; i++)
		{
			printf("%s: %d\n", parent->words[i].word, parent->words[i].frequency);
		}
	}
	else if (parent->isFile == 0)
	{
		for (int i = 0; i < parent->childrenCount; i++)
		{
			printWordFrequency(parent->children[i]);
		}
	}
}

void listenForEvents()
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
			// listen from signalfd
			else if (events[i].data.fd == signal_fd)
			{
				struct signalfd_siginfo fdsi;
				ssize_t s;
				s = read(signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
				if (s != sizeof(struct signalfd_siginfo))
				{
					perror("read");
					exit(1);
				}
				if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM)
				{
					printf("Recived signal %s\n", strsignal(fdsi.ssi_signo));
					endServer();
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
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
	{
		perror("sigprocmask");
		return -1;
	}

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

	// printWordFrequency(&root);

	listenForEvents();

	endServer();

	return 0;
}