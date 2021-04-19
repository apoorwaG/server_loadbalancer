#include<err.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/time.h>
#define N 10
#define bufsize 500

extern int errno;
extern char* optarg;
extern int optind, opterr, optopt;


sem_t sem;      // locking semaphore used for critical regions
sem_t signal;   // semaphore used to signal healthcheck thread to perform healthcheck   
sem_t ready;    // semaphore used to signal that healthceck has been completed   

sem_t empty;    // counting semaphore for number of empty slots
sem_t full;     // counting semaphore for number of filled slots
sem_t lock;     // locking sempahore used for critical sections

int buffer[bufsize];
int in = 0;             // index for inserting into the array
int out = 0;            // index for extracting from the array

int numServers = 0; 
int serverport[N];     // array will hold all of the port numbers 
int serverState[N];    // array will hold the state of the corresponding server (1 = good)

int bestServer = 0;     // will store port number of the best server
int healthErrors = 0;   // will store numErrors of the current best server
int healthEntries = 0;  // will store total load of the current best server
int serverReady = 0;    // server ready for processing? 1 = yes, -1 = no

int numRequests = 0;    // keeps track of completed requests
int reHealth = 5;       // default R = 5; healthcheck every 5 requests


// sends the formatted message (500 error) to the provided client
void sendErrorMessage(int client_socket){
    dprintf(client_socket, "HTTP/ 1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
    return;
    // send with MSG
}

/*
 * client_connect takes a port number and establishes a connection as a client.
 * connectport: port number of server to connect to
 * returns: valid socket if successful, -1 otherwise
 */
int client_connect(uint16_t connectport) {
    int connfd;
    struct sockaddr_in servaddr;

    connfd=socket(AF_INET,SOCK_STREAM,0);
    if (connfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);

    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(connectport);

    /* For this assignment the IP address can be fixed */
    inet_pton(AF_INET,"127.0.0.1",&(servaddr.sin_addr));

    if(connect(connfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0)
        return -1;
    return connfd;
}

/*
 * server_listen takes a port number and creates a socket to listen on 
 * that port.
 * port: the port number to receive connections
 * returns: valid socket if successful, -1 otherwise
 */
int server_listen(int port) {
    int listenfd;
    int enable = 1;
    struct sockaddr_in servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        return -1;
    memset(&servaddr, 0, sizeof servaddr);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htons(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
        return -1;
    if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof servaddr) < 0)
        return -1;
    if (listen(listenfd, 500) < 0)
        return -1;
    return listenfd;
}

/*
 * bridge_connections send up to 100 bytes from fromfd to tofd
 * fromfd, tofd: valid sockets
 * returns: number of bytes sent, 0 if connection closed, -1 on error
 */
int bridge_connections(int fromfd, int tofd) {
    char recvline[4096];
    int n = recv(fromfd, recvline, 4096, 0);
    if (n < 0) {
        // printf("connection error receiving\n");
        return -1;
    } else if (n == 0) {
        // printf("receiving connection ended\n");
        return 0;
    }
    recvline[n] = '\0';
    // printf("%s", recvline);
    // sleep(1);
    n = send(tofd, recvline, n, 0);
    if (n < 0) {
        // printf("connection error sending\n");
        return -1;
    } else if (n == 0) {
        // printf("sending connection ended\n");
        return 0;
    }
    return n;
}

// returns -1 if httpserver doesn't respond to healthcheck in specified time
// returns 1 if httpserver is responsive
int healthTimer(int connfd){
    fd_set set;
    struct timeval timeout;

    FD_ZERO(&set);
    FD_SET(connfd, &set);

    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    switch(select(FD_SETSIZE, &set, NULL, NULL, &timeout)){
        case -1:
            return -1;
        case 0:
            return -1;
        default:
            if(FD_ISSET(connfd, &set)){
                return 1;
            }
            else{
                return -1;
            }
    }
}

/*
 * bridge_loop forwards all messages between both sockets until the connection
 * is interrupted. It also prints a message if both channels are idle.
 * sockfd1, sockfd2: valid sockets
 */
void bridge_loop(int sockfd1, int sockfd2) {
    fd_set set;
    struct timeval timeout;

    int fromfd, tofd;
    while(1) {
        // set for select usage must be initialized before each select call
        // set manages which file descriptors are being watched
        FD_ZERO (&set);
        FD_SET (sockfd1, &set);
        FD_SET (sockfd2, &set);

        // same for timeout
        // max time waiting, 5 seconds, 0 microseconds
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        // select return the number of file descriptors ready for reading in set
        switch (select(FD_SETSIZE, &set, NULL, NULL, &timeout)) {
            case -1:
                printf("error during select, exiting\n");
                return;
            case 0:
                // server isn't responsive
                sendErrorMessage(sockfd1);
                sem_wait(&sem);
                serverReady = -1;
                sem_post(&sem);
                return;

            default:
                if (FD_ISSET(sockfd1, &set)) {
                    fromfd = sockfd1;
                    tofd = sockfd2;
                } else if (FD_ISSET(sockfd2, &set)) {
                    fromfd = sockfd2;
                    tofd = sockfd1;
                } else {
                    printf("this should be unreachable\n");
                    return;
                }
        }
        if (bridge_connections(fromfd, tofd) <= 0)
            return;
    }
}

// args: httpserver socket, httpserver port
// sends a healthcheck request to httpserver socket and reads the response
// returns 1 if succesful, -1 if unsucessful
int processHealthRequest(int server_socket, int port){
    int numErrors;      // stores the total errors of the httpserver
    int totalEntries;   // stores the total load/entries of the httpserver
    char request[500];  // buffer holds the healthcheck request
    memset(request, 0, 500);
    char response[500]; // buffer to hold the healthcheck response
    memset(response, 0, 500);
    ssize_t BUFFERSIZE = 500;
    snprintf(request, 500, "GET /healthcheck HTTP/1.1\r\n\r\n");

    ssize_t bytes_sent = send(server_socket, request, strlen(request), 0);
    // internal server error
    if(bytes_sent < 0){
        return -1;
    }

    int responseReady = healthTimer(server_socket);
    if(responseReady < 0){
         printf("%d timeout error\n", port);
        return -1;
    }
    int tokens;
    ssize_t bytes_recvd = recv(server_socket, response, BUFFERSIZE, 0);
    if(bytes_recvd < 0)
        return -1;
    if(bytes_recvd > 0){
        // if body is included in the response
        if(bytes_recvd > 38){
            tokens = sscanf(response, "HTTP/1.1 200 OK\r\nContent-Length: %*d\r\n\r\n%d\r\n%d", &numErrors, &totalEntries);
        }
        // body not included in the response
        else{
            int statusCode;
            tokens = sscanf(response, "HTTP/1.1 %d OK\r\nContent-Length: %*d\r\n\r\n", &statusCode);
            if(tokens == 1 && statusCode == 200){
                char data[20];
                memset(data, 0, 20);
                bytes_recvd = recv(server_socket, data, 20, 0);
                if(bytes_recvd < 0)
                    return -1;
                tokens = sscanf(data, "%d\r\n%d", &numErrors, &totalEntries);
            }
        }
        // incorrect response to healthcheck
        if(tokens != 2){
            printf("Error response from port %d\n", port);
            return -1;
        }
        printf("Server %d, total load: %d, total errors: %d\n", port, totalEntries, numErrors);
        // sem_wait(&sem);
        if(bestServer == 0){
            bestServer = port;
            healthEntries = totalEntries;
            healthErrors = numErrors;
        }
        // if this server has less load than the current server
        if(bestServer != 0 && totalEntries < healthEntries){
            // update the current load and errors
            healthEntries = totalEntries;
            healthErrors = numErrors;
            bestServer = port;
        }
        // if this server has the same load as the current server
        else if(bestServer != 0 && totalEntries == healthEntries){
            if(numErrors < healthErrors){
                bestServer = port;
                healthEntries = totalEntries;
                healthErrors = numErrors;
            }
        }
        // sem_post(&sem);
    }
    return 1;
}

// pings the httpservers and finds the best one
// returns 1 if at least one server is good
// returns -1 if no servers are good
int getBestServer(){
    // send and recv healthceck for each listed server
    for(int i = 0; i < numServers; i++){

        int port = serverport[i];
        int connfd = client_connect(port);
        if(connfd == -1){
            serverState[i] = -1;
        }
        else{
            int success = processHealthRequest(connfd, port);
            if(success == -1)
                serverState[i] = -1;
            else
                serverState[i] = 1;
        }
        close(connfd);
    }

    // at this point, healthcheck for all servers has been performed
    for(int i = 0; i < numServers; i++){
        // return 1 if there is at least one active server
        if(serverState[i] == 1){
            serverReady = 1;
            return 1;
        }
    }
    // return -1 if no active servers
    serverReady = -1;
    return -1;
}

// resets the globals healthErrors and healthEntries
// for new healthcheck
void resetData(){
    bestServer = 0;
    healthErrors = 0;
    healthEntries = 0;
    serverReady = 0;
    for(int i = 0; i < numServers; i++){
        serverState[i] = 0;
    }
}

// healthcheck thread: updates variable bestServer and serverReady
// if no servers are responding, serverReady = -1
void* processHealthCheck(void * id){
    int threadId = *(int*)id;
    printf("Enter healthcheck thread: %d\n", threadId);
    struct timespec ts;
    struct timeval now;

    while(1){
        memset(&ts, 0, sizeof(ts));
        gettimeofday(&now, NULL);
        ts.tv_sec = now.tv_sec + 3;
        ts.tv_nsec = now.tv_usec * 1000L;
        sem_timedwait(&signal, &ts);
        // critical region
        // sem_wait(&ready);
        sem_wait(&sem);
        resetData();                        // reset healthErrors and healthEntries for future use
        int success = getBestServer();      // bestServer, healthErrors, healthEntries, and serverReady are set
        sem_post(&sem);
        // sem_post(&ready);
        // exit critical region
        if(success == 1){
            printf("Best server: %d\n", bestServer);
        }
        else{
            printf("All servers down\n");
        }
    }


    return NULL;
}

void* bridger(void* id){
    int threadId = *(int*)id;
    int acceptfd;

    while(1){
        printf("Thread %d waiting for client\n", threadId);
        sem_wait(&full);
        sem_wait(&lock);
        acceptfd = buffer[out];
        out = (out + 1) % bufsize;
        printf("Thread %d will bridge client %d\n", threadId, acceptfd);
        sem_post(&lock);
        sem_post(&empty);

        int port;

        sem_wait(&sem);             // critical region
        if(serverReady == 1)
            port = bestServer;
        else
            port = -1;

        sem_post(&sem);             // leave critical region

        // no servers up
        if(port == -1){
            sendErrorMessage(acceptfd);
        }
        // best server selected in port
        else{
            int connfd = client_connect(port);
            if(connfd == -1)
                sendErrorMessage(acceptfd);
            else{
                bridge_loop(acceptfd, connfd);
            }
            close(connfd);
        }
        close(acceptfd);

        sem_wait(&sem);
        ++numRequests;
        // signal to healthcheck every R requests
        if(numRequests % reHealth == 0){
            printf("Signalling healthcheck\n");
            sem_post(&signal);
        }
        sem_post(&sem);
    }
}

int main(int argc,char **argv) {
    char* threads_num = NULL;
    char* healthInterval = NULL;

    int numThreads = 4;    // 4 parallel connections by default

    int c;
    while((c = getopt(argc, argv, "N:R:")) != -1){
        if(c == 'N'){
            threads_num = optarg;
        }
        else if(c == 'R'){
            healthInterval = optarg;
        }
    }

    if(threads_num != NULL){
        if(atoi(threads_num) == 0 || atoi(threads_num) < 0){
            printf("Invalid argument for -N\n");
        }
        else{
            numThreads = atoi(threads_num);
        }
    }

    if(healthInterval != NULL){
        if(atoi(healthInterval) == 0 || atoi(healthInterval) < 0){
            printf("Invalid argument for -R\n");
        }
        else{
            reHealth = atoi(healthInterval);
        }
    }

    if(argc - optind < 2){
        printf("Not enough arguments!\n");
        return EXIT_FAILURE;
    }

    int listenport = atoi(argv[optind]);
    if(listenport == 0){
        printf("Invalid port for clients!\n");
        return EXIT_FAILURE;
    }
    int pos = 0; ++optind;
    for(int k = optind; k < argc; k++){
        int temp = atoi(argv[k]);
        if(temp == 0){
            printf("Invalid port for server!\n");
            return EXIT_FAILURE;
        }
        serverport[pos] = temp;
        ++pos; ++numServers;
    }

    sem_init(&sem, 0, 1);
    sem_init(&signal, 0, 0);
    sem_init(&ready, 0, 1);

    printf("Clients connect to port: %u\n", listenport);
    printf("Server ports: %d\n", numServers);

    printf("Parallel Connections: %d\n", numThreads);
    printf("Health check signal: %d\n", reHealth);


    // run inital healtcheck on all servers and set the best one
    int success = getBestServer();
    printf("Best Server: %d\n", bestServer);

    // set the best server at the start of the loadbalancer
    pthread_t healthThread;
    int healthId = 1;
    pthread_create(&healthThread, NULL, processHealthCheck, &healthId);

    // array of worker threads
    // worker threads will bridge the clients to the optimal server
    // and establish connections
    pthread_t workerThread[numServers];
    int threadId[numServers];
    for(int i = 0; i < numServers; i++){
        threadId[i] = i;
        pthread_create(&workerThread[i], NULL, bridger, &threadId[i]);
    }

    int listenfd, acceptfd;
    if ((listenfd = server_listen(listenport)) < 0){
        printf("failed listening\n");
        return EXIT_FAILURE;
    }

    sem_init(&empty, 0, bufsize);
    sem_init(&full, 0, 0);
    sem_init(&lock, 0, 1);

    while(1){
        acceptfd = accept(listenfd, NULL, NULL);
        printf("Client accepted: %d\n", acceptfd);
        sem_wait(&empty);
        sem_wait(&lock);
        // printf("Inserted client %d into buffer\n", acceptfd);
        buffer[in] = acceptfd;
        in = (in + 1)%bufsize;
        sem_post(&lock);
        sem_post(&full);
    }

    return 0;
}