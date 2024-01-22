#include "utils.h"

int server_sock;
struct sockaddr_in server_addr;
int lastCommand = -1;

char fileName[100]; // name of the file to be downloaded

int createConnection()
{
	server_sock = socket(AF_INET, SOCK_STREAM, TCP_PROTOCOL);
	if (server_sock < 0)
	{
		perror("socket");
		return -1;
	}

	printf("Socket created\n");

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT);
	server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (connect(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
	{
		perror("connect");
		return -1;
	}

	printf("Connected to server\n");
	return 0;
}

void printMenu()
{
	printf("\n\n");
	printf("=====================================\n");
	printf("Choose an option:\n");
	printf("EXIT\n");
	printf("LIST\n");
	printf("DOWNLOAD <filepath>\n");
	printf("UPLOAD <filepath>\n");
	printf("DELETE <filepath>\n");
	printf("MOVE <oldpath> <newpath>\n");
	printf("UPDATE <filepath> <start offset> <content dimension> <content>\n");
	printf("SEARCH <word>\n");
	printf("COMMAND: ");
}

char *menu()
{
	printMenu();
	char *input = (char *)malloc(100);
	fgets(input, 100, stdin);
	input[strlen(input) - 1] = '\0';
	return input;
}

char *intToChar(int num)
{
	char *str = (char *)malloc(4);
	str[0] = (num >> 24) & 0xFF;
	str[1] = (num >> 16) & 0xFF;
	str[2] = (num >> 8) & 0xFF;
	str[3] = num & 0xFF;
	return str;
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

void list()
{
	uint32_t code = LIST;
	send(server_sock, &code, 4, 0);
	send(server_sock, ";", 1, 0);
}

void download(char *filePath)
{
	uint32_t code = DOWNLOAD;
	send(server_sock, &code, 4, 0);
	send(server_sock, ";", 1, 0);
	uint32_t lenPath = strlen(filePath);
	send(server_sock, &lenPath, 4, 0);
	send(server_sock, ";", 1, 0);
	send(server_sock, filePath, lenPath, 0);
	send(server_sock, ";", 1, 0);
}

void upload(char *filePath)
{
	if (checkExistanceFile(filePath) == 0)
	{
		printf("File not found !\n");
		return;
	}
	uint32_t code = UPLOAD;
	send(server_sock, &code, 4, 0);
	send(server_sock, ";", 1, 0);

	uint32_t len = strlen(filePath);
	send(server_sock, &len, 4, 0);
	send(server_sock, ";", 1, 0);

	send(server_sock, filePath, len, 0);
	send(server_sock, ";", 1, 0);

	int fd = open(filePath, O_RDONLY);
	if (fd == -1)
	{
		perror("open");
		return -1;
	}
	len = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	send(server_sock, &len, 4, 0);
	send(server_sock, ";", 1, 0);

	sendfile(server_sock, fd, NULL, len);
	send(server_sock, ";", 1, 0);

	close(fd);
}

void delete(char *command, int dimension)
{
}

void getFileName(char *command)
{
	// get the filename from the path
	int len = strlen(command);
	int i = len - 1;
	while (command[i] != '/')
	{
		i--;
	}
	memcpy(fileName, command + i + 1, len - i - 1);
	fileName[len - i - 1] = '\0';
}

char *executeCommand(char *input)
{
	char *command = (char *)malloc(100);
	int dimension = 0;
	char *token = strtok(input, " \n\0");
	if (strcmp(token, "EXIT") == 0)
	{
		return "EXIT";
	}
	else if (strcmp(token, "LIST") == 0)
	{
		lastCommand = LIST;
		list();
	}
	else if (strcmp(token, "DOWNLOAD") == 0)
	{
		lastCommand = DOWNLOAD;
		token = strtok(NULL, " \n\0");
		if (token == NULL)
		{
			printf("Invalid command\n");
			return NULL;
		}
		getFileName(token);
		download(token);
	}
	else if (strcmp(token, "UPLOAD") == 0)
	{
		lastCommand = UPLOAD;
		token = strtok(NULL, " \n\0");
		if (token == NULL)
		{
			printf("Invalid command\n");
			return NULL;
		}
		upload(token);
	}
	else if (strcmp(token, "DELETE") == 0)
	{
		memcpy(command, intToChar(0x8), 4);
		printf("Not implemented yet!\n");
		return NULL;
	}
	else if (strcmp(token, "MOVE") == 0)
	{
		memcpy(command, intToChar(0x8), 4);
		printf("Not implemented yet!\n");
		return NULL;
	}
	else if (strcmp(token, "UPDATE") == 0)
	{
		printf("Not implemented yet!\n");
		memcpy(command, intToChar(0x10), 4);
		return NULL;
	}
	else if (strcmp(token, "SEARCH") == 0)
	{
		printf("Not implemented yet!\n");
		memcpy(command, intToChar(0x20), 4);
		return NULL;
	}
	else
	{
		printf("Invalid command!\n");
		return NULL;
	}
	return command;
}

void reciveData()
{
	char *buffer = (char *)malloc(512);
	char *thrash = (char *)malloc(1);
	memset(buffer, 0, 512);
	uint32_t size;
	uint32_t status;
	if (lastCommand == LIST)
	{
		recv(server_sock, &status, 4, 0);
		recv(server_sock, thrash, 1, 0);
		memset(buffer, 0, 512);
		recv(server_sock, &size, 4, 0);
		recv(server_sock, thrash, 1, 0);
		memset(buffer, 0, 512);
		recv(server_sock, buffer, size, 0);
		recv(server_sock, thrash, 1, 0);
		write(STDOUT_FILENO, buffer, size);
	}
	else if (lastCommand == DOWNLOAD)
	{
		recv(server_sock, &status, 4, 0);
		recv(server_sock, thrash, 1, 0);
		if (status == SUCCESS)
		{
			printf("Success !\n");
			memset(buffer, 0, 512);
			recv(server_sock, &size, 4, 0);
			recv(server_sock, thrash, 1, 0);
			memset(buffer, 0, 512);
			recv(server_sock, buffer, size, 0);
			recv(server_sock, thrash, 1, 0);
			int fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			if (fd == -1)
			{
				perror("open");
				return -1;
			}
			int wc = write(fd, buffer, size);
			close(fd);
		}
		else if (status == FILE_NOT_FOUND)
		{
			printf("File not found !\n");
		}
		else if (status == PERMISSION_DENIED)
		{
			printf("Permission denied !\n");
		}
	}
}

int main(int argc, char **argv)
{
	char *input = (char *)malloc(100);
	char *command = (char *)malloc(100);
	if (createConnection() == -1)
	{
		printf("Failed to create connection\n");
		return -1;
	}
	while (1)
	{
		input = menu();
		command = executeCommand(input);
		if (command == NULL)
		{
			continue;
		}
		if (strncmp(command, "EXIT", 4) == 0)
		{
			close(server_sock);
			break;
		}
		reciveData();
	}
	return 0;
}