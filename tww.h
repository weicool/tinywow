#ifndef _TWW_H_
#define _TWW_H_
 
/** Standard C/C++ utility libraries. */
#include <cstdlib>
#include <stdio.h>
#include <stdarg.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <cmath>
#include <ctime>

/** Socket API headers. */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

/** General utility files for the project. */
#include "constants.h"
#include "messages.h"

/** Other */
#include <fcntl.h>

#define DEBUG false
extern void debug(const char* format, ...);

/** Null-terminates string at the given position. */
extern void null_terminate(char *string, unsigned int position);

/** Calculates padding size for the speakPacket*/
extern size_t calculatePaddingSize(int length);

/** Calculates P2P ID. */
extern uint32_t calc_p2p_id(unsigned char *name);

/** Prints user data. */
extern void printUserData(struct p2p_user_data user);

/** Returns a random number between low and high, inclusive. */
int random(int low, int high);

class Client;
class Server;
class Tracker;
class ServerEntry;
class TWW;
class UDPHandler;
class Packet;
class UDPPacket;
class Dungeon;
class Player;
class PlayerFactory;
class Peers;

/****** TCP Packet structures ******/

struct tww_packet_header {
    uint8_t version;
    uint16_t total_length;
    uint8_t msg_type;
} __attribute((packed));

struct tww_login_request {
    char name[MAX_LOGIN_LENGTH + 1];
    int hp;
    int exp;
    uint8_t x;
    uint8_t y;
} __attribute((packed));

struct tww_login_reply {
    uint8_t errorCode;
    int hp;
    int exp;
    uint8_t x;
    uint8_t y;
    uint8_t padding;
} __attribute((packed));

struct tww_move {
    uint8_t direction;
    uint8_t padding[3];
} __attribute((packed));

struct tww_move_notify {
    char name[MAX_LOGIN_LENGTH + 1];
    uint8_t x;
    uint8_t y;
    int hp;
    int exp;
} __attribute((packed));

struct tww_attack {
    char name[MAX_LOGIN_LENGTH + 1];
    uint8_t padding[2];
} __attribute((packed));

struct tww_attack_notify {
    char attacker[MAX_LOGIN_LENGTH + 1];
    char victim[MAX_LOGIN_LENGTH + 1];
    uint8_t damage;
    int hp;
    uint8_t padding[3];
} __attribute((packed));

struct tww_speak {
    char *msg;
} __attribute((packed));

struct tww_speak_notify {
    char name[MAX_LOGIN_LENGTH + 1];
    char *msg;
} __attribute((packed));

struct tww_logout_notify {
    char name[MAX_LOGIN_LENGTH + 1];
    int hp;
    int exp;
    uint8_t x;
    uint8_t y;
} __attribute((packed));

struct tww_invalid_state {
    uint8_t errorCode;
    uint8_t padding[3];
} __attribute((packed));

/* tww_logout does not exist b/c there's no payload */

/****** UDP Packet structures ******/

struct udp_packet_header {
    uint8_t message_type;
    uint32_t id;
} __attribute((packed));

struct udp_storage_location_request {
    char name[MAX_LOGIN_LENGTH + 1];
    uint8_t padding;
} __attribute((packed));

struct udp_storage_location_response {
    uint32_t server_ip_address;
    uint16_t server_udp_port;
    uint8_t padding;
} __attribute((packed));

struct udp_player_state_request {
    char name[MAX_LOGIN_LENGTH + 1];
    uint8_t padding;
} __attribute((packed));

struct udp_player_state_response {
    char name[MAX_LOGIN_LENGTH + 1];
    int hp;
    int exp;
    uint8_t x;
    uint8_t y;
    uint8_t padding[3];
} __attribute((packed));

struct udp_server_area_request {
    uint8_t x;
    uint8_t y;
    uint8_t padding;
} __attribute((packed));

struct udp_server_area_response {
    uint32_t server_ip_address;
    uint16_t server_tcp_port;
    uint8_t min_x;
    uint8_t max_x;
    uint8_t min_y;
    uint8_t max_y;
    uint8_t padding;
} __attribute((packed));

struct udp_save_state_request {
    char name[MAX_LOGIN_LENGTH + 1];
    int hp;
    int exp;
    uint8_t x;
    uint8_t y;
    uint8_t padding[3];
} __attribute((packed));

struct udp_save_state_response {
    uint8_t error_code;
    uint16_t padding;
} __attribute((packed));


/****** P2P TCP ******/

