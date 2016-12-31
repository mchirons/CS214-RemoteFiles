#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include "libnetfiles.h"



int clientSocket = -1;


/*
Creates a socket and verifies that host exists.
If successful returns 0 , if not returns -1 and sets
errno appropriately.
*/
int netserverinit(char *hostname, int filemode){
	struct addrinfo hints, *serverinfo, *p;

	int rv;

	if (filemode < 0 || filemode > 2){
		errno = INVALID_FILE_MODE;
		return -1;
	}


	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(hostname, "42602", &hints, &serverinfo)) != 0) {
    	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    	return -1;
	}

	for(p = serverinfo; p != NULL; p = p->ai_next){
		if (((clientSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)){
			//perror("netinit bind socket");
			continue;
		}
		else if (connect(clientSocket, p->ai_addr, p->ai_addrlen) == -1){
			//perror("netinit connect to server");
			continue;
		}
		else {
			//printf("connection made\n");
		}
		break;
	}

	if (p == NULL){
		errno = HOST_NOT_FOUND;
		clientSocket = -1;
		return -1;
	} else {
		int messageLength = 8;
		ssize_t bytesRead = -1;
		int bytes[2];
		bytes[0] = messageLength;
		bytes[1] = filemode;

		ssize_t bytesSent = send(clientSocket, bytes, messageLength, 0);
	    if (bytesSent == -1){
	    	//perror("netinit send");
	    	return -1;
	    }
	    if (bytesSent < messageLength){
	    	//printf("netinit not all bytes sent\n");
	    	return -1;
	    }
	    char buf[4];
	    if ((bytesRead = recv(clientSocket, buf, 4, MSG_WAITALL)) == -1){
			//perror("netinit receive");
			shutdown(clientSocket, 2);
			return -1;		}
		else if (bytesRead < 4){
			//printf("netinit not all bytes received\n");
			//printf("netinit bytesRead: %d\n", (int)bytesRead);
		}
		else {
			//printf("errno before: %d\n", errno);
			
		    if (*((int *)buf) == filemode){
		    	//printf("filemode set successfully\n");
		    }	
		    else {
		    	errno = EBADF;
		    	return -1;
		    }	    
		}
	}

	return 0;
}

/*
Returns file descriptor of file given by pathname
and -1 otherwise while setting errno appropriately.
*/
int netopen(const char *pathname, int flags){

	if (clientSocket == -1){
		errno = ENOTCONN;
		return -1;
	}


	if (flags !=  O_RDONLY && flags != O_WRONLY && flags != O_RDWR){
		errno = EINVAL;
		return -1;
	}

	int returnVal = -1;
    char *bytes = NULL;
    char buf[8];
    int pathlength = strlen(pathname);
    int messageLength = pathlength + 13;
    

    ssize_t bytesRead = -3;

    //constructing the message
    bytes = malloc(sizeof(char)*(messageLength));
    *((int*)bytes) = messageLength;
    *(bytes + 4) = 0;                           // number 0-3 indicating function
    *((int*)(bytes+5)) = pathlength;        // length of message to be read
    memcpy(bytes+9, pathname, pathlength);                  // message to be read
    *((int*)(bytes+9+pathlength)) = flags;  // flag to be interpretted by function
    
    
    ssize_t bytesSent = send(clientSocket, bytes, messageLength, 0);
    if (bytesSent == -1){
    	//perror("nOpen send");
    	return -1;
    }
    if (bytesSent < messageLength){
    	//printf("nOpen not all bytes sent\n");
    	return -1;
    }

    if ((bytesRead = recv(clientSocket, buf, 8, MSG_WAITALL)) == -1){
		//perror("nOpen receive");
		shutdown(clientSocket, 2);
		return -1;
	}
	else if (bytesRead < 8){
		//printf("nOpen not all bytes received\n");
		//printf("nOpen bytesRead: %d\n", (int)bytesRead);
		return -1;
	}
	else {
		//printf("errno before: %d\n", errno);
		errno = *((int *)(buf + 4));
	    returnVal = *((int *)buf);
	    if (returnVal != -1){
	    	 returnVal = returnVal * -1;
	    }
	    //printf("nOpen file descriptor: %d errno: %s value: %d\n", returnVal, strerror(errno), errno);
	}

    return returnVal;
}
            
/*
Closes the file on the server.
Returns 0 on success and -1 on failure while
setting errno appropriately.
*/
int netclose(int fd){

	if (clientSocket == -1){
		errno = ENOTCONN;
		return -1;
	}

	if (fd == -1){
		errno = EBADF;
		return -1;
	}

	fd = fd * -1;
    char *bytes = NULL;
    char buf[8];
    int returnVal = -1;
    ssize_t bytesRead =-3;
    ssize_t bytesSent = -3;

    //formatting the message
    int messageLength = 9;
    bytes = (char*)malloc(sizeof(char)*(messageLength));
    *((int*)bytes) = messageLength;
    *(bytes + 4) = 1;                           
    *((int*)(bytes + 5)) = fd;       
    
    bytesSent = send(clientSocket, bytes, messageLength, 0);
    if (bytesRead == -1){
    	//perror("nClose send");
    	return -1;
    }
    if (bytesSent < messageLength){
    	//printf("nClose not all bytes sent\n");
    	return -1;
    }
  	
    bytesRead = recv(clientSocket, buf, 8, MSG_WAITALL);
    if (bytesRead == -1){
    	//perror("nClose receive");
		shutdown(clientSocket, 2);
		return -1;
    }
    else if (bytesRead < 8){
    	//printf("nClose not all bytes received\n");
		//printf("nClose bytesRead: %d\n", (int)bytesRead);
		return -1;
    }
    else {
    	errno = *((int *)(buf + 4));
	    returnVal = *((int *)buf);
	   	//printf("nClose return val: %d errno: %s value: %d\n", returnVal, strerror(errno), errno);
    }

    return returnVal;
}
            
