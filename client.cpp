#include "tww.h"

using namespace std;

static bool processUDPPacketFnc(UDPPacket *packet);
static void deletePackets(PacketList *packets);

Client client;

// NO_STATE -> FINDING_STATE -> GRABBING_STATE
// -> NOT_LOGGED_IN -> LOGGED_IN -> SWITCHING -> NOT_LOGGED_IN -> LOGGED_IN -> ...
// -> LOGGING_OUT -> FINDING_STATE -> SAVING_STATE -> GAME_OVER

Client::Client(uint32_t trackerIP, uint16_t trackerPort) {
    mySocket = 0;
    myTww = new TWW();
    myPlayer = NULL;
    myPlayerName = (char *) malloc(MAX_LOGIN_LENGTH + 1);
    myDungeon = new Dungeon(DUNGEON_SIZE_X, DUNGEON_SIZE_Y);
    myGameState = NO_STATE;
    myLoggingOut = false;
    autoSave = false;
     autoSaveDone = false;
    firstTimeLoggedIn = true;
    
    myBuffer = new std::vector<unsigned char>();

    myTrackerIP = trackerIP;
    myTrackerPort = trackerPort;
    myStorageServerIP = 0;
    myStorageServerPort = 0;
    myLocationServerIP = 0;
    myLocationServerPort = 0;
    myUDPSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (mySocket < 0) {
        on_client_connect_failure();
        exit(1);
    }
    myUDPHandler = new UDPHandler(myUDPSocket, true, false);
    srand(time(NULL));
    myMsgID = rand();
    debug("initial msgID: %d", myMsgID);
}

void Client::runGame() {
    char commandBuffer[MAX_CMD_LENGTH + 1] = {0};

    /* Retrieve login request. */
    while (myGameState != FINDING_STATE) {
        show_prompt();
        if (!fgets(commandBuffer, MAX_CMD_LENGTH + 1, stdin)) {
            exit(1);
        }
        readUserInput(commandBuffer);
    }

    assert(check_player_name(myPlayerName));
    assert(myGameState == FINDING_STATE);
    
    bool done = false;
    while (myGameState != GAME_OVER) {
        done = doUDP();
        if (done) {
            assert(myGameState == GAME_OVER);
            break;
        }
        
        debug("##### END UDP ##### START TCP #####");
        connectToServer(myLocationServerIP, myLocationServerPort);
        sendLoginRequest(myPlayerName);
        doTCP();
        debug("##### END TCP ##### START UDP #####");
    }
    
    on_disconnection_from_server();
}

void Client::doTCP() {
    char commandBuffer[MAX_CMD_LENGTH + 1] = {0};
    unsigned char readBytes[MAX_PACKET_LENGTH];
    size_t bytesRead;
    bool done = false;
    time_t lastTimeSavedState = time(NULL);

    show_prompt();
    while (true) {
        struct timeval timeSelect;
        timeSelect.tv_sec = 1;
        timeSelect.tv_usec = 0;
    
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fileno(stdin), &readfds);
        FD_SET(mySocket, &readfds);

        if (difftime(time(NULL), lastTimeSavedState) >= 60.0) {
          lastTimeSavedState = time(NULL);
          autoSave = true;
          myGameState = FINDING_STATE;
          while (!autoSaveDone) {
               doUDP();
          }
          myGameState = LOGGED_IN;
          autoSave = false;
          autoSaveDone = false;
        }
        
        if (select(max(mySocket, fileno(stdin)) + 1, &readfds, NULL, NULL, &timeSelect) < 0) {
            exit(1);
        }

        if (FD_ISSET(fileno(stdin), &readfds)) {
            /* fgets reads user input up to a maximum length and always null-terminates */
            if (!fgets(commandBuffer, MAX_CMD_LENGTH + 1, stdin)) {
                exit(1);
            }

            readUserInput(commandBuffer);
            show_prompt();
        }

        if (FD_ISSET(mySocket, &readfds)) {
            bytesRead = recv(mySocket, readBytes, MAX_PACKET_LENGTH, 0);
            if (bytesRead <= 0) {
                myGameState = GAME_OVER;
                return;
            }
            debug("recved %u bytes", bytesRead);
            done = updateGame(readBytes, bytesRead);
            if (done) {
                return;
            }
        }
    }
}