struct p2p_join_request {
    uint32_t server_p2p_id;
} __attribute((packed));

struct p2p_user_data {
    char name[MAX_LOGIN_LENGTH + 1];
    int hp;
    int exp;
    uint8_t x;
    uint8_t y;
} __attribute((packed));

struct p2p_join_response {
    uint32_t user_number;
    struct p2p_user_data *user_data_list;
} __attribute((packed));

/* bkup_request does not exist b/c it's the same as p2p_user_data */

struct p2p_bkup_response {
    uint8_t error_code;
    uint8_t padding[3];
} __attribute((packed));

/****** Other ******/

struct client_data {
    Player *player;
    std::vector<unsigned char> *buffer;
};

struct location {
    int x;
    int y;
};

struct range {
    uint16_t high;
    uint16_t low;
};

typedef std::vector<Player *> PlayerList;
typedef std::vector<Packet *> PacketList;
typedef std::vector<UDPPacket *> UDPPacketList;
typedef std::vector<ServerEntry *> ServerEntryList;
typedef std::vector<struct p2p_user_data> UserDataList;

/****** Class declarations ******/

/** Client/Server/Tracker */

class Server {
public:
    Server();
    
    void startServer(uint16_t tcpPort, uint16_t udpPort);
    
    /* Calculate this server's P2P_id, make a server entry. */
    void makeMyServerEntry(uint16_t tcpPort, uint16_t udpPort);
    
    void run();
    
    void updateGame(int clientSocket, unsigned char *buffer, size_t bytesRead);

    /* Sending */
    
    void sendLoginReply(int clientSocket, int errorCode, Player *player);
    
    void broadcastMoveNotify(Player *player);
    
    void sendMoveNotify(int clientSocket, Player *player);
    
    void broadcastAttackNotify(int clientSocket, char *attackerName, char *victimName, 
        int damage, int hp);
    
    void broadcastSpeakNotify(char *playerName, char *msg);
    
    void broadcastLogoutNotify(char *playerName, int hp, int exp, uint8_t x, uint8_t y);
    
    void sendInvalidState(int clientSocket, int errorCode);

    /* Receiving */

    void processLoginRequest(int clientSocket, Packet *packet);

    void processSpeak(int clientSocket, Packet *packet);

    void processMove(int clientSocket, Packet *packet);

    void processAttack(int clientSocket, Packet *packet);

    void processLogout(int clientSocket, Packet *packet);
    
    /* P2P Setup */
    
    void p2pConnectToPeers();
    
    void p2pSetup();
    
    int connectToPeer(std::string ip, uint16_t port);
    
    /* Receiving P2P */
    
    void sendP2PJoinRequest(int serverSocket);
    
    void sendP2PJoinResponse(int serverSocket, UserDataList *userDataList);
    
    void sendP2PBackupRequest(int serverSocket, struct p2p_user_data userData);
    
    void sendP2PBackupResponse(int serverSocket, bool errorCode);

    void processJoinRequest(int clientSocket, Packet *packet);
    
    void processJoinResponse(int clientSocket, Packet *packet);
    
    void processBkupRequest(int clientSocket, Packet *packet);
    
    void processBkupResponse(int clientSocket, Packet *packet);
    
    /* Sending and Receiving UDP */
    
    void sendPlayerStateResponse(uint32_t dstIP, uint16_t dstPort, uint32_t msgID, Player *player);
    
    void sendSaveStateResponse(uint32_t dstIP, uint16_t dstPort, uint32_t msgID, bool success);
    
    bool processUDPPacket(UDPPacket *packet);
    
    void processPlayerStateRequest(UDPPacket *packet);
    
    void processSaveStateRequest(UDPPacket *packet);

    void closeServer();

private:
    void sendAll(int clientSocket, Packet *packet);

    void broadcast(Packet *packet);
    
    void disconnectClient(int clientSocket);
    
    Player * playerOfSocket(int clientSocket);
        
    int maxClientSocket();
    
    bool validPacket(Packet *packet);
    
    int myListeningSocket, myUDPSocket;
    int myPredecessorSocket, mySuccessorSocket, myPrevSuccessorSocket;
    int myP2PState;
    TWW *myTww;
    Dungeon *myDungeon;
    std::map<int, struct client_data> myClients;
    PlayerFactory *myFactory;
    UDPHandler *myUDPHandler;
    ServerEntry *myServerEntry;
    Peers *myPeers;
    uint32_t myIP;
    UserDataList *myBackupDataList;
    bool disconnectPrevSuccessor;
    unsigned int numJoinResponses;
};

