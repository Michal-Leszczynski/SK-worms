#ifndef SK_SCREEN_WORMS_SERVER_H
#define SK_SCREEN_WORMS_SERVER_H

#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <sys/timerfd.h>

using namespace std;

enum constants_server {
    TIMERS_AMOUNT = 26,
    TIMEOUT = 2,
    
    MAX_WIDTH = 2000,
    MAX_HEIGHT = 2000,
    
    CLIENT_MESS_SIZE = 33,
    CLIENTS_AT_ONCE = 10,
    
    RAND_MULT = 279410273,
    RAND_MOD = 4294967291,
};

struct player_t {
    uint64_t session_id = 0;
    int8_t turn_direction = -1;
    string name;

    bool ready = false;
    int timer_num = -1;
    int worm_num = -1;

    sockaddr_in6 addr {};
};

struct worm_t {
    double pos_x = 0;
    double pos_y = 0;
    int direction = 0;
    uint8_t turn_direction = 0;

    bool eliminated = false;
};

struct memory_server_t {
    uint16_t PORT_NUM = 2021;
    time_t SEED = time(nullptr);
    int TURNING_SPEED = 6;
    int ROUNDS_PER_SEC = 50;
    int WIDTH = 640;
    int HEIGHT = 480;

    bool board[MAX_WIDTH][MAX_HEIGHT] {};
    map<string, player_t> players;
    vector<vector<uint8_t>> events;
    vector<worm_t> worms;
    
    int worms_alive = -1;
    int last_event = 0;
    uint32_t game_id = 0;

    sockaddr_in6 local_addr {};
    int sock = -1;
	
	uint64_t next_message = 0;
	
    pollfd timers[TIMERS_AMOUNT] {};
    bool used_timers[TIMERS_AMOUNT] {};
    itimerspec turn_span {};
    itimerspec player_timeout {};
};

#endif