bool Client::doUDP() {
    switch (myGameState) {
        case FINDING_STATE:
            debug("state FINDING_STATE");
            sendStorageLocationRequest();
            break;
        case NOT_LOGGED_IN:
            debug("state NOT_LOGGED_IN");
            sendServerAreaRequest(myPlayer->myLocation);
            break;
        default:
            debug("myGameState is whack");
            assert(false);
    }
    myUDPHandler->run(processUDPPacketFnc);
    if (myGameState == GAME_OVER || autoSaveDone) {
        return true;
    }
    return false;
}

bool Client::processUDPPacket(UDPPacket *packet) {
    return this->updateGameUDP(packet);
}

static bool processUDPPacketFnc(UDPPacket *packet) {
    return client.processUDPPacket(packet);
}

bool Client::updateGameUDP(UDPPacket *packet) {
    try {
        if (DEBUG) {
            struct in_addr ipSender = { htonl(packet->ip) };
            printf("received UDP packet #%u from %s at port %u - ", packet->id(), inet_ntoa(ipSender), packet->port);
            packet->print();
        }
        switch (packet->msgType()) {
        case STORAGE_LOCATION_RESPONSE:
            debug("STORAGE_LOCATION_RESPONSE");
            if (myGameState != FINDING_STATE) {
                debug("dropping dup");
                break;
            }
            processStorageLocationResponse(packet);
            assert(myGameState == GRABBING_STATE || myGameState == SAVING_STATE);
            break;
        case PLAYER_STATE_RESPONSE:
            debug("PLAYER_STATE_RESPONSE");
            if (myGameState != GRABBING_STATE) {
                debug("dropping dup");
                break;
            }
            processPlayerStateResponse(packet);
            assert(myGameState == NOT_LOGGED_IN);
            break;
        case SERVER_AREA_RESPONSE:
            debug("SERVER_AREA_RESPONSE");
            if (myGameState != NOT_LOGGED_IN) {
                debug("dropping dup");
                break;
            }
            processServerAreaResponse(packet);
            return true;
        case SAVE_STATE_RESPONSE:
            debug("SAVE_STATE_RESPONSE");
            if (myGameState != SAVING_STATE) {
                debug("dropping dup");
                break;
            }
            processSaveStateResponse(packet);
            assert(myGameState == SAVING_STATE);
            if (autoSave) {
                autoSaveDone = true;
            } else {
                myGameState = GAME_OVER;
            }
            return true;
        default:
            throw -1;
        }
    } catch (int e) {
        on_malformed_message_from_server();
    }

    return false;
}

