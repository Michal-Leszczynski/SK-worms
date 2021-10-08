#ifndef SK_SCREEN_WORMS_CLIENT_H
#define SK_SCREEN_WORMS_CLIENT_H

#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <netdb.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <endian.h>

#include "common.h"

using namespace std;

enum constants_client {
    LD,
    LU,
    RD,
    RU,
    
    MESS_BASIC_LEN = 13,
    MESSAGE_SPAN = 30000,
    
    SERVER_AT_ONCE = 10,
    MAX_TCP = 14,
};

struct memory_client_t {
    string name;

    uint8_t direction = 0;

    int server_sock = -1;
    int gui_sock = -1;

    uint32_t game_id = 0;
    uint32_t next_event_no = 0;
    uint64_t session_id;
    uint32_t players_cnt = 0;
    uint32_t max_x = 0;
    uint32_t max_y = 0;
    uint64_t next_message = 0;

    string player_names[MAX_PLAYERS] {};
};

#endif
