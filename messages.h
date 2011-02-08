/**************************************************************************
 * The reference server implementation of the EE122 Project #1
 *
 * Author: Junda Liu (liujd@cs)
 * Author: DK Moon (dkmoon@cs)
 * Author: David Zats (dzats@cs)
**************************************************************************/

#ifndef _messages_h_
#define _messages_h_

#include <assert.h>
#include <stdint.h>
#include <cctype>

#include "constants.h"
#include "tww.h"

/**************************************************************************
  * Message functions
  ************************************************************************/
static inline void show_prompt()
{
  fprintf(stdout, "command> ");
  fflush(stdout);
}

static inline void on_invalid_udp_source() {
  fprintf(stdout, "**Received UDP packet from unexpected source.\n");
  fflush(stdout);
}

static inline void on_malformed_udp() {
  fprintf(stdout, "**Received malformed UDP packet.\n");
  fflush(stdout);
}

static inline void on_udp_duplicate(unsigned int ipaddr) {
  fprintf(stdout,"**Received duplicate packet from %x.\n",ipaddr);
  fflush(stdout);
}

static inline void on_udp_attempt(int attempt) {
  fprintf(stdout, "**UDP transmission attempt:%d.\n",attempt);
  fflush(stdout);
}

static inline void on_udp_fail() {
  fprintf(stdout, "Error: did not obtain response from server.\n");
  fflush(stdout);
}

static inline void on_loc_resp(unsigned char type, unsigned int ipaddr, unsigned short udpport) {

  fprintf(stdout, "**LOC Resp - TYPE: %d IPADDR: %x UDPPORT: %x.\n", type, ipaddr, udpport);
  fflush(stdout);
}

static inline void on_state_resp(unsigned char type, char* name, int hp, int exp, unsigned char x, unsigned char y) {
  fprintf(stdout, "**State Resp - TYPE: %d NAME: %s HP: %d EXP: %d XLOC: %d YLOC: %d.\n", type, name, hp, exp, x, y);
  fflush(stdout);
}

static inline void on_area_resp (unsigned char type, unsigned int ipaddr, unsigned short port, unsigned char minx, unsigned char maxx, unsigned char miny, unsigned char maxy) {
  fprintf(stdout, "**Area Resp - TYPE: %d IPADDR: %x PORT: %x MINX: %d MAXX: %d MINY: %d MAXY: %d\n",type, ipaddr, port, minx, maxx, miny, maxy);
  fflush(stdout);
}

static inline void on_save_resp (unsigned char type, unsigned char ecode) {
  if(ecode == 0) {
    printf("**Save Resp - TYPE: %d RESULT: Success.\n", type);
  }
  else if (ecode == 1) {
    printf("**Save Resp - TYPE: %d RESULT: FAILURE.\n", type);

  }
  else {
    assert(0);
  }
  fflush(stdout);
}

static inline void on_before_login() {
  fprintf(stdout, "Must login before using this command.\n");
}

static inline void on_close_to_boundary(char type, int bound_loc) {
  if(type == 1) {
    fprintf(stdout, "Cannot see past x-boundary at: %d.\n",bound_loc);
  }
  else if (type == 2) {
    fprintf(stdout, "Cannot see past y-boundary at: %d.\n",bound_loc);
  }
  else {
    assert(0);
  }
  show_prompt();
}

/* Error messages for the client. */
static inline void on_client_connect_failure()
{
  fprintf(stdout,
          "The gate to the tiny world of warcraft is not ready.\n");
}

static inline void on_malformed_message_from_server()
{
  fprintf(stdout, "Meteor is striking the world.\n");
  exit(1);
}

static inline void on_disconnection_from_server()
{
  fprintf(stdout,
          "The gate to the tiny world of warcraft has disappeared.\n");
}

static inline void on_not_visible()
{
  fprintf(stdout, "The target is not visible.\n");
  show_prompt();
}

static inline void on_login_reply(const int error_code)
{
  if (error_code == 0) {
    fprintf(stdout, "Welcome to the tiny world of warcraft.\n");
  } else if (error_code == 1) {
    fprintf(stdout, "A player with the same name is already in the game.\n");
  } else {
    /* This must not happen. The caller of this function must check the
       error code is either 0 or 1 and must perform malformed message
       handling before this function. We simply die here. */
    assert(0);
  }
  show_prompt();
}

static inline void on_move_notify(const char *player_name,
                                  const uint8_t x,
                                  const uint8_t y,
                                  const uint32_t hp,
                                  const uint32_t exp)
{
  fprintf(stdout, "%s: location=(%u,%u), HP=%u, EXP=%u\n",
          player_name, x, y, hp, exp);
  show_prompt();
}