void Client::connectToServer(uint32_t ip, uint16_t port) {
    assert(myGameState == NOT_LOGGED_IN);
    
    debug("Connecting...");
    mySocket = socket(AF_INET, SOCK_STREAM, 0);
    if (mySocket < 0) {
        on_client_connect_failure();
        exit(1);
    }
    debug("Communicating through socket %d", mySocket);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(ip);
    sin.sin_port = htons(port);
    if (connect(mySocket, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        on_client_connect_failure();
        exit(1);
    }
}

void Client::readUserInput(char *commandBuffer) {
    char commandName[MAX_CMD_LENGTH + 1] = {0};
    char commandArgument[MAX_CMD_LENGTH + 1] = {0};

    if (sscanf(commandBuffer, "%s %s\n", commandName, commandArgument) <= 0) {
        on_invalid_command(commandBuffer);
        return;
    }

    if (check_command(commandName)) {
        if (myGameState == NO_STATE) {
            if (!strcmp(commandName, "login")) {
                sendLoginRequest(commandArgument);
            } else {
                on_before_login();
            }
        } else {
            if (!strcmp(commandName, "logout")) {
                sendLogout();
                myGameState = LOGGING_OUT;
                myLoggingOut = true;
            } else if (!strcmp(commandName, "speak")) {
                sendSpeak(commandBuffer + strlen(commandName) + 1);
            } else if (!strcmp(commandName, "map")) {
                /* Easter egg command that prints out a map of the dungeon. */
                myDungeon->print();
            } else if (strlen(commandArgument) == 0) {
                on_invalid_syntax();
            } else if (!strcmp(commandName, "login")) {
                sendLoginRequest(commandArgument);
            } else if (!strcmp(commandName, "move")) {
                sendMove(commandArgument);
            } else if (!strcmp(commandName, "attack")) {
                sendAttack(commandArgument);
            }
        }
    } else {
        on_invalid_command(commandName);
    }
}

bool Client::updateGame(unsigned char *bufferClient, size_t bytesRead) {
    PacketList *packetsParsed = NULL;
    Packet *packet;

    try {
        packetsParsed = myTww->parsePackets(bufferClient, bytesRead, myBuffer);
        debug("number of packets: %u", packetsParsed->size());

        for (unsigned int i = 0; i < packetsParsed->size(); i++) {
            packet = packetsParsed->at(i);
            if (DEBUG) {
                packet->print();
            }
            debug("CHECKPOINT!");
            switch (packet->msgType()) {
            case LOGIN_REPLY:
                debug("LOGIN_REPLY");
                processLoginReply(packet);
                break;
            case MOVE_NOTIFY:
                debug("MOVE_NOTIFY");
                processMoveNotify(packet);
                break;
            case ATTACK_NOTIFY:
                debug("ATTACK_NOTIFY");
                processAttackNotify(packet);
                break;
            case SPEAK_NOTIFY:
                debug("SPEAK_NOTIFY");
                processSpeakNotify(packet);
                break;
            case LOGOUT_NOTIFY:
                debug("LOGOUT_NOTIFY");
                processLogoutNotify(packet);
                if (myGameState == NOT_LOGGED_IN || (myLoggingOut && myGameState == FINDING_STATE)) {
                    deletePackets(packetsParsed);
                    return true;
                }
                break;
            case INVALID_STATE:
                debug("INVALID_STATE");
                processInvalidState(packet);
                break;
            default:
                throw -1;
            }
        }
    } catch (int e) {
        on_malformed_message_from_server();
    }

    deletePackets(packetsParsed);
    
    return false;
}

void Client::sendLoginRequest(char *playerName) {
    if (!check_player_name(playerName)) {
        on_invalid_name(playerName);
        return;
    }

    if (myGameState == NO_STATE) {
        strncpy(myPlayerName, playerName, strlen(playerName) + 1);
        null_terminate(myPlayerName, MAX_LOGIN_LENGTH);
        myGameState = FINDING_STATE;
        return;
    }
    
    Packet *packet = myTww->makeLoginPacket(myPlayer->myName, myPlayer->myHp, myPlayer->myExp,
                        myPlayer->myLocation.x, myPlayer->myLocation.y);
    sendAll(packet->packet, packet->length);
    delete packet;
}

void Client::sendMove(char *direction) {
    enum direction dir;
    if (!strcmp(direction, "north")) {
        dir = NORTH;
    } else if (!strcmp(direction, "south")) {
        dir = SOUTH;
    } else if (!strcmp(direction, "east")) {
        dir = EAST;
    } else if (!strcmp(direction, "west")) {
        dir = WEST;
    } else {
        on_invalid_direction(direction);
        return;
    }

    struct location possibleLocation;
    myDungeon->computeMovePlayer(myPlayer->myLocation, dir, &possibleLocation);
    if (!myDungeon->withinBoundary(possibleLocation)) {
        myDungeon->movePlayer(myPlayer, dir);
        debug("LOGGING OUT TO NEW SERVER");
        sendLogout();
        myGameState = SWITCHING;
        return;
    }
    Packet *packet = myTww->makeMovePacket((uint8_t) dir);
    sendAll(packet->packet, packet->length);
    delete packet;
}

void Client::sendAttack(char *playerName) {
    if (!check_player_name(playerName)) {
        on_invalid_name(playerName);
        return;
    }

    Player *victim = myDungeon->findPlayer(playerName);

    /* can't attack self */
    if (victim == myPlayer) {
        return;
    }
    if (!victim || !myDungeon->inVision(myPlayer, victim)) {
        on_not_visible();
        return;
    }

    Packet *packet = myTww->makeAttackPacket(playerName);

    sendAll(packet->packet, packet->length);
    debug("madeAttackPacket! packet length: %d", packet->length);
    delete packet;
}

void Client::sendSpeak(char *msg) {
    /* Chop off last garbage character. */
    int msgLength = strlen(msg);
    if (msgLength > 0) {
        msg[MAX_MSG_LENGTH] = '\0';
        msg[msgLength - 1] = '\0';
    }

    if (msgLength > MAX_MSG_LENGTH) {
        on_invalid_message();
        return;
    }

    if (!check_player_message(msg)) {
        on_invalid_message();
        return;
    }

    Packet *packet = myTww->makeSpeakPacket(msg);
    sendAll(packet->packet, packet->length);
    debug("madeSpeakPacket! packet length: %d", packet->length);
    delete packet;
}

void Client::sendLogout() {
    Packet *packet = myTww->makeLogoutPacket();
    sendAll(packet->packet, packet->length);
    delete packet;
}

void Client::sendAll(unsigned char *buffer, size_t length) {
    /* Courtesy Beej's Guide. */
    size_t totalSent = 0;
    size_t bytesleft = length;
    size_t n;

    while (totalSent < length) {
        debug("SENDING!");
        n = send(mySocket, buffer + totalSent, bytesleft, 0);
        if (n == -1) {
            on_disconnection_from_server();
            exit(1);
        }
        totalSent += n;
        bytesleft -= n;
    }

    if (totalSent != length) {
        on_disconnection_from_server();
        debug("Houston, we have a problem; sendAll() is being whack");
        exit(1);
    }
}

void Client::sendStorageLocationRequest() {
    debug("sendStorageLocationRequest!");
    myMsgID++;
    UDPPacket *packet = myUDPHandler->makeStorageLocationRequest(myTrackerIP, myTrackerPort,
        myMsgID, myPlayerName);
    myUDPHandler->send(packet);
}

void Client::sendPlayerStateRequest() {
    debug("sendPlayerStateRequest!");
    myMsgID++;
    UDPPacket *packet = myUDPHandler->makePlayerStateRequest(myStorageServerIP, myStorageServerPort,
        myMsgID, myPlayerName);
    myUDPHandler->send(packet);
}

void Client::sendServerAreaRequest(struct location loc) {
    debug("sendServerAreaRequest!");
    myMsgID++;
    UDPPacket *packet = myUDPHandler->makeServerAreaRequest(myTrackerIP, myTrackerPort,
        myMsgID, loc);
    myUDPHandler->send(packet);
}

void Client::sendSaveStateRequest() {
    debug("sendSaveStateRequest!");
    myMsgID++;
    UDPPacket *packet = myUDPHandler->makeSaveStateRequest(myStorageServerIP, myStorageServerPort,
        myMsgID, myPlayerName, myPlayer->myHp, myPlayer->myExp, myPlayer->myLocation);
    myUDPHandler->send(packet);
}

void Client::processLoginReply(Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_login_reply)) {
        debug("processLoginReply: packet length not equal");
        throw -1;
    }

    if (myGameState == LOGGED_IN) {
        debug("YO WHY AM I LOGGED IN!");
        throw -1;
    }

    uint8_t errorCode = packet->packet[sizeof(tww_packet_header)];
    if (firstTimeLoggedIn) {
        on_login_reply(errorCode);
        firstTimeLoggedIn = false;
    }
    
    struct in_addr ipSender = { htonl(myLocationServerIP) };
    debug("Connected to server ip:%s port:%d", inet_ntoa(ipSender), myLocationServerPort);
    if (errorCode == 0) {
        struct tww_login_reply *loginReply;
        loginReply = (struct tww_login_reply *) (packet->packet + sizeof(tww_packet_header));
        struct location loc;
        loc.x = loginReply->x;
        loc.y = loginReply->y;
        if (loc.x > DUNGEON_SIZE_X || loc.y > DUNGEON_SIZE_Y) {
            debug("Location size out of bounds");
            throw -1;
        }
        assert(myPlayer != NULL);
        if (myPlayer->myHp != ntohl(loginReply->hp) || 
            myPlayer->myExp != ntohl(loginReply->exp) ||
            myPlayer->myLocation.x != loc.x || myPlayer->myLocation.y != loc.y) {
            throw -1;
        }
        
        myDungeon->addPlayer(myPlayer);
        myGameState = LOGGED_IN;
    } else if (errorCode != 1) {
        throw -1;
    }
}

