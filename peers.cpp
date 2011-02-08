#include "tww.h"
#include <dirent.h>

using namespace std;

#define USERS_DIRECTORY "users"

static bool p2pIDSort(ServerEntry *i, ServerEntry *j);

Peers::Peers(std::string filename, ServerEntry *thisServer) {
    myPeers = new ServerEntryList();
    myServer = thisServer;
    myFileName = filename;
    myFirstTimeRead = true;
}

/** Reads peers.lst, updates my peers and adds this server to the list. */
void Peers::readPeers() {
    debug("readPeers");

    clearPeers();
    
    ifstream file(myFileName.c_str(), ifstream::in);
    if (file.fail()) {
        /* Create file. */
        debug("cannot open %s", myFileName.c_str());
        ofstream newFile(myFileName.c_str());
        newFile.close();
    }

    unsigned int p2pID;
    string ip;
    unsigned short tcpPort;
    string line;
    char buffer[1024];
    ServerEntry *serverEntry;
    bool isMyServerInPeers = false;
    while (file.good()) {
        file.getline(buffer, 1024);
        string line(buffer);
        if (line.empty()) {
            break;
        }
        
        stringstream ss(stringstream::in | stringstream::out);
        ss.str(line);
        
        ss >> p2pID >> ip >> tcpPort;
        debug("Read server %u, %s, %u", p2pID, ip.c_str(), tcpPort);

        serverEntry = new ServerEntry(p2pID,ip, tcpPort, 0);

        if (serverEntry->id == myServer->id) {
            if (strcmp(serverEntry->ip, myServer->ip) ||
                serverEntry->tcpPort != myServer->tcpPort) {
                    debug("P2P_ID conflicts.");
                    throw -1;
            }
            delete serverEntry;
            myPeers->push_back(myServer);
            isMyServerInPeers = true;
        } else {
            myPeers->push_back(serverEntry);
        }

    }
    file.close();
    if (myFirstTimeRead) {
        myFirstTimeRead = false;
        if (!isMyServerInPeers) {
            registerServer(myServer);
        }
    } else if (!isMyServerInPeers) {
        throw -1;
    }

    sort(myPeers->begin(), myPeers->end(), p2pIDSort);
    
    calculateRanges();
    
    printf("P2P: peers.lst: p2p_head");
    for (int i = 0; i < myPeers->size(); i++) {
        printf("->(%d %d)", myPeers->at(i)->primaryRange.low, myPeers->at(i)->primaryRange.high);
    }
    if (DEBUG) {
        for (int i = 0; i < myPeers->size(); i++) {
            myPeers->at(i)->print();
        }
    }
}

void Peers::registerServer(ServerEntry *server) {
    debug("registerServer");
    ofstream file;
    file.open(myFileName.c_str(), ifstream::app);
    if (file.fail()) {
        debug("cannot open %s", myFileName.c_str());
        throw -1;
    }
    file << myServer->id << " "
         << string(myServer->ip) << " "
         << myServer->tcpPort << endl;
    file.close();
    
    myPeers->push_back(server);
}

void Peers::calculateRanges() {
    debug("calculateRanges");
    ServerEntry *peer, *prevPeer;
    for (unsigned int i = 0; i < myPeers->size(); i++) {
        peer = myPeers->at(i);
        prevPeer = myPeers->at(i == 0 ? myPeers->size()-1 : i-1);
        peer->primaryRange.low = prevPeer->id + 1;
        peer->primaryRange.high = peer->id;
        if (i != 0) {
            peer->backupRange = prevPeer->primaryRange;
        }
    }
    myPeers->at(0)->backupRange = myPeers->at(myPeers->size()-1)->primaryRange;
}

int Peers::whoIsMyPeer(int p2pID) {
    ServerEntry *peer, *prevPeer, *nextPeer;
    for (unsigned int i = 0; i < myPeers->size(); i++) {
        peer = myPeers->at(i);
        if (myPeers->at(i)->id == p2pID) {
            nextPeer = myPeers->at(i == myPeers->size()-1 ? 0 : i+1);
            prevPeer = myPeers->at(i == 0 ? myPeers->size()-1 : i-1);
            if (myServer == nextPeer && myServer == prevPeer) {
                return BOTH;
            } else if (myServer == nextPeer) {
                return PREDECESSOR;
            } else if (myServer == prevPeer) {
                return SUCCESSOR;
            } else {
                return NOBODY;
            }
        }
    }
    return NOBODY;
}

