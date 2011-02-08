#include "tww.h"

Dungeon::Dungeon(unsigned int width, unsigned int height) {
    myWidth = width;
    myHeight = height;
    myMinX = 0;
    myMinY = 0;
    myMaxX = 0;
    myMaxY = 0;
    myPlayers = new PlayerList();

}

void Dungeon::setBoundary(uint8_t min_x, uint8_t min_y, uint8_t max_x, uint8_t max_y) {
    myMinX = min_x;
    myMinY = min_y;
    myMaxX = max_x;
    myMaxY = max_y;
}

Player * Dungeon::findPlayer(char *name) {
    int i = findPlayerIndex(name);
    if (i == -1) {
        return NULL;
    }
    return myPlayers->at(i);
}

void Dungeon::addPlayer(Player *player) {
    myPlayers->push_back(player);
}

void Dungeon::removePlayer(Player *player) {    
    int i = findPlayerIndex(player->myName);
    if (i == -1) {
        assert(0);
    }
    myPlayers->erase(myPlayers->begin() + i);
}

void Dungeon::clear(Player *mainPlayer) {
    Player *playerToRemove;
    for (unsigned int i = 0; i < myPlayers->size(); i++) {
        playerToRemove = myPlayers->at(i);
        if (playerToRemove != mainPlayer) {
            delete playerToRemove;
        }
    }
    delete myPlayers;
    
    myPlayers = new PlayerList();
}

void Dungeon::movePlayer(Player *player, int direction) {
    struct location newLocation;
    computeMovePlayer(player->myLocation, direction, &newLocation);
    player->myLocation.x = newLocation.x;
    player->myLocation.y = newLocation.y;
}

void Dungeon::computeMovePlayer(struct location oldLocation, int direction, struct location *newLocation) {
    int deltaX = 0;
    int deltaY = 0;
    switch (direction) {
        case NORTH: deltaY = -3; break;
        case SOUTH: deltaY =  3; break;
        case EAST:  deltaX =  3; break;
        case WEST:  deltaX = -3; break;
        default: assert(false);
    }

    newLocation->x = oldLocation.x + deltaX;
    newLocation->y = oldLocation.y + deltaY;
    if (newLocation->x < 0) {
        newLocation->x = myWidth + newLocation->x;
    } else {
        newLocation->x = newLocation->x % myWidth;
    }
    if (newLocation->y < 0) {
        newLocation->y = myHeight + newLocation->y;
    } else {
        newLocation->y = newLocation->y % myHeight;
    }
}

bool Dungeon::withinBoundary(struct location loc) {
    return (loc.x >= myMinX && loc.x <= myMaxX && loc.y >= myMinY && loc.y <= myMaxY);
}

void Dungeon::printBoundary(struct location loc) {
    if (loc.x + VISION_RANGE > myMaxX) {
        on_close_to_boundary(1, myMaxX);
    }
    if (loc.x - VISION_RANGE < myMinX) {
        on_close_to_boundary(1, myMinX);
    }
    if (loc.y + VISION_RANGE > myMaxY) {
        on_close_to_boundary(2, myMaxY);
    }
    if (loc.y - VISION_RANGE < myMinY) {
        on_close_to_boundary(2, myMinY);
    }
}

bool Dungeon::inVision(Player *player, Player *otherPlayer) {
    unsigned int x, y, otherX, otherY;
    x = player->myLocation.x;
    y = player->myLocation.y;
    otherX = otherPlayer->myLocation.x;
    otherY = otherPlayer->myLocation.y;

    if (!Dungeon::withinBoundary(player->myLocation) ||
        !Dungeon::withinBoundary(otherPlayer->myLocation)) {
        return false;
    }

    return (otherX >= x - VISION_RANGE) &&
           (otherX <= x + VISION_RANGE) &&
           (otherY >= y - VISION_RANGE) &&
           (otherY <= y + VISION_RANGE);
}

int Dungeon::findPlayerIndex(char *name) {
    for (int i = 0; i < myPlayers->size(); i++) {
        if (!strcmp(myPlayers->at(i)->myName, name)) {
            return i;
        }
    }
    return -1;
}

bool Dungeon::locationOccupied(int x, int y) {
    Player *player;
    for (int i = 0; i < myPlayers->size(); i++) {
        player = myPlayers->at(i);
        if (player->myLocation.x == x && player->myLocation.y == y) {
            return true;
        }
    }
    return false;
}

void Dungeon::playersInRangeOf(Player *player, PlayerList &players) {
    Player *otherPlayer;
    for (int i = 0; i < myPlayers->size(); i++) {
        otherPlayer = myPlayers->at(i);
        if (otherPlayer != player && inVision(player, otherPlayer)) {
            players.push_back(otherPlayer);
        }
    }
}

void Dungeon::incrementHPForAllPlayers() {
    Player *player;
    for (int i = 0; i < myPlayers->size(); i++) {
        player = myPlayers->at(i);
        player->myHp++;
    }
}

void Dungeon::print() {
    unsigned int x, y;
    for (y = 0; y <= myHeight; y++) {
        printf("+");
        for (x = 0; x < myWidth; x++) {
            if (locationOccupied(x, y)) {
                printf("O");
            } else {
                printf("-");
            }
        }
        printf("+\n");
    }
}
