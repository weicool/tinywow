#include "tww.h"
#include <algorithm>

using namespace std;

static int timeval_subtract(struct timeval *, struct timeval, struct timeval);

UDPHandler::UDPHandler(int socket, bool resend, bool isTracker) {
    mySocket = socket;
    myResend = resend;
    myIgnoreDups = resend;
    myIsTracker = isTracker;
    myLastSentPacket = NULL;
    myReceiveHistory = new UDPPacketList();
}

void UDPHandler::run(bool (*handlerFunction)(UDPPacket *)) {
    debug("running UDPHandler loop");

    while (true) {
        struct timeval time;
        time.tv_sec = 0;
        time.tv_usec = 100000;   // 100 ms
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(mySocket, &readfds);

        int selectVal;
        if (selectVal = select(mySocket + 1, &readfds, NULL, NULL, &time) < 0) {
            exit(1);
        }
        
        bool done = receive(readfds, handlerFunction);
        if (done) {
            return;
        }
    }
}

bool UDPHandler::receive(fd_set readfds, bool (*handlerFunction)(UDPPacket *)) {
    unsigned char readBytes[4096];
    struct timeval currentTime, timeDiff;
    
    if (FD_ISSET(mySocket, &readfds)) {
        bool dup = false;
        struct sockaddr_in sin;

        socklen_t sin_len = sizeof(struct sockaddr_in);
        int bytesRead = recvfrom(mySocket, readBytes, MAX_PACKET_LENGTH, 0,
                                (struct sockaddr *) &sin, &sin_len);
        if (bytesRead < 0) {
            if (myIsTracker) {
                tracker_on_malformed_udp(3);
            } else {
                on_malformed_udp();
                exit(1);            
            }
        }
        
        UDPPacket *udpPacket = parsePacket(readBytes, bytesRead, &sin);
        if (udpPacket) {
            registerInReceiveHistory(udpPacket);
            dup = isDuplicatePacket(udpPacket);
            if (dup && !myIgnoreDups) {
                if (myIsTracker) {
                    tracker_on_udp_duplicate(udpPacket->ip);
                } else {
                    on_udp_duplicate(udpPacket->ip);                    
                }
            }
        }
        
        if (!(dup && myIgnoreDups)) {
            if (myResend && myLastSentPacket) {
                if (udpPacket->id() != myLastSentPacket->id()) {
                    on_malformed_udp();
                }
                
                if (udpPacket->ip != myLastSentPacket->ip ||
                    udpPacket->port != myLastSentPacket->port) {
                    on_invalid_udp_source();
                }
                
                delete myLastSentPacket;
                myLastSentPacket = NULL;
            }

            bool done;
            try {
                done = handlerFunction(udpPacket);
            } catch (int e) {
                on_malformed_udp();
            }
            if (done) {
                return true;
            }                
        }
    }
    
    /* exponential backoff resending */
    if (myResend && myLastSentPacket) {
        gettimeofday(&currentTime, NULL);
        int result = timeval_subtract(&timeDiff, currentTime, myLastSentPacket->timeSent);
        unsigned int diffMilliseconds = (timeDiff.tv_sec * 1000) + (timeDiff.tv_usec / 1000);
        
        if (diffMilliseconds >= myLastSentPacket->timeToWait()) {
            send(myLastSentPacket);                
        }
    }
    
    return false;
}

UDPPacket * UDPHandler::parsePacket(unsigned char *readBytes, size_t bytesRead,
        struct sockaddr_in *sin) {
    uint32_t ip = ntohl(sin->sin_addr.s_addr);
    uint16_t port = ntohs(sin->sin_port);
    unsigned char messageType = readBytes[0];
    if (bytesRead % 4) {
        on_malformed_udp();
        return NULL;
    }
    if (messageType >= MAX_UDP_MESSAGE) {
        if (myIsTracker) {
            tracker_on_malformed_udp(2);
        } else {
            on_malformed_udp();
        }
        return NULL;
    }
    unsigned char *packet = (unsigned char *) malloc(bytesRead);
    memset(packet, 0, sizeof(packet));
    memcpy(packet, readBytes, bytesRead);
    return new UDPPacket(ip, port, packet, bytesRead);
}

void UDPHandler::registerInReceiveHistory(UDPPacket *packet) {
    debug("register packet #%u", packet->id());
    if (myReceiveHistory->size() == 50) {
        debug("removing old UDP packet in receive history");
        UDPPacket *oldPacket = myReceiveHistory->at(0);
        delete oldPacket;
        myReceiveHistory->erase(myReceiveHistory->begin());
    }
    myReceiveHistory->push_back(packet);
}

bool UDPHandler::isDuplicatePacket(UDPPacket *packet) {
    UDPPacket *oldPacket;
    for (unsigned int i = 0; i < myReceiveHistory->size(); i++) {
        oldPacket = myReceiveHistory->at(i);
        if (oldPacket != packet &&
            oldPacket->ip == packet->ip && oldPacket->id() == packet->id()) {
            return true;
        }
    }
    return false;
}

