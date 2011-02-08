#include "tww.h"
#include <csignal>

using namespace std;

static bool processUDPPacketFnc(UDPPacket *packet);
static void handleSigTerm(int param);

Server server;

Server::Server() {
    myListeningSocket = 0;
    myUDPSocket = 0;
    myTww = new TWW();
    myDungeon = new Dungeon(DUNGEON_SIZE_X, DUNGEON_SIZE_Y);
    myFactory = new PlayerFactory();
    myUDPHandler = NULL;
    /* myClients has already been instantiated and put on the server object's 
    stack because in TWW.h it was declared not a pointer but an object*/
    
    /* P2P logic */
    myServerEntry = NULL;
    myPredecessorSocket = mySuccessorSocket = myPrevSuccessorSocket = 0;
    myP2PState = P2P_INACTIVE;
    myIP = 0;
    disconnectPrevSuccessor = false;
    numJoinResponses = 0;
}

void Server::startServer(uint16_t tcpPort, uint16_t udpPort) {
    debug("starting server on TCP port %u and UDP port %u", tcpPort, udpPort);
    
    /* TCP */
    int optval = 1;
    myListeningSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (myListeningSocket < 0) {
        on_server_failure();
    }
    if (setsockopt(myListeningSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        on_server_failure();
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(tcpPort);

    if (bind(myListeningSocket, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        on_server_port_bind_failure();
        exit(1);
    }

    if (listen(myListeningSocket, MAX_NUM_CLIENTS)) {
        on_server_failure();
        exit(1);
    }
    
    /* UDP */
    myUDPSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (myUDPSocket < 0) {
        on_server_failure();
    }
    if (setsockopt(myUDPSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        on_server_failure();
    }
    
    struct sockaddr_in sin_udp;
    memset(&sin_udp, 0, sizeof(sin_udp));
    sin_udp.sin_family = AF_INET;
    sin_udp.sin_addr.s_addr = INADDR_ANY;
    sin_udp.sin_port = htons(udpPort);
    
    if (bind(myUDPSocket, (struct sockaddr *) &sin_udp, sizeof(sin_udp)) < 0) {
        on_server_port_bind_failure();
        exit(1);
    }
    
    myUDPHandler = new UDPHandler(myUDPSocket, false, false);
  
    makeMyServerEntry(tcpPort,udpPort);

    myServerEntry->print();
    myPeers = new Peers(string("peers.lst"), myServerEntry);    

    on_server_init_success();
    
    printf("P2P: p2p_myid = %u \n", myServerEntry->id);
}

void Server::makeMyServerEntry(uint16_t tcpPort, uint16_t udpPort) {
    char hostname[255];
    gethostname(hostname, 255);
    struct hostent *me = gethostbyname(hostname);
    struct in_addr *ipSender = (struct in_addr *) me->h_addr_list[0];
    myIP = ntohl(ipSender->s_addr);

    /* Find my IP for P2P setup. */
    unsigned int ipNum[4];
    unsigned char name[7];
    debug(inet_ntoa(*ipSender));
    sscanf(inet_ntoa(*ipSender), "%u.%u.%u.%u", ipNum+0, ipNum+1, ipNum+2, ipNum+3);
    for (unsigned int i = 0; i < 4; i++) {
        name[i] = ipNum[i];
    } 
    name[4] = tcpPort >> 8;
    name[5] = (tcpPort << 8) >> 8;
    name[6] = '\0';
     
    int p2p_id = calc_p2p_id(name);    
    myServerEntry = new ServerEntry(p2p_id, string(inet_ntoa(*ipSender)), tcpPort, udpPort);

}

void Server::run() {
    debug("running Server");

    unsigned char readBytes[MAX_PACKET_LENGTH];

    int newClientSocket, clientSocket;
    map<int, struct client_data>::iterator clientDataIter;

    while (true) {
        struct timeval timeSelect;
        timeSelect.tv_sec = 0;
        timeSelect.tv_usec = 10000;
    
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(myListeningSocket, &readfds);
        FD_SET(myUDPSocket, &readfds);

        /* Add all client sockets to select set. */
        for (clientDataIter = myClients.begin(); clientDataIter != myClients.end(); clientDataIter++) {
            FD_SET(clientDataIter->first, &readfds);
        }

        unsigned int maxSocket = max(myUDPSocket, max(myListeningSocket, maxClientSocket()));
        int selectVal = select(maxSocket + 1, &readfds, NULL, NULL, &timeSelect);
        if (selectVal < 0) {
            exit(1);
        }

        if (FD_ISSET(myListeningSocket, &readfds)) {
            debug("Listening socket heard a request!");

            struct sockaddr_in client_sin;
            int clientAddressLength = sizeof(client_sin);
            newClientSocket = accept(myListeningSocket, (struct sockaddr *) &client_sin, 
                                        (socklen_t *) &clientAddressLength);
            if (newClientSocket < 0) {
            } else {
                fprintf(stdout, "New connection from %s.%d. fd=%d\n", 
                    inet_ntoa(client_sin.sin_addr), 
                    ntohs(client_sin.sin_port),
                    newClientSocket);

                struct client_data clientData;
                clientData.player = NO_PLAYER;
                clientData.buffer = new vector<unsigned char>();
                myClients.insert(pair<int, struct client_data>(newClientSocket, clientData));
            }
        }
        
        if (FD_ISSET(myUDPSocket, &readfds)) {
            debug("UDP socket receiving");

            myUDPHandler->receive(readfds, processUDPPacketFnc);
        }
        
        p2pSetup();
        
        for (clientDataIter = myClients.begin(); clientDataIter != myClients.end();) {
            clientSocket = clientDataIter->first;
            if (FD_ISSET(clientSocket, &readfds)) {
                size_t bytesRead = recv(clientSocket, readBytes, MAX_PACKET_LENGTH, 0);
                try {
                    if (bytesRead <= 0) {
                        debug("client disconnected");
                        throw -1;
                    }
                    
                    updateGame(clientSocket, readBytes, bytesRead);
                } catch (int e) {
                    if (disconnectPrevSuccessor) {
                        debug("YO BITCHES! I'M DISCONNECTING FROM MY PREVIOUS SUCCESSOR");
                        ServerEntry *newSuccessor = myPeers->findSuccessor(myServerEntry);
                        mySuccessorSocket = connectToPeer(newSuccessor->ip, newSuccessor->tcpPort);
                        printf("P2P: connect to suc %d. p2pfd %d \n", newSuccessor->id, mySuccessorSocket);
                        clientSocket = myPrevSuccessorSocket;
                        disconnectPrevSuccessor = false;
                        if (myPrevSuccessorSocket == 0) {
                            debug("YO BITCHES! MY PREV SUCCESSOR IS ZERO!");
                            clientDataIter++;
                            continue;
                        }
                    }
                    
                    FD_CLR(clientSocket, &readfds);
                    disconnectClient(clientSocket);
                    clientDataIter = myClients.begin();
                    continue;
                }
            }
            clientDataIter++;
        }
    }
}

void Server::updateGame(int clientSocket, unsigned char *buffer, size_t bytesRead) {
    map<int, struct client_data>::iterator clientDataIter = myClients.find(clientSocket);
    if (clientDataIter == myClients.end()) {
        on_server_failure();
    }

    PacketList *packetsParsed = myTww->parsePackets(buffer, bytesRead,
            clientDataIter->second.buffer);
    debug("number of packets: %u", packetsParsed->size());
    Packet *packet;
    bool error = false;

    try {
        for (unsigned int i = 0; i < packetsParsed->size(); i++) {
            packet = packetsParsed->at(i);
            printf("- fd:%d received ", clientSocket);
            packet->print();
            switch (packet->msgType()) {
                case LOGIN_REQUEST:
                    debug("LOGIN");
                    processLoginRequest(clientSocket, packet);
                    break;
                case MOVE:
                    debug("MOVE");
                    processMove(clientSocket, packet);
                    break;
                case ATTACK:
                    debug("ATTACK");
                    processAttack(clientSocket, packet);
                    break;
                case SPEAK:
                    debug("SPEAK");
                    processSpeak(clientSocket, packet);
                    break;
                case LOGOUT:
                    debug("LOGOUT");
                    processLogout(clientSocket, packet);
                    break;
                /* P2P MESSAGES */
                case JOIN_REQUEST:
                    debug("JOIN_REQUEST");
                    processJoinRequest(clientSocket, packet);
                    break;
                case JOIN_RESPONSE:
                    debug("JOIN_RESPONSE");
                    processJoinResponse(clientSocket, packet);
                    debug("YO BITCHES WE'VE RECEIVED %u JOIN RESPONSES", numJoinResponses);
                    break;
                case BKUP_REQUEST:
                    debug("BKUP_REQUEST");
                    processBkupRequest(clientSocket, packet);
                    break;
                case BKUP_RESPONSE:
                    debug("BKUP_RESPONSE");
                    processBkupResponse(clientSocket, packet);
                    break;
                default:
                    debug("DISCONNECT");
                    throw -1;
            }
        }
    } catch (int e) {
        error = true;
    }

    for (int i = 0; i < packetsParsed->size(); i++) {
        delete packetsParsed->at(i);
    }
    delete packetsParsed;
    
    if (error) {
        throw -1;
    }
}

void Server::processLoginRequest(int clientSocket, Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_login_request)) {
        debug("processLogin: corrupt packet");
        throw -1;
    }

    struct tww_login_request *login;
    login = (struct tww_login_request *) (packet->packet + sizeof(tww_packet_header));

    if (!check_player_name(login->name)) {
        throw -1;
    }

    int errorCode;
    map<int, struct client_data>::iterator clientDataIter = myClients.find(clientSocket);

    if (clientDataIter->second.player) {
        /* client already logged in */
        errorCode = 1;
        sendInvalidState(clientSocket, errorCode);
    } else if (myDungeon->findPlayer(login->name)) {
        /* a player already logged in with the same name */
        errorCode = 1;
        sendLoginReply(clientSocket, errorCode, NULL);
    } else {
        errorCode = 0;
        Player *player = myFactory->newPlayer(login->name, ntohl(login->hp), ntohl(login->exp), login->x, login->y);
        clientDataIter->second.player = player;
        myDungeon->addPlayer(player);
        
        sendLoginReply(clientSocket, errorCode, player);
        /* broadcast MOVE_NOTIFY of this new player to existing players */
        broadcastMoveNotify(player);
        /* for each existing player, send MOVE_NOTIFY of that player to this new player */
        Player *otherPlayer;
        for (clientDataIter = myClients.begin(); clientDataIter != myClients.end(); clientDataIter++) {
            otherPlayer = clientDataIter->second.player;
            if (otherPlayer && otherPlayer != player) {
                sendMoveNotify(clientSocket, otherPlayer);                
            }
        }
    }
}

void Server::processMove(int clientSocket, Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_move)) {
        debug("processMove: corrupt packet");
        throw -1;
    }
    
    Player *player = playerOfSocket(clientSocket);
    if (!player) {
        sendInvalidState(clientSocket, 0);
        return;
    }
    
    struct tww_move *move;
    move = (struct tww_move *) (packet->packet + sizeof(tww_move));
    if (move->direction == NORTH || move->direction == SOUTH ||
        move->direction == EAST || move->direction == WEST) {
        myDungeon->movePlayer(player, move->direction);
    } else {
        debug("processMove: corrupt packet");
        throw -1;        
    }
    
    broadcastMoveNotify(player);
}

