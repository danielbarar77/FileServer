# FileServer
- the project includes server and client

# Description
This project represents a multi-thread server and a client enabling users to manage files on the server. The server uses epoll for connection managemnt, signal management and input from STDIN. Additionally the server has a logging thread, graceful termination - which assures each customer that it will complete current connections and will no longer accept new connections. Also the project implements safety measures like mutexes to ensure synchronized access to shared resources.

# Instalation
- use make in the project folder

# Usage
- run ./server in a terminal
- run ./client in one or more terminals to launch the clients
- the client will have CLI menu
  
### Client
    root:FileServer$ ./client
    Socket created
    Connected to server
    
    =====================================
    Choose an option:
    EXIT
    LIST
    DOWNLOAD <filepath>
    UPLOAD <filepath>
    DELETE <filepath>
    MOVE <oldpath> <newpath>
    UPDATE <filepath> <start offset> <content>
    SEARCH <word>
    COMMAND:
### Server
    root:FileServer$ ./server
    Binded
    Server initialized
    Connection accepted: 7

### log.log
    2024-1-25 23:44:48	Server started
    2024-1-25 23:44:48	Connection accepted:7
    2024-1-25 23:44:48	Connection accepted:8
    2024-1-25 23:44:48	Connection accepted:9
    2024-1-25 23:45:10	LIST		Client:9	SUCCESS
    2024-1-25 23:45:20	LIST		Client:9	SUCCESS
    2024-1-25 23:45:29	Connection closed:7
    2024-1-25 23:45:53	Connection closed:8
    2024-1-25 23:46:1	Connection closed:9
    2024-1-25 23:44:48	Connection accepted:7
    2024-1-25 23:46:8	LIST		Client:7	SUCCESS
    2024-1-25 23:44:48	Connection accepted:8
    2024-1-25 23:46:18	LIST		Client:8	SUCCESS
    2024-1-25 23:47:2	DOWNLOAD	Client:8	SUCCESS	./root/dirTest/test3.txt
    2024-1-25 23:44:48	Connection accepted:9
    2024-1-25 23:47:46	UPLOAD		Client:9	SUCCESS	./root/local.txt
    2024-1-25 23:48:2	Connection closed:9
    2024-1-25 23:48:4	Connection closed:8
    2024-1-25 23:47:22	UPLOAD		Client:9	PERMISSION_DENIED(File already exists)	./root/