static inline void on_attack_notify(const char *attacker_name,
                                    const char *victim_name,
                                    const int damage,
                                    const int updated_hp)
{
  if (updated_hp <= 0) {
    fprintf(stdout, "%s killed %s\n", attacker_name, victim_name);
  } else {
    fprintf(stdout, "%s damaged %s by %d. %s's HP is now %d\n",
            attacker_name, victim_name, damage, victim_name, updated_hp);
  }
  show_prompt();
}

static inline void on_speak_notify(const char *speaker, const char *message)
{
  fprintf(stdout, "%s: %s\n", speaker, message);
  show_prompt();
}

static inline void on_exit_notify(const char *player_name)
{
  fprintf(stdout, "Player %s has left the tiny world of warcraft.\n",
          player_name);
  show_prompt();
}

static inline void on_invalid_state(const int error_code)
{
  if (error_code == 0) {
    fprintf(stdout, "You must log in first.\n");
  } else if (error_code == 1) {
    fprintf(stdout, "You already logged in.\n");
  } else {
    /* This must not happen. The caller of this function must check the
       error code is either 0 or 1, and must perform malformed message
       handling before this function. We simply die here. */
    assert(0);
  }
  show_prompt();
}

static inline void on_invalid_command(const char *command) {
    fprintf(stdout, "! Invalid command: %s.\n", command);
    fprintf(stdout, "Available commands = login, move, attack, speak, logout\n");
}

static inline void on_invalid_name(const char *name) {
    fprintf(stdout, "! Invalid name: %s.\n", name);
}

static inline void on_invalid_message() {
    fprintf(stdout, "! Invalid text message.\n");
}

static inline void on_invalid_syntax() {
    fprintf(stdout, "! Invalid syntax.\n");
}

static inline void on_invalid_direction(const char *direction) {
    fprintf(stdout, "! Invalid direction: %s\n", direction);
}

static inline void client_usage() {
  fprintf(stdout, "! Usage: ./client -p <port_number> -s <server_ip>\n");
}

/**************************************************************************
  * Message functions for the server
  ************************************************************************/
static inline void on_server_init_success() {
    fprintf(stdout, "* Listening socket is ready.\n");
}

/* Error messages for the server. */
static inline void on_server_port_bind_failure() {
    fprintf(stdout, "The gate cannot be opened there.\n");
}

static inline void on_server_invalid_port() {
    fprintf(stdout, "! The server port number is not defined.\n");
}

static inline void on_server_failure() {
    fprintf(stdout, "Fail.\n");
    exit(1);
}

/**************************************************************************
  * Message functions for the tracker
  ************************************************************************/
static inline void tracker_on_malformed_udp(int type) {
  switch(type) {
  case 1:
    printf("Received malformed packet: wrong size.\n");
    break;
  case 2:
    printf("Received malformed packet: undefined type.\n");
    break;
  default:
    printf("Error: Undefined malformed type.\n");
    exit(0);
  }

  fflush(stdout);
}

static inline void tracker_on_udp_duplicate(unsigned int ipaddr) {
  printf("Received duplicate packet from: %x.\n",ipaddr);
  fflush(stdout);
}

static inline void on_tracker_failure() {
    fprintf(stdout, "Fail.\n");
    exit(1);
}


/**************************************************************************
  * Utility functions
  ************************************************************************/
/* Returns 1 if the name is valid. */
static inline int check_player_name(const char *name)
{
  size_t i;
  /* Must not be null. */
  if (!name[0]) return 0;
  for (i = 0; i <= MAX_LOGIN_LENGTH; ++ i) {
    if (!name[i]) return 1;
    /* Must not contain a non-alphanumeric character. */
    if (!isalnum(name[i])) return 0;
  }
  /* Must be null terminated. */
  return 0;
}

/* Returns if 1 if the text message is valid. */
static inline int check_player_message(const char *message)
{
  size_t i;
  bool allSpaces = true;
  /* Must not be null. */
  if (!message[0]) return 0;
  for (i = 0; i <= MAX_MSG_LENGTH; ++ i) {
    if (!message[i]) return !allSpaces;
    /* Must not contain a non-printable character. */
    if (!isprint(message[i])) {
        return 0;
    }
    if (message[i] != ' ') {
        allSpaces = false;
    }
  }

  /* Must be null terminated. */
  return 0;
}


/* Returns 1 if the command name is a valid command. */
static inline int check_command(const char *command)
{
    return !strcmp(command, "login") || !strcmp(command, "logout") ||
           !strcmp(command, "move") || !strcmp(command, "attack") ||
           !strcmp(command, "speak") || !strcmp(command, "map");
}

#endif /* _messages_h_ */