void Server::processAttack(int clientSocket, Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_attack)) {
        debug("processAttack: corrupt packet");
        throw -1;
    }
    
    Player *attacker = playerOfSocket(clientSocket);
    if (!attacker) {
        sendInvalidState(clientSocket, 0);
        return;
    }
    
    struct tww_attack *attack;
    attack = (struct tww_attack *) (packet->packet + sizeof(tww_packet_header));
    if (!check_player_name(attack->name)) {
        debug("processAttack: invalid victim name");
        return;
    }
    
    Player *victim = myDungeon->findPlayer(attack->name);
    if (!victim) {
        debug("processAttack: victim doesn't exist");
        return;
    }
    if (victim == attacker) {
        debug("processAttack: victim is the same as the attacker");
        return;
    }
    
    int damage = random(10, 20);
    if (damage > victim->myHp) {
        damage = victim->myHp;
    }
    victim->myHp -= damage;
    assert(victim->myHp >= 0);
    attacker->myExp += damage;
    debug("%s attacked %s. damage:%d hp:%d exp:%d", attacker->myName, victim->myName,
       damage, victim->myHp, attacker->myExp);

    broadcastAttackNotify(clientSocket, attacker->myName, victim->myName, damage, victim->myHp);

    if (victim->myHp == 0) {
        myFactory->resurrectPlayer(victim);
    }
}