void Client::processMoveNotify(Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_move_notify)) {
        throw -1;
    }

    struct tww_move_notify *moveReply;
    moveReply = (struct tww_move_notify *) (packet->packet + sizeof(tww_packet_header));
    moveReply->hp = ntohl(moveReply->hp);
    moveReply->exp = ntohl(moveReply->exp);

    debug("RECEIVED x:%d Y:%d", moveReply->x, moveReply->y);
    if (!check_player_name(moveReply->name) ||
            moveReply->x > DUNGEON_SIZE_X || moveReply->y > DUNGEON_SIZE_Y ||
            moveReply->hp < 0 || moveReply->exp < 0) {
        printf("processMoveReply error Name:%s X:%d Y:%d HP:%d EXP:%d",
                moveReply->name, moveReply->x, moveReply->y, moveReply->hp, moveReply->exp);
        throw -1;
    }

    Player *movedPlayer = myDungeon->findPlayer(moveReply->name);

    PlayerList playersInRangeBefore;
    PlayerList playersInRangeAfter;

    if (movedPlayer) {
        if (myPlayer == movedPlayer) {
            myDungeon->playersInRangeOf(myPlayer, playersInRangeBefore);
        }
        movedPlayer->myLocation.x = moveReply->x;
        movedPlayer->myLocation.y = moveReply->y;
        movedPlayer->myHp = moveReply->hp;
        movedPlayer->myExp = moveReply->exp;
        
    } else {
        struct location loc;
        loc.x = moveReply->x;
        loc.y = moveReply->y;
        movedPlayer = new Player(moveReply->name, moveReply->hp, moveReply->exp, loc);
        myDungeon->addPlayer(movedPlayer);
    
    }

    /* Print locations of newly seen players. */
    if (myPlayer == movedPlayer) {
        myDungeon->playersInRangeOf(myPlayer, playersInRangeAfter);

        Player *newlySeenPlayer;
        unsigned int i, j;
        bool newlySeen;
        
        for (i = 0; i < playersInRangeAfter.size(); i++) {
            newlySeenPlayer = playersInRangeAfter[i];

            /* Assume newly seen until proven not. */
            newlySeen = true;
            for (j = 0; j < playersInRangeBefore.size(); j++) {
                if (playersInRangeBefore[j] == newlySeenPlayer) {
                    newlySeen = false;
                    break;
                }
            }

            if (newlySeen) {
                on_move_notify(newlySeenPlayer->myName,
                        newlySeenPlayer->myLocation.x, newlySeenPlayer->myLocation.y,
                        newlySeenPlayer->myHp, newlySeenPlayer->myExp);
            }
        }
    }

    if (myPlayer == movedPlayer || myDungeon->inVision(myPlayer, movedPlayer)) {
        on_move_notify(movedPlayer->myName, 
                movedPlayer->myLocation.x, movedPlayer->myLocation.y,
                movedPlayer->myHp, movedPlayer->myExp);
    }

    if (myPlayer == movedPlayer) {
        myDungeon->printBoundary(myPlayer->myLocation);
    }
}

