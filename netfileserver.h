#ifndef NETFILESERVER_h
#define NETFILESERVER_h

typedef struct Args {
    int clientSocket;
} Args;

typedef struct Message {
    char * payload;
    int length;
} Message;

typedef struct fileList { //list of open files
    char *fileName;             // file name
    int filedesc;               //file descriptor
    int writers;                //number of writers
    int readers;                //number of readers
    int totalUsers;    //number of clients with file open
    int exclusiveUsers;         //numbers of exclusive users
    int exclusiveWriter;        // if an exclsusive user is writing, -1 if all writers are unrestricted
    int transactionMode;        //0 if in transaction mode, -1 otherwise
    struct fileList *next;      //pointer to next open file
    
}fileList;

typedef struct clientList{ // list of files opened by each client. Each client has a list of their open files
    int fd;
    int flagUsed;
    struct clientList *next;
    char * pathname;
}clientList;

void * handleClient(void *args);
void printClientList(clientList ** cList);
void printFileList();
Message * serverOpen(void *receivedMessage, int mode, clientList **cList);
Message * serverClose(void *receivedMessage, clientList **cList, int);
Message * serverRead(void *receivedMessage);
Message * serverWrite(void *receivedMessage);



#endif