void Server::processSpeak(int clientSocket, Packet *packet) {
    if (packet->length > sizeof(tww_packet_header) + MAX_MSG_LENGTH + 1) {
        debug("processSpeak: corrupt packet");
        throw -1;
    }
    
    Player *player = playerOfSocket(clientSocket);
    if (!player) {
        sendInvalidState(clientSocket, 0);
        return;
    }
    
    char *packetMsg = (char *) packet->packet + sizeof(tww_packet_header);
    
    /*Check msg if it is malicious.*/
    if (!check_player_message(packetMsg)) {
        debug("processSpeak: corrupt message");
        throw -1;
    }

    debug("%s said: %s", player->myName, packetMsg);
    broadcastSpeakNotify(player->myName, packetMsg);
}

void Server::processLogout(int clientSocket, Packet *packet) {
    if (packet->length != sizeof(tww_packet_header)) {
        debug("processLogout: corrupt packet");
        throw -1;
    }
    
    Player *player = playerOfSocket(clientSocket);
    if (!player) {
        sendInvalidState(clientSocket, 0);
        return;
    }

    throw -1;
}

void Server::p2pSetup() {
    ServerEntry *successor;
    ServerEntry *predecessor;
    switch (myP2PState) {
        case P2P_INACTIVE:
            debug("P2PState P2P_INACTIVE");
            myPeers->readPeers();
            p2pConnectToPeers();
            break;
        case P2P_SEND_JOIN:
            debug("P2PState P2P_SEND_JOIN");
            predecessor = myPeers->findPredecessor(myServerEntry);
            printf("send P2P_JOIN_REQUEST to predc %d (%s:%d)", predecessor->id, predecessor->ip, predecessor->tcpPort);
            sendP2PJoinRequest(myPredecessorSocket);
            if (myPredecessorSocket != mySuccessorSocket) {
                successor = myPeers->findSuccessor(myServerEntry);
                printf("send P2P_JOIN_REQUEST to suc %d (%s:%d)", successor->id, successor->ip, successor->tcpPort);
                sendP2PJoinRequest(mySuccessorSocket);

            }
            myP2PState = P2P_RECEIVE_JOIN;
            break;
        case P2P_FIND_NEW_SUCCESSOR:
            debug("P2PState P2P_FIND_NEW_SUCCESSOR");
            myPeers->readPeers();
            successor = myPeers->findSuccessor(myPeers->findSuccessor(myServerEntry));
            mySuccessorSocket = successor ? connectToPeer(successor->ip, successor->tcpPort) : 0;
            if (mySuccessorSocket == 0) {
                debug("OH NOES cannot connect to new successor");
            }
            myP2PState = P2P_ACTIVE;
            break;
        case P2P_ACTIVE:
            break;
        case P2P_RECEIVE_JOIN:
            debug("P2PState P2P_RECEIVE JOIN");
            if (numJoinResponses == 2) {
                debug("recevied both join responses");
                numJoinResponses = 0;
                myP2PState = P2P_ACTIVE;
            }
            break;
        default:
            debug("FAIL: Invalid myP2PState!");
            throw -1;
            break;
    }
}