void Client::processAttackNotify(Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_attack_notify)) {
        debug("processAttackNotify: packet length not equal");
        throw -1;
    }
    struct tww_attack_notify *attackReply;
    attackReply = (struct tww_attack_notify *) (packet->packet + sizeof(tww_packet_header));
    attackReply->hp = ntohl(attackReply->hp);

    if (!check_player_name(attackReply->attacker) ||
            !check_player_name(attackReply->victim) ||
            attackReply->hp < 0) {
        debug("procesAttackNotify error Attacker:%s Victim:%s Damage:%d Victim's HP:%s",
                attackReply->attacker,attackReply->victim, attackReply->damage, attackReply->hp);
        throw -1;
    }
    Player *attacker = myDungeon->findPlayer(attackReply->attacker);
    Player *victim = myDungeon->findPlayer(attackReply->victim);
    if (attacker == NULL || victim == NULL) {
        debug("processAttackNotify: either for the players are not found in myPlayers");
        throw -1;
    }
    attacker->myExp += attackReply->damage;
    if ((victim->myHp - attackReply->damage) > attackReply->hp) {
        debug("processAttacNotify: attackReply's Victim HP <  Victim's HP - attackReply's damage.");
        throw -1;
    }
    victim->myHp = attackReply->hp;

    if (myDungeon->inVision(myPlayer, attacker) && myDungeon->inVision(myPlayer, victim)) {
        on_attack_notify(attacker->myName, victim->myName, attackReply->damage, victim->myHp);
    }
}