class Client {
public:
    Client() {}
    
    Client(uint32_t trackerIP, uint16_t trackerPort);
    
    void runGame();
    
    void readUserInput(char *commandBuffer);
    
    bool updateGame(unsigned char *readBytes, size_t bytesRead);

    /*UDP Auxiliary Methods*/

    void doTCP();
    
    bool doUDP();
    
    bool processUDPPacket(UDPPacket *packet);

    bool updateGameUDP(UDPPacket *packet);

    void connectToServer(uint32_t ip, uint16_t port);

    /* Sending */
    
    void sendLoginRequest(char *playerName);
    
    void sendLogout();
    
    void sendMove(char *direction);

    void sendAttack(char* playerName);

    void sendSpeak(char *msg);

    void sendAll(unsigned char *buffer, size_t length);
    
    /* Receiving */
    
    void processLoginReply(Packet *packet);
    
    void processMoveNotify(Packet *packet);
    
    void processAttackNotify(Packet *packet);
    
    void processSpeakNotify(Packet *packet);
    
    void processLogoutNotify(Packet *packet);
    
    void processInvalidState(Packet *packet);
    
    void closeClient();
    
    /* Sending and Receiving UDP */
    
    void sendStorageLocationRequest();

    void sendPlayerStateRequest();
    
    void sendServerAreaRequest(struct location loc);
    
    void sendSaveStateRequest();
    
    void processStorageLocationResponse(UDPPacket *udppacket);
    
    void processServerAreaResponse(UDPPacket *udppacket);
    
    void processPlayerStateResponse(UDPPacket *packet);
    
    void processSaveStateResponse(UDPPacket *udppacket);

private:    
    int mySocket;
    TWW *myTww;
    Player *myPlayer;
    char *myPlayerName;
    Dungeon *myDungeon;
    int myGameState;
    bool myLoggingOut;
    bool autoSave;
    bool autoSaveDone;
    bool firstTimeLoggedIn;
    std::vector<unsigned char> *myBuffer;

    uint32_t myTrackerIP;
    uint16_t myTrackerPort;
    uint32_t myStorageServerIP, myLocationServerIP;
    uint16_t myStorageServerPort, myLocationServerPort;
    int myUDPSocket;
    UDPHandler *myUDPHandler;
    uint32_t myMsgID;
};

class Tracker {
public:
    Tracker();
    
    void registerServers(std::string filename);
    
    void registerServerRegions();
    
    ServerEntry * serverStoringPlayer(char *s);
    
    ServerEntry * serverResponsibleForArea(uint8_t x, uint8_t y);
    
    void startTracker(int port);
    
    void run();
    
    /* Sending */
    
    void sendStorageLocationResponse(uint32_t dstIP, uint16_t dstPort,
        uint32_t msgID, ServerEntry *server);
        
    void sendServerAreaResponse(uint32_t dstIP, uint16_t dstPort,
        uint32_t msgID, ServerEntry *server);
    
    /* Receiving */
    
    bool processPacket(UDPPacket *packet);
    
    void processStorageLocationRequest(UDPPacket *packet);
    
    void processServerAreaRequest(UDPPacket *packet);

    void closeTracker();

private:    
    int mySocket;
    int myPort;
    UDPHandler *myUDPHandler;
    ServerEntryList *servers;
};

class ServerEntry {
public:
    ServerEntry(unsigned int pid, std::string pip, uint16_t ptcpPort, uint16_t pudpPort) {
        id = pid;
        
        size_t length = pip.size() + 1;
        ip = (char *) malloc(length);
        strncpy(ip, pip.c_str(), length);
        
        tcpPort = ptcpPort;
        udpPort = pudpPort;
        
        backupRange.high = backupRange.low = 0;
        primaryRange.high = primaryRange.low = 0;
    }
    
    void print() {
        debug("server %u - %s, TCP port %u, UDP port %u; primary: [%u, %u], backup: [%u, %u]",
            id, ip, tcpPort, udpPort,
            primaryRange.low, primaryRange.high, backupRange.low, backupRange.high);
    }

    unsigned int id;
    char *ip;
    uint16_t tcpPort, udpPort;
    unsigned int maxX, minX;
    struct range backupRange, primaryRange;
};

/** Protocol */

class TWW {
public:
    TWW() {}
    
    /** Methods to generate packets as an unsigned char array. 
     *  It is the caller's responsibility to free the memory.
     *  Sets length to the number of bytes in returned packet. */
     
