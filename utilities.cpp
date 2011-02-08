#include "tww.h"

void debug(const char* format, ...) {
    if (DEBUG) {
        va_list ap;
        va_start(ap, format);
        fprintf(stdout, (char *) "-##DEBUG##-: ");
        vfprintf(stdout, format, ap);
        va_end(ap);
        fprintf(stdout, "\n");
    }
}

void null_terminate(char *string, unsigned int position) {
    string[position] = '\0';
}

size_t calculatePaddingSize(int length) {
    size_t paddingSize = 0;
    if ((paddingSize = length % sizeof(int)) == 0) {
        return paddingSize;
    }
    paddingSize = sizeof(int) - paddingSize;
    debug("Padding SIZE: %d", paddingSize);
    return paddingSize;
}

int random(int low, int high) {
    int n = (rand() % (high - low + 1)) + low;
    debug("generated random number %d in range (%d, %d)", n, low, high);
    return n;
}

uint32_t calc_p2p_id(unsigned char *name) {
    unsigned int hashval;
    for (hashval = 0; *name != 0; name++) {
        hashval = *name + 31*hashval;
    }
    return hashval % 1024;
}

void printUserData(struct p2p_user_data user) {
    debug("user data:%s hp:%d exp:%d, (%u, %u)", user.name, user.hp, user.exp, user.x, user.y);
}