void Client::processSpeakNotify(Packet *packet) {
    if (packet->length > MAX_PACKET_LENGTH) {
        debug("processSpeakNotify: packet length greater than 300 bytes");
        throw -1;
    }

    struct tww_speak_notify *speakReply;
    speakReply = (struct tww_speak_notify *) (packet->packet + sizeof(tww_packet_header));

    Player *player = myDungeon->findPlayer(speakReply->name);
    if (player == NULL) {
        debug("Player who wants to speak does not exist.\n");
        throw -1;
    }
    const char *speakMsg = (const char *) packet->packet + sizeof(tww_packet_header) + MAX_LOGIN_LENGTH + 1;
    if (strlen(speakMsg) > MAX_MSG_LENGTH || strlen(speakMsg) == 0) {
        throw -1;
    }
    if (!check_player_message(speakMsg)) {
        debug("Message is malformed.\n");
        throw -1;
    }

    on_speak_notify(player->myName, speakMsg);
}

void Client::processLogoutNotify(Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_logout_notify)) {
        debug("processLogoutNotify: packet length not equal to 16.\n");
        throw -1;
    }
    struct tww_logout_notify *logoutReply;
    logoutReply = (struct tww_logout_notify *) (packet->packet + sizeof(tww_packet_header));
    Player *player = myDungeon->findPlayer(logoutReply->name);
    if (player == NULL) {
        debug("Player who wants to leave does not even exist!\n");
        throw -1;
    }
    
    if (player == myPlayer) {
        if (myGameState == SWITCHING) {
            myDungeon->clear(myPlayer);
            myGameState = NOT_LOGGED_IN;
        } else if (myGameState == LOGGING_OUT) {
            myGameState = FINDING_STATE;
        } else {
            assert(false);
        }
        
        myPlayer->myHp = ntohl(logoutReply->hp);
        myPlayer->myExp = ntohl(logoutReply->exp);
        closeClient();
        debug("logged out of old server");
    } else {
        on_exit_notify(player->myName);
        myDungeon->removePlayer(player);        
    }
}

void Client::processInvalidState(Packet *packet) {
    if (packet->length != sizeof(tww_packet_header) + sizeof(tww_invalid_state)) {
        debug("processInvalidState: packet length not equal to 8");
        throw -1;
    }
    struct tww_invalid_state *invalidState;
    invalidState = (struct tww_invalid_state *) (packet->packet + sizeof(tww_packet_header));
    on_invalid_state(invalidState->errorCode);
    if (invalidState->errorCode == 1) {
        assert(!strcmp(myPlayer->myName, myPlayerName));
    }
}

void Client::processStorageLocationResponse(UDPPacket *packet) {
    debug("processStorageLocationResponse!");
    assert(myGameState == FINDING_STATE);
    if (packet->length != (sizeof(udp_storage_location_response) + sizeof(udp_packet_header))) {
        debug("processStorageLocationResponse: incorrect packet length");
        throw -1;
    }
    struct udp_storage_location_response *storageLocReply;
    storageLocReply = (struct udp_storage_location_response *)
                        (packet->packet + sizeof(udp_packet_header));
    myStorageServerIP = ntohl(storageLocReply->server_ip_address);
    myStorageServerPort = ntohs(storageLocReply->server_udp_port);

    on_loc_resp((uint32_t) packet->msgType(), myStorageServerIP, myStorageServerPort);
    if (myLoggingOut || autoSave) {
        myGameState = SAVING_STATE;
        sendSaveStateRequest();
    } else {
        myGameState = GRABBING_STATE;
        sendPlayerStateRequest();        
    }
}