void Server::p2pConnectToPeers() {
    debug("p2pConnectToPeers");
    
    ServerEntry *predecessor = myPeers->findPredecessor(myServerEntry);
    ServerEntry *successor = myPeers->findSuccessor(myServerEntry);
    
    if (!predecessor ^ !successor) {
        debug("FAIL: found only one of predecessor and successor");
        throw -1;
    } else if (!(predecessor || successor)) {
        printf("P2P: I'm the first.\n");
        debug("all alone in the dark");
        myP2PState = P2P_ACTIVE;
    } else {
        if (predecessor == successor) {
            debug("2 servers");
            myPredecessorSocket = mySuccessorSocket = connectToPeer(predecessor->ip, predecessor->tcpPort);
        } else {
            myPredecessorSocket = connectToPeer(predecessor->ip, predecessor->tcpPort);
            mySuccessorSocket = connectToPeer(successor->ip, successor->tcpPort);
        }
        printf("P2P: find predecessor %d, find successor %d \n", predecessor->id, successor->id);
        myP2PState = P2P_SEND_JOIN;
    }
    
    if (myPredecessorSocket == -1 || mySuccessorSocket == -1) {
        throw -1;
    }
}

int Server::connectToPeer(string ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    debug("Communicating through socket %d", sock);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(ip.c_str());
    sin.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        debug("Failed to connect!");
        return -1;
    }
    
    struct client_data clientData;
    clientData.player = NO_PLAYER;
    clientData.buffer = new vector<unsigned char>();
    myClients.insert(pair<int, struct client_data>(sock, clientData));
    
    return sock;
}