    Packet * makeLoginPacket(char *playerName, int hp, int exp, uint8_t x, uint8_t y);
    
    Packet * makeLogoutPacket();
    
    Packet * makeMovePacket(uint8_t dir);

    Packet * makeAttackPacket(char *victim);

    Packet * makeSpeakPacket(char *msg);

    Packet * makeLoginReplyPacket(int errorCode, int hp, int exp, uint8_t x, uint8_t y);

    Packet * makeMoveNotifyPacket(char *playerName, int hp, int exp, uint8_t x, uint8_t y);
    
    Packet * makeAttackNotifyPacket(char *attackerName, char *victimName, int damage, int hp);
        
    Packet * makeSpeakNotifyPacket(char *playerName, char *msg);
    
    Packet * makeLogoutNotifyPacket(char *playerName, int hp, int exp, uint8_t x, uint8_t y);
        
    Packet * makeInvalidStatePacket(int errorCode);

    /* P2P Packets */
    Packet * makeP2PBackupResponse(int errorCode);
    
    Packet * makeP2PBackupRequest(struct p2p_user_data userData);
    
    Packet * makeP2PJoinResponsePacket(UserDataList *userDataList);
    
    Packet * makeP2PJoinRequestPacket(int p2p_id);

    PacketList * parsePackets(unsigned char *buffer, size_t bytesReceived, 
                                std::vector<unsigned char> *clientBuffer);

private:
    /** Generates a packet with the given payload. */
    Packet * makePacket(char messageType, unsigned char *payload, size_t payloadLength);
};

class UDPHandler {
public:
    UDPHandler(int socket, bool resend, bool isTracker);
    
    void run(bool (*handlerFunction)(UDPPacket *));
    
    void send(UDPPacket *packet);

    bool receive(fd_set readfds, bool (*handlerFunction)(UDPPacket *));

    UDPPacket * parsePacket(unsigned char *readBytes, size_t bytesRead,
            struct sockaddr_in *sin);
            
    /** Registers the packet in the receive history. */
    void registerInReceiveHistory(UDPPacket *packet);
    
    bool isDuplicatePacket(UDPPacket *packet);

    UDPPacket * makeStorageLocationRequest(uint32_t ip, uint16_t port,
            uint32_t msgID, char *playerName);
            
    UDPPacket * makeStorageLocationResponse(uint32_t ip, uint16_t port,
        uint32_t msgID, const char *serverIP, uint16_t serverUDPPort);
        
    UDPPacket * makeServerAreaRequest(uint32_t ip, uint16_t port,
        uint32_t msgID, struct location loc);

    UDPPacket * makeServerAreaResponse(uint32_t ip, uint16_t port,
        uint32_t msgID, const char *serverIP, uint16_t serverTCPPort,
        uint8_t minX, uint8_t maxX, uint8_t minY, uint8_t maxY);
        
    UDPPacket * makePlayerStateRequest(uint32_t ip, uint16_t port, 
        uint32_t msgID, char *playerName);

    UDPPacket * makePlayerStateResponse(uint32_t ip, uint16_t port, 
        uint32_t msgID, char *playerName, int hp, int exp, uint8_t x, uint8_t y);

    UDPPacket * makeSaveStateRequest(int32_t ip, uint16_t port,
        uint32_t msgID, char *playerName, int hp, int exp, struct location loc);

    UDPPacket * makeSaveStateResponse(uint32_t ip, uint16_t port, 
        uint32_t msgID, uint8_t errorCode);
            
    UDPPacket * makeUDPPacket(uint32_t ip, uint16_t port, char messageType,
            uint32_t msgID, unsigned char *payload, size_t payloadLength);

private:    
    int mySocket;
    bool myResend, myIgnoreDups, myIsTracker;
    UDPPacket *myLastSentPacket;
    UDPPacketList *myReceiveHistory;
};

class Packet {
public:
    Packet(unsigned char *message, size_t size) {
        packet = message;
        length = size;
    }
    
    virtual ~Packet() {
        free(packet);
    }
    
    virtual uint8_t msgType() {
        return (uint8_t) packet[sizeof(tww_packet_header) - 1];
    }
    
    void print() {
        printf("msg ver:%d len:%lu type:%d raw_pkt(net_byte_order)=[",
                packet[0], length, msgType());
        for (int i = 0; i < length; i++) {
            printf("%02x ", packet[i]);
        }
        printf("]\n");
    }
    
    unsigned char *packet;
    size_t length;
};