void Client::processPlayerStateResponse(UDPPacket *packet) {
    debug("processPlayerStateResponse!");
    assert(myGameState == GRABBING_STATE);
    if (packet->length != (sizeof(udp_player_state_response) + sizeof(udp_packet_header))) {
        debug("processPlayerStateResponse: incorrect packet length");
        throw -1;
    }

    struct udp_player_state_response *playerStateReply;
    playerStateReply = (struct udp_player_state_response *)
                        (packet->packet + sizeof(udp_packet_header));

    int hp = ntohl(playerStateReply->hp);
    int exp = ntohl(playerStateReply->exp);

    struct location loc = {playerStateReply->x, playerStateReply->y};
    if (strcmp(myPlayerName, playerStateReply->name) || hp < 0 || exp < 0 ||
        loc.x >= DUNGEON_SIZE_X || loc.y >= DUNGEON_SIZE_Y) {
        throw -1;
    }
    if (myPlayer) {
        delete myPlayer;
    }
    myPlayer = new Player(myPlayerName, hp, exp, loc);
    myGameState = NOT_LOGGED_IN;
    on_state_resp((uint32_t)packet->msgType(), myPlayerName, hp, exp, loc.x, loc.y);
    sendServerAreaRequest(loc);
}

void Client::processServerAreaResponse(UDPPacket *packet) {
    debug("processServerAreaResponse!");
    assert(myGameState == NOT_LOGGED_IN);
    if (packet->length != (sizeof(udp_server_area_response) + sizeof(udp_packet_header))) {
        debug("processServerAreaResponse: incorrect packet length");
        throw -1;
    }
    struct udp_server_area_response *serverAreaReply;

    serverAreaReply = (struct udp_server_area_response *)
                        (packet->packet + sizeof(udp_packet_header));
    myLocationServerIP = ntohl(serverAreaReply->server_ip_address);
    myLocationServerPort = ntohs(serverAreaReply->server_tcp_port);
    
    if (serverAreaReply->max_x > DUNGEON_SIZE_X || serverAreaReply->max_y > DUNGEON_SIZE_Y) {
        throw -1;
    }

    myDungeon->setBoundary(serverAreaReply->min_x, serverAreaReply->min_y,
        serverAreaReply->max_x, serverAreaReply->max_y);

    on_area_resp((uint32_t)packet->msgType(), myLocationServerIP, myLocationServerPort, serverAreaReply->min_x,
        serverAreaReply->max_x, serverAreaReply->min_y, serverAreaReply->max_y);
}

void Client::processSaveStateResponse(UDPPacket *packet) {
    debug("processSaveStateResponse!");
    assert(myGameState == SAVING_STATE);
    if (packet->length != (sizeof(udp_save_state_response) + sizeof(udp_packet_header))) {
        debug("processSaveStateResponse: incorrect packet length");
        throw -1;
    }
    
    
    struct udp_save_state_response *saveStateReply;
    saveStateReply = (struct udp_save_state_response *)
                        (packet->packet + sizeof(udp_packet_header));
    
    on_save_resp((uint32_t) packet->msgType(), saveStateReply->error_code);
}

void Client::closeClient() {
    close(mySocket);
}

static void deletePackets(PacketList *packets) {
    for (int i = 0; i < packets->size(); i++) {
        delete packets->at(i);
    }
    delete packets;
}

int main(int argc, char **argv) {
    uint32_t server = 0;
    uint16_t port = 0;

    /* If no arguments, we assume defaults. */
    if (argc == 1) {
        server = (uint32_t) ntohl(inet_addr("127.0.0.1"));
        port = 1026;
    }

    int i = 0;
    while (i < argc) {
        string opt = argv[i];
        if (opt == "-s") {
            server = ntohl(inet_addr((argv[i+1])));
            i += 2;
        } else if (opt == "-p") {
            port = atoi(argv[i+1]);
            i += 2;
        } else {
            i++;
        }
    }

    if (server == 0) {
        fprintf(stdout, "! The server ip address is not defined.\n");
        client_usage();
        exit(1);
    }
    if (port == 0) {
        fprintf(stdout, "! The server port number is not defined.\n");
        client_usage();
        exit(1);
    }

    client = Client(server, port);
    try {
        client.runGame();
        client.closeClient();
    } catch (int e) {
        client.closeClient();
    }

    return 0;
}
