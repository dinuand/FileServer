all: client server

client: client.o link_emulator/lib.o
	gcc -g client.o link_emulator/lib.o -o client

server: server.o link_emulator/lib.o
	gcc -g server.o link_emulator/lib.o -o server

.cpp.o: 
	gcc -Wall -g -c $? 

clean:
	rm -f server.o server
