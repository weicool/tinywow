#include "tww.h"
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

#define USERS_DIRECTORY "users"

PlayerFactory::PlayerFactory() {
    srand(time(NULL));
    mkdir(USERS_DIRECTORY, 0777);
}

Player * PlayerFactory::newPlayerFromFile(char *playerName) {
    FILE *openFile;
    string fileName = string(USERS_DIRECTORY) + string("/") + string(playerName);
    char playerData[80];
    
    int hp, exp, x, y = 0;
    Player *player;
    debug("Opening file: %s", fileName.c_str());

    openFile = fopen(fileName.c_str(), "r");

    if (openFile == NULL) {
        openFile = fopen(fileName.c_str(), "w");
        struct location loc = randomLocation();
        sprintf(playerData, "%d %d %u %u\n", randomHP(100, 120), 0, loc.x, loc.y);
        fputs(playerData, openFile);
        openFile = freopen(fileName.c_str(), "r", openFile);
    }

    assert(openFile != NULL);

    if (fscanf(openFile, "%d %d %u %u", &hp, &exp, &x, &y) == EOF) {
        debug("fscanf fails");
        throw -1;
    }
    fclose(openFile);

    struct location loc = {x, y};
    player = new Player(playerName, hp, exp, loc);
    debug("PlayerFactory created player: ");
    player->print();
    return player;
}

Player * PlayerFactory::newPlayer(char *playerName, int hp, int exp, uint8_t x, uint8_t y) {
    Player *player;
    struct location loc = {x, y};
    player = new Player(playerName, hp, exp, loc);
    return player;
}

void PlayerFactory::resurrectPlayer(Player *deadPlayer) {
    deadPlayer->myHp = randomHP(30, 50);
    debug("PlayerFactory resurrected a dead player");
}

bool PlayerFactory::savePlayer(char *playerName, int hp, int exp, uint8_t x, uint8_t y) {
    char playerData[80];
    sprintf(playerData, "%d %d %u %u\n", hp, exp, x, y);
    
    FILE *openFile;
    string fileName = string(USERS_DIRECTORY) + string("/") + string(playerName);
    openFile = fopen(fileName.c_str(), "w+");
    if (!openFile) {
        return false;
    }
    assert(openFile != NULL);
    fputs(playerData, openFile);
    fclose(openFile);
    
    debug("Saved player %s - wrote %s to %s", playerName, playerData, fileName.c_str());
    
    return true;
}

void PlayerFactory::destroyPlayer(Player *player) {
    delete player;
    
    debug("PlayerFactory deleted player: ");
    player->print();
}

int PlayerFactory::randomHP(int low, int high) {
    return random(low, high);
}

struct location PlayerFactory::randomLocation() {
    struct location loc;
    loc.x = random(0, 99);
    loc.y = random(0, 99);
    return loc;
}