/*
Reads a number of bytes given by nbyte from the file
given by fildes and writes into buf. Returns number of
bytes read on success and -1 on failure while setting
errno appropriately.
*/
ssize_t netread(int fildes, void *buf, size_t nbyte){

	if (clientSocket == -1){
		errno = ENOTCONN;
		return -1;
	}

	if (fildes == -1){
		errno = EBADF;
		return -1;
	}
	fildes = fildes * -1;

    char *bytes = NULL;
    char buff[nbyte + 8];
    int returnVal = -1;
    ssize_t bytesRead =-3;
    ssize_t bytesSent = -3;

    //formatting the message
    int messageLength = 13;
    bytes = (char*)malloc(sizeof(char)*(messageLength));
    *((int*)bytes) = messageLength;
    *(bytes + 4) = 2;                           
    *((int*)(bytes + 5)) = fildes;  
    *((int*)(bytes + 9)) = nbyte;       
    
    bytesSent = send(clientSocket, bytes, messageLength, 0);
    if (bytesRead == -1){
    	//perror("nRead send");
    	return -1;
    }
    if (bytesSent < messageLength){
    	//printf("nRead not all bytes sent\n");
    	return -1;
    }

    bytesRead = recv(clientSocket, buff, (nbyte + 8), MSG_WAITALL);
    if (bytesRead == -1){
    	//perror("nRead receive");
		shutdown(clientSocket, 2);
		return -1;
    }
    else if (bytesRead < (nbyte + 8)){
    	//printf("nRead not all bytes received\n");
		//printf("nRead bytesRead: %d\n", (int)bytesRead);
		return -1;
    }
    else {
    	errno = *((int *)(buff + nbyte + 4));
    	//printf("nRead errno after extraction: %d\n", *((int *)(buff + nbyte + 4)));
    	
	    returnVal = *((int *)buff);
	    if (returnVal != -1){
	    	//printf("copying to buf\n");
	    	 memcpy(buf, buff + 4, nbyte);
	    }
	    //printf("nRead buf after read:\n");
	    //int i;
	    /*
	    for (i = 0; i < bytesRead; i++){
	    	printf("bytes[%d]: %d\n", i, *((char*)(buff + i)));
	    }
	    for (i = 0; i < nbyte; i++){
	    	printf("buf[%d]: %c\n", i, *((char*)(buf + i)));
	    }
	    */
	   	//printf("nRead bytesRead: %d errno: %s value: %d\n", returnVal, strerror(errno), errno);
    }



    return returnVal;
}            
/*
Writes a number of bytes specified by nbyte from buf to the 
file given by filedes. Returns number of bytes read on 
success and -1 on failure while setting errno appriopriately.
*/
ssize_t netwrite(int fildes, const void *buf, size_t nbyte){
	//printf("nWrite filedes: %d\n", fildes);

	if (clientSocket == -1){
		errno = ENOTCONN;
		return -1;
	}

	if (fildes == -1){
		errno = EBADF;
		return -1;
	}
	fildes = fildes * -1;
    int returnVal = -1;
    char *bytes = NULL;
    char buff[8];
    int messageLength = nbyte + 13;
    

    ssize_t bytesRead = -3;

    //constructing the message
    bytes = malloc(sizeof(char)*(messageLength));
    *((int*)bytes) = messageLength;
    *(bytes + 4) = 3;                          
    *((int*)(bytes+5)) = fildes; 
    *((int*)(bytes+9)) = nbyte;       
    memcpy(bytes+13, buf, nbyte);                 
    
    ssize_t bytesSent = send(clientSocket, bytes, messageLength, 0);
    if (bytesSent == -1){
    	//perror("nWrite send");
    	return -1;
    }
    if (bytesSent < messageLength){
    	//printf("nWrite not all bytes sent\n");
    	return -1;
    }

    if ((bytesRead = recv(clientSocket, buff, 8, MSG_WAITALL)) == -1){
		//perror("nWrite receive");
		shutdown(clientSocket, 2);
		return -1;
	}
	else if (bytesRead < 8){
		//printf("nWrite not all bytes received\n");
		//printf("nWrite bytesRead: %d\n", (int)bytesRead);
		return -1;
	}
	else {
		//printf("errno before: %d\n", errno);
		errno = *((int *)(buff + 4));
	    returnVal = *((int *)buff);

	    //printf("nWrite bytesWritten: %d errno: %s value: %d\n", returnVal, strerror(errno), errno);
	}

    return returnVal;
}