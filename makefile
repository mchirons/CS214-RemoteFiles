make: netfileserver.c netfileserver.h
	gcc -Wall -g -pthread -o netfileserver netfileserver.c 
	
clean :
	rm netfileserver