void UDPHandler::send(UDPPacket *packet) {
    if (myResend) {
        if (packet->numTimesSent == 0) {
            if (myLastSentPacket) {
                delete myLastSentPacket;                
            }
            myLastSentPacket = packet;
        } else if (packet->numTimesSent >= 4) {
            on_udp_fail();
            exit(1);
        }        
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(packet->ip);
    sin.sin_port = htons(packet->port);

    size_t numBytesSent = sendto(mySocket, packet->packet, packet->length, 0,
                            (struct sockaddr *) &sin, sizeof(sin));
    if (numBytesSent != packet->length) {
        on_udp_fail();
        exit(1);
    }
    
    if (myResend) {
        gettimeofday(&packet->timeSent, NULL);
        packet->numTimesSent++;
        on_udp_attempt(packet->numTimesSent);
    }

    if (DEBUG) {
        struct in_addr ipSender = { htonl(packet->ip) };
        printf("sent UDP packet #%u to %s at port %u - ", packet->id(), inet_ntoa(ipSender), packet->port);
        packet->print();
    }
}

/** Methods to make UDP Packets. */

UDPPacket * UDPHandler::makeStorageLocationRequest(uint32_t ip, uint16_t port,
        uint32_t msgID, char *playerName) {
    struct udp_storage_location_request payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makeUDPPacket(ip, port, STORAGE_LOCATION_REQUEST, msgID,
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makeStorageLocationResponse(uint32_t ip, uint16_t port,
        uint32_t msgID, const char *serverIP, uint16_t serverUDPPort) {
    struct udp_storage_location_response payload;
    memset(&payload, 0, sizeof(payload));

    payload.server_ip_address = inet_addr(serverIP);
    payload.server_udp_port = htons(serverUDPPort);

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makeUDPPacket(ip, port, STORAGE_LOCATION_RESPONSE, msgID, 
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makeServerAreaRequest(uint32_t ip, uint16_t port,
        uint32_t msgID, struct location loc) {
    struct udp_server_area_request payload;
    memset(&payload, 0, sizeof(payload));
    payload.x = loc.x;
    payload.y = loc.y;
        
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makeUDPPacket(ip, port, SERVER_AREA_REQUEST, msgID,
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makeServerAreaResponse(uint32_t ip, uint16_t port,
        uint32_t msgID, const char *serverIP, uint16_t serverTCPPort,
        uint8_t minX, uint8_t maxX, uint8_t minY, uint8_t maxY) {
    struct udp_server_area_response payload;
    memset(&payload, 0, sizeof(payload));

    payload.server_ip_address = inet_addr(serverIP);
    payload.server_tcp_port = htons(serverTCPPort);
    payload.min_x = minX;
    payload.max_x = maxX;
    payload.min_y = minY;
    payload.max_y = maxY;
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makeUDPPacket(ip, port, SERVER_AREA_RESPONSE, msgID, 
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makePlayerStateRequest(uint32_t ip, uint16_t port, 
        uint32_t msgID, char *playerName) {
    struct udp_player_state_request payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makeUDPPacket(ip, port, PLAYER_STATE_REQUEST, msgID,
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makePlayerStateResponse(uint32_t ip, uint16_t port, 
        uint32_t msgID, char *playerName, int hp, int exp, uint8_t x, uint8_t y) {
    struct udp_player_state_response payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));
    payload.hp = htonl(hp);
    payload.exp = htonl(exp);
    payload.x = x;
    payload.y = y;

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makeUDPPacket(ip, port, PLAYER_STATE_RESPONSE, msgID,
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makeSaveStateRequest(int32_t ip, uint16_t port,
        uint32_t msgID, char *playerName, int hp, int exp, struct location loc) {
    struct udp_save_state_request payload;
    memset(&payload, 0, sizeof(payload));
    
    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));
    payload.hp = htonl(hp);
    payload.exp = htonl(exp);
    payload.x = loc.x;
    payload.y = loc.y;

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makeUDPPacket(ip, port, SAVE_STATE_REQUEST, msgID,
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makeSaveStateResponse(uint32_t ip, uint16_t port, 
        uint32_t msgID, uint8_t errorCode) {
    struct udp_save_state_response payload;
    payload.error_code = errorCode;

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makeUDPPacket(ip, port, SAVE_STATE_RESPONSE, msgID,
        payloadBytes, sizeof(payloadBytes));
}

UDPPacket * UDPHandler::makeUDPPacket(uint32_t ip, uint16_t port, char messageType,
        uint32_t msgID, unsigned char *payload, size_t payloadLength) {
    size_t headerLength = sizeof(udp_packet_header);
    size_t totalLength = headerLength + payloadLength;

    struct udp_packet_header header;
    header.message_type = messageType;
    header.id = htonl(msgID);

    unsigned char *udppacket = (unsigned char *) malloc(totalLength);
    /* copy header */
    memcpy(udppacket, &header, headerLength);
    /* copy payload */
    if (payloadLength > 0) {
        memcpy(udppacket + headerLength, payload, payloadLength);
    }

    return new UDPPacket(ip, port, udppacket, sizeof(udp_packet_header) + payloadLength);
}

/** Courtesy stackoverflow.com */
static int timeval_subtract(struct timeval *result, struct timeval x, struct timeval y) {    
    /* Perform the carry for the later subtraction by updating y. */
    if (x.tv_usec < y.tv_usec) {
        int nsec = (y.tv_usec - x.tv_usec) / 1000000 + 1;
        y.tv_usec -= 1000000 * nsec;
        y.tv_sec += nsec;
    }
    if (x.tv_usec - y.tv_usec > 1000000) {
        int nsec = (x.tv_usec - y.tv_usec) / 1000000;
        y.tv_usec += 1000000 * nsec;
        y.tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
      tv_usec is certainly positive. */
    result->tv_sec = x.tv_sec - y.tv_sec;
    result->tv_usec = x.tv_usec - y.tv_usec;

    /* Return 1 if result is negative. */
    return x.tv_sec < y.tv_sec;
}