void Server::processJoinRequest(int serverSocket, Packet *packet) {
    debug("processJoinRequest");
    if (packet->length != (sizeof(tww_packet_header) + sizeof(p2p_join_request))) {
        debug("processJoinRequest: corrupt packet");
        throw -1;
    }
    struct p2p_join_request *join;
    join = (struct p2p_join_request *) (packet->packet + sizeof(tww_packet_header));

    join->server_p2p_id = ntohl(join->server_p2p_id);
    myPeers->readPeers();
    int peer_type = myPeers->whoIsMyPeer(join->server_p2p_id);
    UserDataList *user_data_list;
    ServerEntry *predecessor, *successor;
    switch (peer_type) {
        case BOTH:
        //This server is BOTH my successor and predecessor
        //First do the PREDECESSOR portion, then SUCCESSOR portion
        //Yay for switch cases, just let this fall through
        case PREDECESSOR:
        //This server is MY predecessor, which means I do the successor's part
        //I want to find all user data in the primary range of MY predecessor
            printf("P2P: %d is my predecessor.\n", join->server_p2p_id);
            myPredecessorSocket = serverSocket;
            predecessor = myPeers->findPredecessor(myServerEntry);
            assert(predecessor->id == join->server_p2p_id);
            user_data_list = myPeers->findUsersInRange(myServerEntry->backupRange.low, 
                myServerEntry->backupRange.high);
            sendP2PJoinResponse(serverSocket, user_data_list);
            printf("P2P: send P2P_JOIN_RESPONSE to predc %u (%lu users)", predecessor->id, user_data_list->size());
            delete user_data_list;
            if (peer_type == PREDECESSOR) {
                break;
            }
        case SUCCESSOR:
        //This server is MY successor, which means I do the predecessor's part
        //I want to find all user data in MY primary range
            printf("P2P: %d is my successor.\n", join->server_p2p_id);
            myPrevSuccessorSocket = mySuccessorSocket;
            mySuccessorSocket = serverSocket;
            successor = myPeers->findSuccessor(myServerEntry);
            assert(successor->id == join->server_p2p_id);
            user_data_list = myPeers->findUsersInRange(myServerEntry->primaryRange.low, 
                myServerEntry->primaryRange.high);
            sendP2PJoinResponse(serverSocket, user_data_list);
            printf("P2P: send P2P_JOIN_RESPONSE to suc %u (%lu users)", successor->id, user_data_list->size());
            delete user_data_list;
            debug("disconnect myPrevSuccessorSocket:%d", myPrevSuccessorSocket);
            disconnectPrevSuccessor = true;
            throw -1;
            break;
        case NOBODY:
            debug("P2P_id: %d is not adjacant to me!", join->server_p2p_id);
            break;
        default:
            debug("Invalid peer type!");
            throw -1;
            break;
    }
}

void Server::processJoinResponse(int serverSocket, Packet *packet) {
    unsigned int user_number = packet->packet[sizeof(tww_packet_header)];
    printf("P2P: recv P2P_JOIN_RESPONSE (%u users)\n", user_number);
    user_number = ntohl(user_number);
    debug("Number of bytes total for user_data_list: %u", user_number*sizeof(p2p_user_data));

    // if (packet->length != (sizeof(tww_packet_header) + sizeof(uint32_t) + (user_number*sizeof(p2p_user_data)))) {
    //  debug("processJoinResponse: corrupt packet");
    //  throw -1;
    // }
    
    struct p2p_user_data *user_data_list = (struct p2p_user_data *) (packet->packet + sizeof(tww_packet_header) + sizeof(int));
    struct p2p_user_data user_data;

    for (int i = 0; i < user_number; i++) {
        user_data = user_data_list[i];
        user_data.hp = ntohl(user_data.hp);
        user_data.exp = ntohl(user_data.exp);
        myPeers->writeUserDataToDisk(user_data);
    }
    
    numJoinResponses++;
    
    if (myPredecessorSocket != mySuccessorSocket &&
        serverSocket == myPredecessorSocket) {
        throw -1;
    }
}

