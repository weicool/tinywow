#!gmake

CC = g++ -Wall

SERVER_OBJECTS = server.o tww.o dungeon.o player_factory.o utilities.o udp_handler.o peers.o

OPTS = -g -lsocket -lnsl

##################################

default: server
clean:
	/bin/rm -f *.o client server tracker

##################################

# client.o: client.cpp tww.h
# client: $(CLIENT_OBJECTS)
#   $(CC) $(CLIENT_OBJECTS) $(OPTS) -o client
	
server.o: server.cpp tww.h
server: $(SERVER_OBJECTS)
	$(CC) $(SERVER_OBJECTS) $(OPTS) -o server

# tracker.o: client.cpp tww.h
# tracker: $(TRACKER_OBJECTS)
#   $(CC) $(TRACKER_OBJECTS) $(OPTS) -o tracker

##################################

tww.o: tww.cpp tww.h
dungeon.o: dungeon.cpp tww.h
player_factory.o: player_factory.cpp tww.h
utilities.o: utilities.cpp tww.h
udp_handler.o: udp_handler.cpp tww.h
peers.o: peers.cpp tww.h