ServerEntry *Peers::findSuccessor(ServerEntry *server) {
    return findPeer(server, 1);
}
    
ServerEntry *Peers::findPredecessor(ServerEntry *server) {
    return findPeer(server, -1);
}

ServerEntry *Peers::findPeer(ServerEntry *server, int incrementBy) {
    assert(myPeers->size() != 0);
    if (myPeers->size() <= 1) {
        return NULL;
    }
    
    int indexOfServer = findServerIndex(server);
    if (indexOfServer == -1) {
        debug("FAIL: server whose peer we want to find does not exist");
        throw -1;
    }
    
    int indexOfPeer = indexOfServer + incrementBy;
    if (indexOfPeer < 0) {
        indexOfPeer = myPeers->size() + indexOfPeer;
    } else {
        indexOfPeer = indexOfPeer % myPeers->size();
    }
    ServerEntry *peer = myPeers->at(indexOfPeer);
    return peer;
}

int Peers::findServerIndex(ServerEntry *server) {
    for (int i = 0; i < myPeers->size(); i++) {
        if (server == myPeers->at(i)) {
            return i;
        }
    }
    return -1;
}

void Peers::clearPeers() {
    ServerEntry *peer;
    for (int i = 0; i < myPeers->size(); i++) {
        peer = myPeers->at(i);
        if (peer->id != myServer->id) {
            delete peer;
        }
    }
    delete myPeers;
    myPeers = new ServerEntryList();
}

UserDataList *Peers::findUsersInRange(uint16_t min, uint16_t max) {
    debug("findUsersInRange");
    
    UserDataList *users = new UserDataList();
    
    struct dirent *entry;
    DIR *dir = opendir(USERS_DIRECTORY);
    if (!dir) {
        debug("FAIL: can't open users directory!");
        throw -1;
    }
    
    char *userName;
    struct p2p_user_data userData;
    uint32_t p2pID;
    while (entry = readdir(dir)) {
        userName = entry->d_name;
        if (strcmp(userName, ".") && strcmp(userName, "..")) {
            userData = loadUser(userName);
            p2pID = calc_p2p_id((unsigned char *) userName);
            if ((p2pID >= min && p2pID <= max) || (min > max && p2pID >= min)) {
                users->push_back(userData);
                debug("chose user: %s, id: %u", userData.name, p2pID);
            }
        }
    }
    
    closedir(dir);
    
    return users;
}

bool Peers::writeUserDataToDisk(struct p2p_user_data user) {
    string fileName = string(USERS_DIRECTORY) + string("/") + string(user.name);
    FILE *openFile = fopen(fileName.c_str(), "w+");
    if (!openFile) {
        return false;
    }
    assert(openFile);
    
    char playerData[80];
    sprintf(playerData, "%d %d %u %u\n", user.hp, user.exp, user.x, user.y);
    fputs(playerData, openFile);
    fclose(openFile);
    
    return true;
}

struct p2p_user_data Peers::loadUser(char *name) {
    struct p2p_user_data user;
    memset(&user, 0, sizeof(user));
    
    string fileName = string(USERS_DIRECTORY) + string("/") + string(name);
    char userData[80];
    FILE *openFile = fopen(fileName.c_str(), "r");
    if (!openFile) {
        debug("FAIL: cannot open user data");
        throw -1;
    }

    unsigned int x, y;
    if (fscanf(openFile, "%d %d %u %u", &user.hp, &user.exp, &x, &y) == EOF) {
        debug("fscanf fails");
        throw -1;
    }
    strncpy(user.name, name, MAX_LOGIN_LENGTH + 1);
    user.x = x;
    user.y = y;
    
    fclose(openFile);
    
    return user;
}

static bool p2pIDSort(ServerEntry *i, ServerEntry *j) {
    return i->id < j->id;
}