class UDPPacket : public Packet {
public:
    UDPPacket(uint32_t pip, uint16_t pport, unsigned char *message, size_t size) :
        Packet(message, size) {
        ip = pip;
        port = pport;
        numTimesSent = 0;
    }
    
    ~UDPPacket() {}
    
    uint32_t id() {
        return (packet[1] << 24) + (packet[2] << 16) + (packet[3] << 8) + packet[4];
    }
    
    virtual uint8_t msgType() {
        return (uint8_t) packet[0];
    }

    /** Number of milliseconds to wait before resending this packet. */
    unsigned int timeToWait() {
        unsigned int expiration = (1 << (numTimesSent - 1)) * 100;
        return expiration;
    }
    
    uint32_t ip;
    uint16_t port;
    unsigned numTimesSent;
    struct timeval timeSent;
};


/** Game */

class Dungeon {
public:
    Dungeon(unsigned int width, unsigned int height);

    void addPlayer(Player *player);
    
    void removePlayer(Player *player);
    
    /** Clears the dungeon of all players. Deletes all players except for mainPlayer. */
    void clear(Player *mainPlayer);
    
    void movePlayer(Player *player, int direction);
        
    void setBoundary(uint8_t min_x,uint8_t min_y, uint8_t max_x, uint8_t max_y);
    
    void computeMovePlayer(struct location oldLocation, int direction,
        struct location *newLocation);
    
    bool withinBoundary(struct location loc);
    
    void printBoundary(struct location loc);
        
    /** Is otherPlayer in the vision of player? */
    bool inVision(Player *player, Player *otherPlayer);
    
    Player *findPlayer(char *name);
    
    bool locationOccupied(int x, int y);
    
    void playersInRangeOf(Player *player, PlayerList &players);
    
    void incrementHPForAllPlayers();
    
    void print();
    
private:
    int findPlayerIndex(char *name);
    
    unsigned int myWidth, myHeight;
    uint8_t myMinX, myMaxX, myMinY, myMaxY;

    PlayerList *myPlayers;
};

class Player {
public:
    Player(char *playerName, int hp, int exp, struct location loc) {
        size_t length = strlen(playerName) + 1;
        myName = (char *) malloc(length);
        strncpy(myName, playerName, length);
        myHp = hp;
        myExp = exp;
        myLocation = loc;
    }
    
    ~Player() {
        debug("deleted player %s", myName);
        free(myName);
    }
    
    void print() {
        debug("Player Info => Name:%s HP:%d EXP:%d Location:(%d, %d)",
            myName, myHp, myExp, myLocation.x, myLocation.y);
    }

    char *myName;
    int myHp, myExp;
    struct location myLocation;
};

/** Responsible for making and deleting players, and keeping player state on disk. */
class PlayerFactory {
public:
    PlayerFactory();

    /** Creates a player with the given name. Player data is either loaded from
     *  disk or generated randomly and saved to disk. */
    Player * newPlayerFromFile(char *playerName);
    
    /** Creates a new player with the given state. */
    Player * newPlayer(char *playerName, int hp, int exp, uint8_t x, uint8_t y);
    
    /** Saves the player's data to a file on disk. */
    bool savePlayer(char *playerName, int hp, int exp, uint8_t x, uint8_t y);
        
    /** Deletes the player. */
    void destroyPlayer(Player *player);
    
    void resurrectPlayer(Player *deadPlayer);
    
private:
    int randomHP(int low, int high);
    
    struct location randomLocation();
};


class Peers {
public:
    Peers(std::string filename, ServerEntry *thisServer);

    /** Reads peers.lst, updates my peers and adds this server to the list. */
    void readPeers();
    
    void registerServer(ServerEntry *server);
    
    void calculateRanges();

    int whoIsMyPeer(int p2pID);
    
    ServerEntry *findSuccessor(ServerEntry *server);
    
    ServerEntry *findPredecessor(ServerEntry *server);
    
    UserDataList *findUsersInRange(uint16_t min, uint16_t max);

    bool writeUserDataToDisk(struct p2p_user_data);
    
private:
    /** Clears the list of peers, making sure not to clear myServer. */
    void clearPeers();
    
    ServerEntry *findPeer(ServerEntry *server, int incrementBy);
    
    int findServerIndex(ServerEntry *server);
    
    struct p2p_user_data loadUser(char *name);
    
    ServerEntryList *myPeers;
    ServerEntry *myServer;
    std::string myFileName;
    bool myFirstTimeRead;
};

#endif
