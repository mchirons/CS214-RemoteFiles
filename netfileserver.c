#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include "netfileserver.h"

pthread_mutex_t listmutex;
fileList* openFiles;


void waitForClients(){
    openFiles = NULL;
    
    int serverSocket, clientSocket;
    pthread_t clientThread;
    void *(*hClient)(void*);
    hClient = &handleClient;
    
    struct addrinfo hints, *serverinfo, *p;
    int rv;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    
    
    if ((rv = getaddrinfo(NULL, "42602", &hints, &serverinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        
        exit(1);
    }
    
    // loop through all the results and bind to the first we can
    for(p = serverinfo; p != NULL; p = p->ai_next) {
        if ((serverSocket = socket(p->ai_family, p->ai_socktype,
                                   p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }
        
        if (bind(serverSocket, p->ai_addr, p->ai_addrlen) == -1) {
            shutdown(serverSocket, 2);
            perror("bind");
            continue;
        }
        else {
            //printf("bind made\n");
        }
        
        break; // if we get here, we must have connected successfully
    }
    
    if (p == NULL) {
        // looped off the end of the list with no successful bind
        fprintf(stderr, "failed to bind socket\n");
        exit(1);
        
    }
    else {
        freeaddrinfo(serverinfo); // all done with this structure
        
        while (1){
            
            struct sockaddr_storage their_addr;
            socklen_t addr_size;
            
            listen(serverSocket, 10);
            
            addr_size = sizeof their_addr;
            //printf("waiting for client connection\n");
            clientSocket = accept(serverSocket, (struct sockaddr *)&their_addr, &addr_size);
            //printf("clientSocket: %d\n", clientSocket);
            
            if (clientSocket > -1){
                //printf("clientSocket not -1\n");
                Args *arguments = (Args*)malloc(sizeof(Args));
                arguments->clientSocket = clientSocket;
                if ((pthread_create(&clientThread, NULL, hClient, arguments)) == 0){
                    //printf("thread spawned\n");
                }
                else {
                    perror("pthread_create");
                }
            }
            
            
        }
        
    }
    
    
}

void *handleClient(void* args){
     errno = 0;
    
    Args * arguments = args;
    int modeMess[1];
    *modeMess = -1;
    clientList *cList = NULL;
    int clientSocket = arguments->clientSocket;
    while (1){
        
        char peekbuf[4];
        
        //get length of message
        if ((recv(clientSocket, peekbuf, 4, MSG_PEEK)) == -1){
            perror("receive peek");
            shutdown(clientSocket, 2);
            exit(1);
        }
        
        int messageLength = *((int*)peekbuf);
        
        //receive message and determine function call
        char buf[messageLength];
        ssize_t bytesRead = recv(clientSocket, buf, messageLength, MSG_WAITALL);
        if (bytesRead == -1){
            perror("receive");
            shutdown(clientSocket, 2);
            exit(1);
        }
        else if (bytesRead == 0){
            continue;
        }
        else if (bytesRead < messageLength){
            printf("handleClient: not all bytes read\n");
            //printf("handleClient bytesRead: %d\n", (int)bytesRead);
            exit(1);
        }
       
        if (*modeMess==-1) {
            if (messageLength == 8) {
                modeMess[0] = *(((int*)(buf+4)));
                if (*modeMess <0 || *modeMess>2) {
                    perror("ERROR WITH MODE MESSAGE. Default to unrestricted for now");
                    *modeMess = 0;
                }

                ssize_t bytesSent = send(clientSocket, modeMess, 4, 0);
                if (bytesSent == -1){
                    perror("mode");
                    exit(1);
                }
                else if (bytesSent < 4){
                    printf("mode not all bytes read");
                    exit(1);
                }
                continue;
            }
        }

        //printf("\n***function command received***\n");
       // printf("messageLength: %d\n", messageLength);
        
        /*
         int i;
         for (i = 0; i < messageLength; i++){
         printf("buf[%d]: %c %d\n", i,  buf[i], buf[i]);
         }
         */
        
        char functionCode = buf[4];
        //printf("functionCode: %d\n", functionCode);
        Message *replyMessage = NULL;
        //execute correct function operation
        pthread_mutex_lock(&listmutex);
        if (functionCode == 0){
            replyMessage = serverOpen(buf, *modeMess, &cList);
            //printFileList();
        }
        else if (functionCode == 1){
            //printFileList();
            //printClientList(&cList);
            replyMessage = serverClose(buf, &cList, *modeMess);
            //printFileList();
            //printClientList(&cList);
        }
        else if (functionCode == 2){
            replyMessage = serverRead(buf);
        }
        else if (functionCode == 3){
            replyMessage = serverWrite(buf);
        }
        else {
            printf("invalid functionCode\n");
        }
        pthread_mutex_unlock(&listmutex);
        
        //send replyMessage to client
        ssize_t bytesSent = send(clientSocket, replyMessage->payload, replyMessage->length, 0);
        if (bytesSent == -1){
            perror("send");
        }
        if (bytesSent < replyMessage->length){
            printf("handleClient: not all bytes sent\n");
            exit(1);
            //need to send the rest
        }
        else {
            //printf("replyMessage sent\n");

        }
    }
    
    return NULL;
}

Message * serverOpen(void * messageReceived, int mode, clientList **cList){
     errno = 0;
    //printf("\n***serverOpen***\n");
    char * ptr = (char *)messageReceived;
    
    //extract length of path
    int length = *((int*)(ptr + 5));
    
    //extract path
    char * pathname = (char *)malloc(sizeof(char) * (length + 1));
    
    memcpy(pathname, ptr + 9, length);
    pathname[length + 1] = '\0';
    //printf("pathname: %s\n", pathname);


    char * pathcopy = (char*)malloc(sizeof(char) * (strlen(pathname) + 1));
    strcpy(pathcopy, pathname);
    
    //extract flags
    
    int flags = *((int *)(ptr + 9 + length));
    
    //EXTENSION A
    
    int found = 0;
    int success = 1;
 
    fileList *current = openFiles;
    fileList *new = NULL;
    fileList *prev = current;
    int fd = 0;
    if (current != NULL){
        switch (mode) {
       ///printf("open big loop\n");         
                
            case 0: //UNSRESTRICTED MODE
                prev = current;
                while (current!=NULL) {
                    //printf("open case 0\n");
                    if (strcmp(current->fileName, pathname) == 0){
                        found = 1;

                        if(current->transactionMode == 0){
                            fd = -1;
                            errno = EACCES;
                            //perror("FILE IN TRANSACTION MODE");
                        }else
                            if(current->exclusiveUsers!=0){  //if there are exclusive clients
                                //perror("FILE IN EXCLUSIVE MODE");
                                if (current->exclusiveWriter == 0 && (flags == O_RDWR||flags==O_WRONLY)) { // checks if client wants to write when an exclusive writer is present
                                    fd = -1;
                                    errno = EACCES;
                                    //perror("EXCLUSIVE WRITER, PERMISSION DENIED");
                                }
                                else if(current->exclusiveWriter == 0 && (flags==O_RDONLY)){ // checks if they only want to read
                                    fd = open(pathname, flags);
                                    //printf("open called\n");
                                    if(fd == -1){
                                        success = 0;
                                        break;
                                    }
                                    current->readers++;
                                    current->exclusiveUsers++;
                                    current->totalUsers++;
                                }else if(current->exclusiveWriter!=0){
                                    fd = open(pathname, flags);
                                    if(fd == -1){
                                        success = 0;
                                        break;
                                    }
                                   // printf("open called\n");
                                    if(flags == O_RDWR){
                                        current->readers++;
                                        current->writers++;
                                    } else
                                    if(flags == O_WRONLY){
                                        current->writers++;
                                    }else
                                    if(flags == O_RDONLY){
                                        current->readers++;
                                    }
                                    current->totalUsers++;
                                    break;
                                } else{
                                    //printf("ERROR\n");

                                }
                            } else{
                                    fd = open(pathname, flags);
                                    if(fd == -1){
                                        success = 0;
                                        break;
                                    }
                                    //printf("open called\n");
                                    if(flags == O_RDWR){
                                        current->readers++;
                                        current->writers++;
                                    } else
                                        if(flags == O_WRONLY){
                                            current->writers++;
                                        }else
                                            if(flags == O_RDONLY){
                                                current->readers++;
                                            }
                                    current->totalUsers++;
                            }
                        break;
                        
                    }
                    prev = current;
                    current = current->next;
                }
                if (found == 0){                    //if file not found in list (not open by anyone) creates new node
                    fd = open(pathname, flags);
                    if(fd == -1){
                        success = 0;
                        break;
                    }
                    //printf("open called\n");
                   
                    new = malloc(sizeof(fileList));
                    prev->next = new;
                    new->next = NULL;
                    new->fileName = pathname;
                    new->totalUsers=1;
                    new->readers=0;
                    new->writers=0;
                    new->exclusiveUsers=0;
                    if(flags ==O_RDONLY){
                        new->readers++;
                    }else
                        if(flags ==O_WRONLY){
                            new->writers++;
                        }else
                        {
                            new->readers++;
                            new->writers++;
                        }
                    new->exclusiveWriter=-1;
                    new->transactionMode = -1;
                    break;
                }
                break;
                
        
            case 1: //EXCLUSIVE MODE
                prev = current;
                while (current!=NULL) {
                    //printf("open case 1\n");
                   // printf("current->fileName: %s", current->fileName);
                    if (strcmp(current->fileName, pathname) == 0){
                        //printf("current->writers: %d\n", current->writers);
                       // printf("current->exlusiveUsers: %d\n", current->exclusiveUsers);
                        //printf("current->exlusiveWriter: %d\n", current->exclusiveWriter);
                       // printf("current->totalUsers: %d\n", current->totalUsers);
                        found = 1;
                        if(current->transactionMode == 0){
                            fd = -1;
                            errno = EACCES;
                            //perror("FILE IN TRANSACTION MODE");
                        }else if(flags!=O_RDONLY&&current->writers!=0){
                            fd = -1;
                            errno = EACCES;
                            //perror("Writers Already Present");
                        } else {
                            fd = open(pathname, flags);
                            if(fd == -1){
                                success = 0;
                                break;
                            }
                            //printf("open called\n");
                            if(flags == O_RDWR){
                                current->readers++;
                                current->writers++;
                                current->exclusiveWriter=0;
                            }   else
                                if(flags == O_WRONLY){
                                    current->writers++;
                                    current->exclusiveWriter=0;
                                }else
                                    if(flags == O_RDONLY){
                                        current->readers++;
                                    }
                            current->exclusiveUsers++;
                            current->totalUsers++;

                        }
                    }
                    prev = current;
                    current = current->next;
                }
                if (found == 0){
                    fd = open(pathname, flags);
                    if(fd == -1){
                        success = 0;
                        break;
                    }
                    //printf("open called\n");
                    new = malloc(sizeof(fileList));
                    prev->next = new;
                    new->fileName = pathname;
                    new->totalUsers=1;
                    new->readers=0;
                    new->writers=0;
                    new->exclusiveUsers=0;
                    new->exclusiveUsers++;
                    if(flags == O_RDONLY){
                        new->readers++;
                        new->exclusiveWriter= -1;
                    }else if(flags ==O_WRONLY){
                        new->writers++;
                        new->exclusiveWriter=0;
                    }else{
                        new->readers++;
                        new->writers++;
                        new->exclusiveWriter = 0;
                    }
                    new->transactionMode = -1;
                    new->next=NULL;
                }

                break;    
            
                
            case 2: //TRANSACTION MODE
                 prev = current;
                while (current!=NULL) {
                    //printf("current->filename %s\n", current->fileName);
                    if (strcmp(current->fileName, pathname) == 0){
                        found = 1;
                        fd = -1;
                        errno = EACCES;
                        //perror("FILE ALREADY OPEN, CANNOT OPEN IF IN TRANSACTION MODE");
                        break;
                    }
                    prev = current;
                    current = current->next;
                }
                if(found == 0){
                    fd = open(pathname, flags);
                    if(fd == -1){
                        success = 0;
                        break;
                    }
                     //printf("open called\n");
                    new = malloc(sizeof(fileList));
                    prev->next = new;
                    new->transactionMode = 0;
                    new->next=NULL;
                    new->fileName = pathname;
                    new->totalUsers = 0;
                    new->readers = 0;
                    new->exclusiveUsers = 0;
                }
                
                break;
                
            default:
               // printf("HOW DID THIS HAPPEN\n");// seriously how
                break;
        }
    } else{ //List is empty. Create first node in global list of open files
        fd = open(pathname, flags);
        if(fd != -1){
            success = 1;
            //printf("open called\n");
            new = malloc(sizeof(fileList));
            openFiles = new;
            new->fileName = pathname;
            new->totalUsers=1;
            new->readers=0;
            new->writers=0;
            new->exclusiveUsers=0;
            if(flags ==O_RDONLY){
                new->readers++;
            }else
                if(flags ==O_WRONLY){
                    new->writers++;
                }else
                {
                    new->readers++;
                    new->writers++;
                }
            new->exclusiveWriter=-1;
            new->transactionMode = -1;
            if (mode == 1){
                if (flags == O_RDWR || flags == O_WRONLY){
                    new->exclusiveWriter=0;
                }
                new->exclusiveUsers++;
            }
            if (mode == 2){
                 new->transactionMode = 0;
            }
            new->filedesc = fd;
            new->next = NULL; 
        }
        
    }
   
    //END EXTENSION A

    if (success){
        clientList *front = *cList;
        clientList *prev = front;
        clientList * current = front;
        clientList * new = NULL;
        if (front == NULL){
            new = (clientList*)malloc(sizeof(clientList));
            new->fd = fd;
            new->pathname = pathcopy;
            new->flagUsed = flags;
            new->next = NULL;
            *cList = new;
        }
        else {
              while (current !=NULL) { 
                  //update client list with flags used and file descriptor 
                    prev = current;
                    current = current->next;
                }
                new = (clientList*)malloc(sizeof(clientList));
                new->fd = fd;
                new->pathname = pathcopy;
                new->flagUsed = flags;
                new->next = NULL;
                prev->next = new;
        }
    }

    if (*cList == NULL){
        //printf("cList is NULL in open\n");
    }


    
    
    //printf("fd: %d\n", fd);
    //construct replyMessage
    Message *replyMessage = (Message*)malloc(sizeof(Message));
    replyMessage->payload = (char*)malloc(sizeof(char) * 8);
    replyMessage->length = 8;
    *((int*)replyMessage->payload) = fd;
    *((int*)(replyMessage->payload + 4)) = errno;
    //printf("file descriptor in payload: %d\n", *((int*)replyMessage->payload));
    //printf("errno in payload: %d\n", *((int*)(replyMessage->payload + 4)));
    
    return replyMessage;
}

void printClientList(clientList **cList){
    printf("Printing client list:\n");
    clientList *front = *cList;
    while (*cList != NULL){
        printf("pathname: %s\n", (*cList)->pathname);
        *cList = (*cList)->next;
    }
    *cList = front;
}

void printFileList(){
    printf("Printing file list:\n");
    fileList *current = openFiles;
    while (current != NULL){
        printf("pathname: %s\n", current->fileName);
        current = current->next;
    }
    
}

Message * serverClose(void * messageReceived, clientList **cList, int mode){

    errno = 0;
    
    //printf("\n***serverClose***\n");
    char * ptr = (char *)messageReceived;


    
    //extract file descriptor
    int fd = *((int *)(ptr + 5));
    fileList *current = openFiles;
    
    char * path;

    int returnVal = -1;
  // printf("errno right before close: %d %s\n", errno, strerror(errno));
    returnVal = close(fd);
    if (returnVal == -1){
        //perror("close");
    }
    else {
        //printf("close successful\n");
        //printf("errno right after close: %d %s\n", errno, strerror(errno));
    }
    
    if (openFiles == NULL){
        printf("openFiles is NULL in close\n");
    }

    if (*cList == NULL){
        //printf("cList is NULL\n");
    }
    clientList * front = *cList;
    while (*cList!=NULL) { 
            //printf("cList->fd: %d\n", (*cList)->fd);
            //printf("fd: %d\n", fd);              // gets info from client's currently open files list
        if ((*cList)->fd == fd){

            path = (*cList)->pathname;
            break;
        }
        *cList = (*cList)->next;
    }
    int found = 0;
    while (current!=NULL) {             // gets file from open files list
       // printf("current->filename: %s\n", current->fileName);
        //printf("cList->filename: %s\n", path);
        if (strcmp(current->fileName, path) == 0){
            found = 1;
            break;
        }
        current = current->next;
    }
    
    //adjusts file information depending on flags used to open it and the mode the client is in
    
    if (current == NULL){
        fd = -1;
        //printf("current is null in close\n");
    }
    else {

        if (found){
           if (mode==0) {                              //unrestricted mode
                if ((*cList)->flagUsed == O_RDWR) {
                    current->readers--;
                    current->writers--;
                    current->totalUsers--;
                }
                if ((*cList)->flagUsed == O_RDONLY) {
                    current->readers--;
                    current->totalUsers--;
                }
                if ((*cList)->flagUsed == O_WRONLY) {
                    current->writers--;
                    current->totalUsers--;
                }
            }
            if (mode==1) {                              //exclusive mode
                current->exclusiveUsers--;
                if ((*cList)->flagUsed == O_RDWR) {
                    current->readers--;
                    current->writers--;
                    current->totalUsers--;
                    current->exclusiveWriter=-1;
                }
                if ((*cList)->flagUsed == O_RDONLY) {
                    current->readers--;
                    current->totalUsers--;
                }
                if ((*cList)->flagUsed == O_WRONLY) {
                    current->writers--;
                    current->totalUsers--;
                    current->exclusiveWriter=-1;
                }
            }
            if (mode==2) {                              //transaction mode
                current->transactionMode=-1;
            }
            fileList *prev = openFiles;
            if (current->totalUsers == 0){
                //printf("DELETING FILE NODE\n");
                while (prev!=NULL) {
                    if (current == openFiles && current->next == NULL){
                        free(current);
                        openFiles = NULL;
                       
                        break;
                    }
                   else if (current == openFiles){
                        openFiles = current->next;
                        free(current);
                       
                        break;
                    }
                    else if (prev->next==current) {
                        prev->next = current->next;         //removes the closed file from list
                        free(current);
                        current=NULL;
                        break;
                    }
                    else {
                        //printf("next node\n");
                    }
                    prev = prev->next;
                }
            }

            clientList* cPrev = front;
            clientList *curr = *cList;
            //printf("DELETING ClIENT NODE\n");
            while (cPrev!=NULL) {
                if (curr == front && curr->next == NULL){
                    free(curr);
                    *cList = NULL;
                    break;
                }
               else if (curr == front){
                    *cList = curr->next;
                    free(curr);
                    break;
                }
                else if (cPrev->next==curr) {
                    cPrev->next = curr->next;         //removes the closed file from list
                    free(curr);
                    *cList = front;
                    break;
                }
                else {
                    //printf("next node\n");
                }
                cPrev = cPrev->next;
            } 
        }

        
    }

    
        
    
    
   
    
    //printf("returnVal: %d\n", returnVal);
    
    //construct replyMessage
    Message *replyMessage = (Message*)malloc(sizeof(Message));
    replyMessage->payload = (char*)malloc(sizeof(char) * 8);
    replyMessage->length = 8;
    *((int*)replyMessage->payload) = returnVal;
    *((int*)(replyMessage->payload + 4)) = errno;
    //printf("returnVal in payload: %d\n", *((int*)replyMessage->payload));
    //printf("errno in payload: %d\n", *((int*)(replyMessage->payload + 4)));
    
    return replyMessage;
}

Message * serverRead(void * messageReceived){
    errno = 0;
    //printf("\n***serverRead***\n");
    char * ptr = (char *)messageReceived;
    
    //extract file descriptor
    int fd = *((int *)(ptr + 5));
    
    //extract nbyte
    size_t nbyte = *((int *)(ptr + 9));

    //printf("nbyte: %zd\n", nbyte);
    
    char buf[nbyte];
    int returnVal = read(fd, buf, nbyte);
   // printf("returnVal after read: %d\n", returnVal);
    if (returnVal == -1){
        //perror("read");
        //printf("errno: %d\n", errno);
    }
    else {
        //printf("read successful\n");
    }
    //printf("returnVal: %d\n", returnVal);
    
    //construct replyMessage
    Message *replyMessage = (Message*)malloc(sizeof(Message));
    replyMessage->payload = (char*)malloc(sizeof(char) * (nbyte + 8));
    replyMessage->length = nbyte + 8;
    *((int*)replyMessage->payload) = returnVal;
    memcpy(replyMessage->payload + 4, buf, nbyte);
    *((int*)(replyMessage->payload + nbyte + 4)) = errno;
    //printf("sending bytes:\n");
    //int i;
    /*
    for (i = 0; i < replyMessage->length; i++){
        printf("at index: %d: %d\n", i, *((char*)(replyMessage->payload + i)));
    }
    */
    //printf("returnVal in payload: %d\n", *((int*)replyMessage->payload));
    //printf("errno in payload: %d\n", *((int*)(replyMessage->payload + nbyte + 4)));
    
    return replyMessage;
}

Message * serverWrite(void * messageReceived){
     errno = 0;
    //printf("\n***serverWrite***\n");
    char * ptr = (char *)messageReceived;
    
    //extract file descriptor
    int fd = *((int*)(ptr + 5)); 
    
    //extract nbyte
    ssize_t nbyte = *((int*)(ptr + 9));

    //printf("serverWrite fd: %d\n", fd); 
    
    int returnVal = write(fd, (ptr + 13), nbyte);
    if (returnVal == -1){
        //perror("write");
    }
    else {
       // printf("write successful\n");
    }
   // printf("fd: %d\n", fd);
    
    //construct replyMessage
    Message *replyMessage = (Message*)malloc(sizeof(Message));
    replyMessage->payload = (char*)malloc(sizeof(char) * 8);
    replyMessage->length = 8;
    *((int*)replyMessage->payload) = returnVal;
    *((int*)(replyMessage->payload + 4)) = errno;
   // printf("returnVal in payload: %d\n", *((int*)replyMessage->payload));
   // printf("errno in payload: %d\n", *((int*)(replyMessage->payload + 4)));
    
    return replyMessage;
}






int main(){
    waitForClients();
    return 0;
}