void Server::processBkupRequest(int serverSocket, Packet *packet) {
    if (packet->length != (sizeof(tww_packet_header) + sizeof(p2p_user_data))) {
        debug("processBkupRequest: corrupt packet");
        throw -1;
    }
    struct p2p_user_data *bk_user_data;
    bk_user_data = (struct p2p_user_data *) (packet->packet + sizeof(tww_packet_header));
    bk_user_data->hp = ntohl(bk_user_data->hp);
    bk_user_data->exp = ntohl(bk_user_data->exp);
    bool errorcode = myPeers->writeUserDataToDisk(*bk_user_data);
    sendP2PBackupResponse(serverSocket, errorcode);
}

void Server::processBkupResponse(int serverSocket, Packet *packet) {
    debug("processBkupResponse");
    if (packet->length != (sizeof(tww_packet_header) + sizeof(p2p_bkup_response))) {
        debug("processBkupResponse: corrupt packet");
        throw -1;
    }
    struct p2p_bkup_response *bkup;
    bkup = (struct p2p_bkup_response *) (packet->packet + sizeof(tww_packet_header));
    debug("Error code returned is: %c", bkup->error_code);

}

void Server::sendLoginReply(int clientSocket, int errorCode, Player *player) {
    Packet *packet;
    if (errorCode == 1 && !player) {
        packet = myTww->makeLoginReplyPacket(errorCode, 0, 0, 0, 0);
    } else {
        packet = myTww->makeLoginReplyPacket(errorCode, player->myHp, player->myExp,
                            player->myLocation.x, player->myLocation.y);
    }

    sendAll(clientSocket, packet);
    delete packet;
}

void Server::broadcastMoveNotify(Player *player) {
    Packet *packet = myTww->makeMoveNotifyPacket(player->myName, player->myHp,
                player->myExp, player->myLocation.x, player->myLocation.y);
    broadcast(packet);
    delete packet;
}

void Server::sendMoveNotify(int clientSocket, Player *player) {
    Packet *packet = myTww->makeMoveNotifyPacket(player->myName, player->myHp,
                player->myExp, player->myLocation.x, player->myLocation.y);
    sendAll(clientSocket, packet);
    delete packet;
}

void Server::broadcastAttackNotify(int clientSocket, char *attackerName, 
    char *victimName, int damage, int hp) {
    debug("I'm in your broadcastAttackNotify, makin' ur packet!!");
    Packet *packet = myTww->makeAttackNotifyPacket(attackerName, victimName,
        damage, hp);
    debug("Lolz I sendAll these packets!");
    broadcast(packet);
    delete packet;
}

void Server::broadcastSpeakNotify(char *playerName, char *msg) {
    Packet *packet = myTww->makeSpeakNotifyPacket(playerName, msg);
    broadcast(packet);
    delete packet;
}

void Server::broadcastLogoutNotify(char *playerName, int hp, int exp, uint8_t x, uint8_t y) {
    Packet *packet = myTww->makeLogoutNotifyPacket(playerName, hp, exp, x, y);
    broadcast(packet);
    delete packet;
}

void Server::sendInvalidState(int clientSocket, int errorCode) {
    Packet *packet = myTww->makeInvalidStatePacket(errorCode);
    sendAll(clientSocket, packet);
    delete packet;
}

void Server::broadcast(Packet *packet) {
    map<int, struct client_data>::iterator clientDataIter;
    for (clientDataIter = myClients.begin(); clientDataIter != myClients.end(); clientDataIter++) {
        if (clientDataIter->second.player) {
            sendAll(clientDataIter->first, packet);            
        }
    }
}

