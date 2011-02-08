#include "tww.h"

using namespace std;

Packet * TWW::makeLoginPacket(char *playerName, int hp, int exp, uint8_t x, uint8_t y) {
    struct tww_login_request payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));
    payload.hp = htonl(hp);
    payload.exp = htonl(exp);
    payload.x = x;
    payload.y = y;
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(LOGIN_REQUEST, payloadBytes, sizeof(payload));
}

Packet * TWW::makeLogoutPacket() {
    return makePacket(LOGOUT, NULL, 0);
}

Packet * TWW::makeMovePacket(uint8_t dir) {
    struct tww_move payload;
    memset(&payload, 0, sizeof(payload));
    payload.direction = dir;

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makePacket(MOVE, payloadBytes, sizeof(payload));
}

Packet * TWW::makeAttackPacket(char *victim) {
    debug("I'm in ur Tww, makin' ur attackPacket!");
    struct tww_attack payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.name, victim, strlen(victim) + 1);
    null_terminate(payload.name, strlen(victim));

    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));

    return makePacket(ATTACK, payloadBytes, sizeof(payload));
}

Packet * TWW::makeSpeakPacket(char *msg) {
    debug("I'm in ur Tww, makin' ur speakPacket!");
    size_t paddingSize = calculatePaddingSize(strlen(msg) + 1);
    struct tww_speak payload;
    payload.msg = (char *) malloc(strlen(msg) + 1);
    strncpy(payload.msg, msg, strlen(msg) + 1);
    int payloadLength = strlen(payload.msg) + paddingSize + 1;
    null_terminate(payload.msg, strlen(msg));

    unsigned char payloadBytes[payloadLength];
    memcpy(payloadBytes, payload.msg, strlen(payload.msg) + 1);

    if (paddingSize != 0) {
        memset(payloadBytes + strlen(payload.msg) + 1, 0, paddingSize);
    }
    free(payload.msg);

    return makePacket(SPEAK, payloadBytes, payloadLength);
}

Packet * TWW::makeLoginReplyPacket(int errorCode, int hp, int exp, uint8_t x, uint8_t y) {
    struct tww_login_reply payload;
    memset(&payload, 0, sizeof(payload));
    payload.errorCode = errorCode;
    payload.hp = htonl(hp);
    payload.exp = htonl(exp);
    payload.x = x;
    payload.y = y;
    payload.padding = 0;
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(LOGIN_REPLY, payloadBytes, sizeof(payload));
}

Packet * TWW::makeMoveNotifyPacket(char *playerName, int hp, int exp, uint8_t x, uint8_t y) {
    struct tww_move_notify payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));
    payload.x = x;
    payload.y = y;
    payload.hp = htonl(hp);
    payload.exp = htonl(exp);
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(MOVE_NOTIFY, payloadBytes, sizeof(payload));
}

Packet * TWW::makeAttackNotifyPacket(char *attackerName, char *victimName, int damage, int hp) {
    struct tww_attack_notify payload;
    memset(&payload, 0, sizeof(payload));
    
    strncpy(payload.attacker, attackerName, strlen(attackerName) + 1);
    null_terminate(payload.attacker, strlen(attackerName));
    strncpy(payload.victim, victimName, strlen(victimName) + 1);
    null_terminate(payload.victim, strlen(victimName));
    payload.damage = damage;
    payload.hp = htonl(hp);
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(ATTACK_NOTIFY, payloadBytes, sizeof(payload));
}

Packet * TWW::makeSpeakNotifyPacket(char *playerName, char *msg) {
    debug("I'm in ur Tww, makin' ur speakNotifyPacket! with playerName:%s and msg:%s", playerName, msg);

    if (!check_player_name(playerName)) {
        on_server_failure();
    }

    size_t msgLength  = strlen(msg);
    struct tww_speak_notify payload;
    memset(&payload, 0, sizeof(payload));
    payload.msg = (char *) malloc(msgLength + 1);
    strncpy(payload.msg, msg, msgLength + 1);
    null_terminate(payload.msg, msgLength);

    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));

    size_t payloadLength = sizeof(payload) + msgLength + 1;

    size_t paddingSize = calculatePaddingSize(payloadLength);
    payloadLength += paddingSize;

    unsigned char payloadBytes[payloadLength];
    memcpy(payloadBytes, payload.name, sizeof(payload.name));
    memcpy(payloadBytes+sizeof(payload.name), payload.msg, msgLength+1);

    free(payload.msg);
    
    return makePacket(SPEAK_NOTIFY, payloadBytes, payloadLength);
}

Packet * TWW::makeLogoutNotifyPacket(char *playerName, int hp, int exp, uint8_t x, uint8_t y) {
    struct tww_logout_notify payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.name, playerName, strlen(playerName) + 1);
    null_terminate(payload.name, strlen(playerName));
    payload.hp = htonl(hp);
    payload.exp = htonl(exp);
    payload.x = x;
    payload.y = y;
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(LOGOUT_NOTIFY, payloadBytes, sizeof(payload));
}

