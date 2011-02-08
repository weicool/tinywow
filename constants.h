/**************************************************************************
 * The reference implementation of the EE122 Project #2
 * Modified by Wei Yeh and Evelyn Yung to include more constants
 *
 * Author: Junda Liu (liujd@cs)
 * Author: DK Moon (dkmoon@cs)
 * Author: David Zats (dzats@cs)
**************************************************************************/

#ifndef _constants_h_
#define _constants_h_

#define VALID_VERSION 4
#define MAX_LOGIN_LENGTH 9
#define DUNGEON_SIZE_X 100
#define DUNGEON_SIZE_Y 100
#define VISION_RANGE 5
#define MAX_MSG_LENGTH 255
#define MAX_CMD_NAME_LENGTH 7
#define MAX_CMD_LENGTH MAX_MSG_LENGTH + MAX_CMD_NAME_LENGTH
#define MAX_PACKET_LENGTH 300
#define MAX_NUM_CLIENTS 20
#define MAX_NUM_SENT_HISTORY 50
#define NO_PLAYER NULL

enum messages {
  LOGIN_REQUEST = 1,
  LOGIN_REPLY,
  MOVE,
  MOVE_NOTIFY,
  ATTACK,
  ATTACK_NOTIFY,
  SPEAK,
  SPEAK_NOTIFY,
  LOGOUT,
  LOGOUT_NOTIFY,
  INVALID_STATE,
  
  BLANK1,
  BLANK2,
  BLANK3,
  BLANK4,
  
  JOIN_REQUEST,
  JOIN_RESPONSE,
  BKUP_REQUEST,
  BKUP_RESPONSE,

  MAX_MESSAGE,
};

enum udpmessages {
    STORAGE_LOCATION_REQUEST = 0,
    STORAGE_LOCATION_RESPONSE,
    SERVER_AREA_REQUEST,
    SERVER_AREA_RESPONSE,
    PLAYER_STATE_REQUEST,
    PLAYER_STATE_RESPONSE,
    SAVE_STATE_REQUEST,
    SAVE_STATE_RESPONSE,

    MAX_UDP_MESSAGE,
};
enum direction {NORTH = 0, SOUTH, EAST, WEST};

enum states {
    NO_STATE = 0,
    FINDING_STATE,
    GRABBING_STATE,
    NOT_LOGGED_IN,
    LOGGED_IN,
    LOGGING_OUT,
    SWITCHING,
    SAVING_STATE,
    GAME_OVER
};

enum p2p_states {
    P2P_INACTIVE = 0,
    P2P_SEND_JOIN,
    P2P_RECEIVE_JOIN,
    P2P_FIND_NEW_SUCCESSOR,
    P2P_ACTIVE
};

enum peer_types {
    NOBODY = 0,
    PREDECESSOR,
    SUCCESSOR,
    BOTH
};

#endif /* _constants_h_ */