void Server::sendAll(int clientSocket, Packet *packet) {
    printf("- fd:%d sending ", clientSocket);
    packet->print();
            
    unsigned char *buffer = packet->packet;
    size_t length = packet->length;
    
    /* Courtesy Beej's Guide. */
    size_t totalSent = 0;
    size_t bytesleft = length;
    size_t n;

    while (totalSent < length) {
        n = send(clientSocket, buffer + totalSent, bytesleft, 0);
        if (n == -1) {
            throw -1;
        }
        totalSent += n;
        bytesleft -= n;
        debug("SENDING!");
    }

    if (totalSent != length) {
        debug("Houston, we have a problem; sendAll() is being whack");
        throw -1;
    }
}

void Server::closeServer() {
    close(myListeningSocket);
}

bool Server::processUDPPacket(UDPPacket *packet) {
    if (DEBUG) {
        struct in_addr ipSender = { ntohl(packet->ip) };
        printf("received UDP packet #%u from %s at port %u - ", packet->id(), inet_ntoa(ipSender), packet->port);
        packet->print();
    }
    if (!packet) {
        return false;
    }

    switch (packet->msgType()) {
        case PLAYER_STATE_REQUEST:
            debug("PLAYER_STATE_REQUEST");
            processPlayerStateRequest(packet);
            break;
        case SAVE_STATE_REQUEST:
            debug("SAVE_STATE_REQUEST");
            processSaveStateRequest(packet);
            break;
        default:
            debug("DISCONNECT");
            throw -1;
    }
    return false;
}

static bool processUDPPacketFnc(UDPPacket *packet) {
    return server.processUDPPacket(packet);
}

void Server::processPlayerStateRequest(UDPPacket *packet) {
    if (packet->length != (sizeof(udp_player_state_request) + sizeof(udp_packet_header))) {
        debug("processSaveStateResponse: incorrect packet length");
        throw -1;
    }
    
    struct udp_player_state_request *player_state_request;
    player_state_request = (struct udp_player_state_request *)
        (packet->packet + sizeof(udp_packet_header));
    
    if (!check_player_name(player_state_request->name)) {
        debug("ERROR: invalid player name %s", player_state_request->name);
        throw -1;
    }
    
    Player *player = myFactory->newPlayerFromFile(player_state_request->name);
    sendPlayerStateResponse(packet->ip, packet->port, packet->id(), player);
}

void Server::processSaveStateRequest(UDPPacket *packet) {
    if (packet->length != (sizeof(udp_save_state_request) + sizeof(udp_packet_header))) {
        debug("processSaveStateResponse: incorrect packet length");
        throw -1;
    }
    
    struct udp_save_state_request *save_state_request;
    save_state_request = (struct udp_save_state_request *)
        (packet->packet + sizeof(udp_packet_header));
    

    if (!check_player_name(save_state_request->name)) {
        debug("ERROR: invalid player name %s", save_state_request->name);
        throw -1;
    }
    
    save_state_request->hp = ntohl(save_state_request->hp);
    save_state_request->exp = ntohl(save_state_request->exp);
    
    if (save_state_request->hp < 0 || save_state_request->exp < 0) {
        debug("ERROR: hp and/or exp negative");
        throw -1;
    }
    
    bool success = myFactory->savePlayer(save_state_request->name,
        save_state_request->hp, save_state_request->exp, save_state_request->x, save_state_request->y);
    sendSaveStateResponse(packet->ip, packet->port, packet->id(), success);
    
    if (mySuccessorSocket != 0) {
        struct p2p_user_data user;
        strncpy(user.name, save_state_request->name, MAX_LOGIN_LENGTH+1);
        user.x = save_state_request->x;
        user.y = save_state_request->y;
        user.hp = htonl(save_state_request->hp);
        user.exp = htonl(save_state_request->exp);
        sendP2PBackupRequest(mySuccessorSocket, user);        
    }
}

void Server::sendPlayerStateResponse(uint32_t dstIP, uint16_t dstPort, uint32_t msgID, Player *player) {
    debug("sendPlayerStateResponse!");
    UDPPacket *packet = myUDPHandler->makePlayerStateResponse(dstIP, dstPort,
        msgID, player->myName, player->myHp, player->myExp, player->myLocation.x, player->myLocation.y);
    myUDPHandler->send(packet);
    delete player;
    delete packet;
}