Packet * TWW::makeInvalidStatePacket(int errorCode) {
    struct tww_invalid_state payload;
    memset(&payload, 0, sizeof(payload));
    payload.errorCode = errorCode;
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(INVALID_STATE, payloadBytes, sizeof(payload));
}

Packet * TWW::makeP2PBackupResponse(int errorCode) {
    struct p2p_bkup_response payload;
    memset(&payload, 0, sizeof(payload));
    payload.error_code = errorCode;
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(BKUP_RESPONSE, payloadBytes, sizeof(payload));
}

Packet * TWW::makeP2PBackupRequest(struct p2p_user_data userData) {
    unsigned char payloadBytes[sizeof(p2p_user_data)];
    userData.hp = htonl(userData.hp);
    userData.exp = htonl(userData.exp);
    memcpy(payloadBytes, &userData, sizeof(p2p_user_data));
    
    return makePacket(BKUP_REQUEST, payloadBytes, sizeof(p2p_user_data));
}

Packet * TWW::makeP2PJoinResponsePacket(UserDataList *userDataList) {
    struct p2p_join_response payload;
    memset(&payload, 0, sizeof(payload));
    
    payload.user_number = userDataList->size();
    
    int userDataList_size = sizeof(p2p_user_data) * payload.user_number;
    payload.user_data_list = (struct p2p_user_data *)malloc(userDataList_size);
    
    unsigned char payloadBytes[sizeof(payload) + userDataList_size];
    payload.user_number = htonl(payload.user_number);
    
    memcpy(payloadBytes, &payload, sizeof(payload));
    int offset = sizeof(payload);
    
    struct p2p_user_data data;
    for (int i = 0; i < userDataList->size(); i++) {
        data = userDataList->at(i);
        data.hp = htonl(data.hp);
        data.exp = htonl(data.exp);
        memcpy(payloadBytes + offset, &data, sizeof(p2p_user_data));
        offset += sizeof(p2p_user_data);
    }
    
    return makePacket(JOIN_RESPONSE, payloadBytes, sizeof(payload) + userDataList_size);
}

Packet * TWW::makeP2PJoinRequestPacket(int p2p_id) {
    struct p2p_join_request payload;
    memset(&payload, 0, sizeof(payload));
    
    payload.server_p2p_id = htonl(p2p_id);
    
    unsigned char payloadBytes[sizeof(payload)];
    memcpy(payloadBytes, &payload, sizeof(payload));
    
    return makePacket(JOIN_REQUEST, payloadBytes, sizeof(payload));
}

Packet * TWW::makePacket(char messageType, unsigned char *payload, size_t payloadLength) {
    size_t headerLength = sizeof(tww_packet_header);
    size_t totalLength = headerLength + payloadLength;
    
    struct tww_packet_header header;
    header.version = VALID_VERSION;
    header.total_length = htons(totalLength);
    header.msg_type = messageType;
    
    unsigned char *packet = (unsigned char *) malloc(totalLength);
    /* copy header */
    memcpy(packet, &header, headerLength);
    /* copy payload */
    if (payloadLength > 0) {
        memcpy(packet + headerLength, payload, payloadLength);
    }
    
    return new Packet(packet, sizeof(tww_packet_header) + payloadLength);
}

PacketList * TWW::parsePackets(unsigned char *buffer, size_t bytesReceived, 
                                    vector<unsigned char> *clientBuffer) {
    PacketList *parsedPackets = new PacketList();

    for (unsigned int i = 0; i < bytesReceived; i++) {
        clientBuffer->push_back(buffer[i]);
    }
    unsigned int bufferOffset = 0;

    while (bufferOffset + sizeof(tww_packet_header) <= clientBuffer->size()) {
        /* Invariant: The first 4 bytes in the buffer should always be a header. */
        uint8_t version = clientBuffer->at(bufferOffset);
        uint16_t totalLength = (clientBuffer->at(bufferOffset+1) << 8) + clientBuffer->at(bufferOffset+2);
        uint8_t msgType = clientBuffer->at(bufferOffset + 3);
        debug("received packet of size: %u", totalLength);
        if (version != 4 ||
            totalLength % 4 != 0 ||
            msgType < 1 || msgType >= MAX_MESSAGE) {
            debug("fail: bad packet");
            throw -1;
        }

        if (bufferOffset + totalLength <= clientBuffer->size()) {
            unsigned char *packet = (unsigned char *) malloc(totalLength);
            memset(packet, 0, sizeof(packet));
            for (unsigned int i = 0; i < totalLength; i++) {
                packet[i] = clientBuffer->at(bufferOffset + i);
            }
            parsedPackets->push_back(new Packet(packet, totalLength));
            bufferOffset += totalLength;
        } else {
            /* We cannot make a packet of the remaining bytes 
               we haven't processed in the buffer. */
            break;
        }
    }

    clientBuffer->erase(clientBuffer->begin(), clientBuffer->begin() + bufferOffset);
    return parsedPackets;
}