void Server::sendSaveStateResponse(uint32_t dstIP, uint16_t dstPort, uint32_t msgID, bool success) {
    debug("sendSaveStateResponse!");
    UDPPacket *packet = myUDPHandler->makeSaveStateResponse(dstIP, dstPort, msgID, success ? 0 : 1);
    myUDPHandler->send(packet);
    delete packet;
}

void Server::sendP2PJoinRequest(int serverSocket) {
    Packet *packet = myTww->makeP2PJoinRequestPacket(myServerEntry->id);
    sendAll(serverSocket, packet);
    delete packet;
}

void Server::sendP2PJoinResponse(int serverSocket, UserDataList *userDataList) {
    Packet *packet = myTww->makeP2PJoinResponsePacket(userDataList);
    sendAll(serverSocket, packet);
    delete packet;
}

void Server::sendP2PBackupRequest(int serverSocket, struct p2p_user_data userData) {
    Packet *packet = myTww->makeP2PBackupRequest(userData);
    sendAll(serverSocket, packet);
    delete packet;
}

void Server::sendP2PBackupResponse(int serverSocket, bool errorCode) {
    Packet *packet = myTww->makeP2PBackupResponse(errorCode ? 2 : 0);
    sendAll(serverSocket, packet);
    delete packet;
}

void Server::disconnectClient(int clientSocket) {
    map<int, struct client_data>::iterator clientDataIter = myClients.find(clientSocket);
    if (clientDataIter == myClients.end()) {
        on_server_failure();
    }
    debug("YO BITCHES 2 myPredSocket=%d mySuccSocket=%d myClientSocket=%d", myPredecessorSocket, mySuccessorSocket, clientSocket);
    
    Player *disconnectingPlayer = clientDataIter->second.player;
    if (disconnectingPlayer) {
        printf("broadcasting %s's logout notification\n", disconnectingPlayer->myName);
        broadcastLogoutNotify(disconnectingPlayer->myName, disconnectingPlayer->myHp, disconnectingPlayer->myExp,
            disconnectingPlayer->myLocation.x, disconnectingPlayer->myLocation.y);
        myDungeon->removePlayer(disconnectingPlayer);
        myFactory->destroyPlayer(disconnectingPlayer);        
    }
    if (clientSocket == mySuccessorSocket) {
        debug("OMG MY SUCCESSOR DIED!!");
        myP2PState = P2P_FIND_NEW_SUCCESSOR;
    }
    
    delete clientDataIter->second.buffer;
    myClients.erase(clientDataIter);
    
    close(clientSocket);

    printf("* Socket closed. fd=%d \n", clientSocket);
    //printf("killed client at socket %d\n", clientSocket);
}

Player * Server::playerOfSocket(int clientSocket) {
    map<int, struct client_data>::iterator clientDataIter = myClients.find(clientSocket);
    if (clientDataIter == myClients.end()) {
        on_server_failure();
    }
    return clientDataIter->second.player;
}

int Server::maxClientSocket() {
    int socket = 0;
    map<int, struct client_data>::iterator client;
    for (client = myClients.begin(); client != myClients.end(); client++) {
        socket = max(socket, (*client).first);
    }
    return socket;
}

void handleSigTerm(int param) {
    debug("handling SIGTERM");
    exit(1);
}

int main(int argc, char **argv) {
    uint16_t tcpPort = 0;
    uint16_t udpPort = 0;

    /* If no arguments, we assume defaults. */
    if (argc == 1) {
        tcpPort = 1026;
        udpPort = 1027;
    }

    int i = 0;
    while (i < argc) {
        string opt = argv[i];
        if (opt == "-t") {
            tcpPort = atoi(argv[i+1]);
            i += 2;
        } else if (opt == "-u") {
            udpPort = atoi(argv[i+1]);
            i += 2;
        } else {
            i++;
        }
    }

    if (tcpPort == 0 || udpPort == 0) {
        on_server_invalid_port();
        exit(1);
    }
    
    signal(SIGTERM, handleSigTerm);

    try {
        server.startServer(tcpPort, udpPort);
        server.run();
        server.closeServer();
    } catch (int e) {
        server.closeServer();
        exit(1);
    }

    return 0;
}
