#include "screen-worms-server.h"
#include "common.h"
#include <sys/time.h>

// Random number generator.
uint32_t my_rand(memory_server_t &mem) {
    static uint32_t r = mem.SEED;
    uint32_t result = r;
    uint64_t new_r = r;
    new_r *= RAND_MULT;
    new_r %= RAND_MOD;
    r = new_r;
    return result;
}

// Sets the board to initial state.
void clean_board(memory_server_t &mem) {
    for (int i = 0; i < mem.WIDTH; i++) {
        for (int j = 0; j < mem.HEIGHT; j++) {
            mem.board[i][j] = false;
        }
    }
}

// Parses options and their values as it is said in the task.
// Adds restrictions to values.
void update_options(memory_server_t &mem, int argc, char *argv[]) {
    int opt;

    while ((opt = getopt(argc, argv, "p:s:t:v:w:h:")) != -1) {
        if (opt == '?') {
            cout<<"Unknown option"<<endl;
            exit(1);
        }

        char *ptr;
        uint32_t val = strtol(optarg, &ptr, 10);

        if (*ptr != 0) {
            cout<<"incorrect option value"<<endl;
            exit(1);
        }

        if (opt == 'p') {
            mem.PORT_NUM = val;
        }
        else if (opt == 's') {
            mem.SEED = val;
        }
        else if (opt == 't') {
            mem.TURNING_SPEED = val;
        }
        else if (opt == 'v') {
            mem.ROUNDS_PER_SEC = val;
        }
        else if (opt == 'w') {
            if (val > MAX_WIDTH) {
                cout<<"too wide"<<endl;
                exit(1);
            }
            mem.WIDTH = val;
        }
        else if (opt == 'h') {
            if (val > MAX_HEIGHT) {
                cout<<"too high"<<endl;
                exit(1);
            }
            mem.HEIGHT = val;
        }
    }
}

// Creates and binds ip6 socket to listen both from ip4 and ip6 clients.
// Sets the socket to be non blocking.
void create_socket(memory_server_t &mem) {
    mem.sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (mem.sock < 0) {
        cout<<"socket"<<endl;
        exit(1);
    }

    struct timeval tv {};
    tv.tv_sec = 0;
    tv.tv_usec = RCV_WAIT;
    if (setsockopt(mem.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) != 0) {
        cout<<"socket non blocking"<<endl;
        exit(1);
    }

    int v6OnlyEnabled = 0;
    if (setsockopt(mem.sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6OnlyEnabled, sizeof(v6OnlyEnabled)) != 0) {
        cout<<"socket ip6 only disabled"<<endl;
        exit(1);
    }

    mem.local_addr.sin6_family = AF_INET6;
    mem.local_addr.sin6_flowinfo = 0;
    mem.local_addr.sin6_addr = in6addr_any;
    mem.local_addr.sin6_port = htons(mem.PORT_NUM);
    if (bind(mem.sock, (sockaddr*) &mem.local_addr, sizeof(mem.local_addr)) < 0) {
        cout<<"bind"<<endl;
        exit(1);
    }
}

// Creates timers for future players (doesn't arm them)
// and sets future countdowns for timers.
void set_timers(memory_server_t &mem) {
    for (int i = 0; i <= MAX_PLAYERS; i++) {
        mem.timers[i].fd = timerfd_create(CLOCK_REALTIME,  0);
        mem.timers[i].events = POLLIN;
        mem.timers[i].revents = 0;
    }

    mem.turn_span.it_value.tv_sec = 0;
    mem.turn_span.it_value.tv_nsec = 1000000000 / mem.ROUNDS_PER_SEC;
    mem.turn_span.it_interval.tv_sec = 0;
    mem.turn_span.it_interval.tv_sec = 1000000000 / mem.ROUNDS_PER_SEC;

    mem.player_timeout.it_value.tv_sec = TIMEOUT;
    mem.player_timeout.it_value.tv_nsec = 0;
    mem.player_timeout.it_interval.tv_sec = 0;
    mem.player_timeout.it_interval.tv_sec = 0;
}

// Divides number to bytes and place them at the end of data in Big Endian order.
void encode_number(vector<uint8_t> &data, uint64_t number, uint8_t bytes) {
    stack<uint8_t> s;

    for (int i = 0; i < bytes; i++) {
        s.push(number % BYTE_RANGE);
        number /= BYTE_RANGE;
    }

    while (!s.empty()) {
        data.push_back(s.top());
        s.pop();
    }
}

// Calculates crc for given data
// and places it at the end of data in Big Endian order.
void calculate_crc(vector<uint8_t> &data) {
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < data.size(); i++) {
        uint64_t temp = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_tab[temp];
    }

    crc = crc ^ 0xFFFFFFFF;
    encode_number(data, crc, DWORD);
}

// Returns player's id which is concatenation of
// player's address, '/' and player's port (it's unique).
string get_player_id(sockaddr_in6 &player_addr) {
    char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, player_addr.sin6_addr.s6_addr, str, INET6_ADDRSTRLEN);
    string tmp = str;
    return tmp + "/" + to_string(player_addr.sin6_port);
}

// Sends events from next_expected_event_no to the most recent one to given client.
// Tries to fit as many events in one datagram as it is possible.
void send_events_to_client(memory_server_t &mem, uint32_t next_expected_event_no, sockaddr_in6 &client_addr) {
    static uint8_t buff[MAX_UDP];
	
	if (next_expected_event_no < mem.events.size()) {
		vector<uint8_t> game_id;
		encode_number(game_id, mem.game_id, DWORD);
		memcpy(buff, &game_id[0], DWORD);
		uint64_t len = DWORD;

		for (size_t i = next_expected_event_no; i < mem.events.size(); i++) {
		    if (len + mem.events[i].size() > MAX_UDP) {
		        sendto(mem.sock, buff, len, 0, (sockaddr*) &client_addr, sizeof(client_addr));
		        
		        memcpy(buff, &game_id[0], DWORD);
				len = DWORD;
		    }

		    memcpy(buff + len, &mem.events[i][0], mem.events[i].size());
		    len += mem.events[i].size();
		}

		if (len != 0) {
		    if (sendto(mem.sock, buff, len, 0, (sockaddr*) &client_addr, sizeof(client_addr)) <= 0) {
		    	cout<<"send to client"<<endl;
		    	exit(1);
		    }
		}
    }
}

// Sends new events (the ones that haven't been sent before) to all clients.
void send_last_event_to_all_clients(memory_server_t &mem) {
    for (auto it = mem.players.begin(); it != mem.players.end(); it++) {
        send_events_to_client(mem, mem.last_event, it->second.addr);
    }
    
    mem.last_event = mem.events.size();
}

// Adds encoded new game event to events vector. Player's names are stored in
// first element of vector order. The second element is player's id.
void add_new_game_event(memory_server_t &mem, vector<pair<string, string>> &order) {
    vector<uint8_t> new_event;
    uint32_t len = EVENT_BASIC_LENGTH;

    for (auto &i : order) {
        len += i.first.size() + 1;
    }

    encode_number(new_event, len, DWORD);
    encode_number(new_event, mem.events.size(), DWORD);
    encode_number(new_event, NEW_GAME_TYPE, BYTE);
    encode_number(new_event, mem.WIDTH, DWORD);
    encode_number(new_event, mem.HEIGHT, DWORD);

    for (auto &i : order) {
        for (char j : i.first) {
            encode_number(new_event, j, BYTE);
        }

        encode_number(new_event, '\0', BYTE);
    }

    calculate_crc(new_event);
    mem.events.push_back(new_event);
    //send_last_event_to_all_clients(mem);
}

// Adds encoded pixel event to events vector.
void add_pixel_event(memory_server_t &mem, uint8_t player, uint32_t x, uint32_t y) {
    vector<uint8_t> new_event;

    encode_number(new_event, PIXEL_LENGTH, DWORD);
    encode_number(new_event, mem.events.size(), DWORD);
    encode_number(new_event, PIXEL_TYPE, BYTE);
    encode_number(new_event, player, BYTE);
    encode_number(new_event, x, DWORD);
    encode_number(new_event, y, DWORD);

    calculate_crc(new_event);
    mem.events.push_back(new_event);
    //send_last_event_to_all_clients(mem);
}

// Add encoded eliminated event to events vector.
void add_eliminated_event(memory_server_t &mem, uint8_t player) {
    vector<uint8_t> new_event;

    encode_number(new_event, ELIMINATED_LENGTH, DWORD);
    encode_number(new_event, mem.events.size(), DWORD);
    encode_number(new_event, ELIMINATED_TYPE, BYTE);
    encode_number(new_event, player, BYTE);

    calculate_crc(new_event);
    mem.events.push_back(new_event);
    send_last_event_to_all_clients(mem);
}

// Add encoded game over event to events vector.
void add_game_over_event(memory_server_t &mem) {
	for (auto &it : mem.players) {
        it.second.ready = false;
    }

    vector<uint8_t> new_event;

    encode_number(new_event, GAME_OVER_LENGTH, DWORD);
    encode_number(new_event, mem.events.size(), DWORD);
    encode_number(new_event, GAME_OVER_TYPE, BYTE);

    calculate_crc(new_event);
    mem.events.push_back(new_event);
   // send_last_event_to_all_clients(mem);
}

// Makes moves of all remaining worms. Returns true
// if the game ended during this turn. Updates all encountered events and sends them at the end.
bool make_moves(memory_server_t &mem) {
    for (size_t i = 0; i < mem.worms.size(); i++) {
        worm_t *worm = &mem.worms[i];

        if (!worm->eliminated) {
            int old_x = floor(worm->pos_x);
            int old_y = floor(worm->pos_y);

            if (worm->turn_direction == RIGHT) {
                worm->direction += mem.TURNING_SPEED;
            }
            else if (worm->turn_direction == LEFT) {
                worm->direction += 360 - mem.TURNING_SPEED;
            }
            worm->direction %= 360;

            worm->pos_x += cos(worm->direction * M_PI / 180);
            worm->pos_y += sin(worm->direction * M_PI / 180);
            int new_x = floor(worm->pos_x);
            int new_y = floor(worm->pos_y);

            if (old_x != new_x || old_y != new_y) {
                if (new_x < 0 || mem.WIDTH <= new_x || new_y < 0 || mem.HEIGHT <= new_y ||
                    mem.board[new_x][new_y]) {
                    mem.worms_alive--;
                    worm->eliminated = true;
                    add_eliminated_event(mem, i);

                    if (mem.worms_alive == 1) {
                        add_game_over_event(mem);
                        send_last_event_to_all_clients(mem);
                        
                        return true;
                    }
                }
                else {
                    mem.board[new_x][new_y] = true;
                    add_pixel_event(mem, i, new_x, new_y);
                }
            }
        }
    }
	
	send_last_event_to_all_clients(mem);
    return false;
}

// Creates worms (in proper order) and generates their positions.
// Updates all encountered events and sets the game turn timer as well.
bool initialize_game(memory_server_t &mem) {
    for (auto &it : mem.players) {
        it.second.worm_num = -1;
    }

    mem.worms.clear();
    mem.events.clear();
    mem.last_event = 0;
    clean_board(mem);
    mem.game_id = my_rand(mem);

    vector<pair<string, string>> order;

    for (auto &player : mem.players) {
        if (player.second.ready) {
            order.emplace_back(player.second.name, player.first);
        }
    }
    
    mem.worms_alive = order.size();
    sort(order.begin(), order.end());
    add_new_game_event(mem, order);

    for (size_t i = 0; i < order.size(); i++) {
        string id = order[i].second;

        mem.players[id].worm_num = i;

        worm_t worm {};
        worm.eliminated = false;
        worm.turn_direction = mem.players[id].turn_direction;
        worm.pos_x = (my_rand(mem) % mem.WIDTH) + 0.5;
        worm.pos_y = (my_rand(mem) % mem.HEIGHT) + 0.5;
        worm.direction = my_rand(mem) % 360;
		
        int x = floor(worm.pos_x);
        int y = floor(worm.pos_y);
		
        if (x < 0 || mem.WIDTH <= x || y < 0 || mem.HEIGHT <= y ||
            mem.board[x][y]) {
            mem.worms_alive--;
            worm.eliminated = true;
            add_eliminated_event(mem, i);

            if (mem.worms_alive == 1) {
                add_game_over_event(mem);

                return true;
            }
        }
        else {
            mem.board[x][y] = true;
            add_pixel_event(mem, i, x, y);
        }
        
        mem.worms.push_back(worm);
    }

    timeval tv {};
    gettimeofday(&tv, nullptr);
	mem.next_message = tv.tv_sec * 1000000 + tv.tv_usec + mem.turn_span.it_value.tv_nsec / 1000;;
    
    return false;
}

// Checks whether given message should be ignored.
bool is_ignored(memory_server_t &mem, client_mess_t &mess, string &id) {
    string name = reinterpret_cast<char*>(mess.player_name);

    if (mess.turn_direction > LEFT) {
        return true;
    }

    for (char i : name) {
        if (i < PLAYER_MIN_CHAR || PLAYER_MAX_CHAR < static_cast<uint8_t>(i)) {
            return true;
        }
    }

    if (mem.players.find(id) != mem.players.end()) {
        return (mem.players[id].session_id < mess.session_id || mem.players[id].name != name);
    }
    else {
        return (mem.players.size() > MAX_PLAYERS);
    }
}

// Erases player from memory and frees his timer.
void disconnect_player(memory_server_t &mem, string &id) {
    mem.used_timers[mem.players[id].timer_num] = false;
    mem.players.erase(id);
}

// Disconnects all timed out players.
void disconnect_timeout(memory_server_t &mem) {
    poll(mem.timers, TIMERS_AMOUNT, 0);

    for (auto it = mem.players.begin(); it != mem.players.end(); ) {
        int timer_num = it->second.timer_num;

        if (mem.timers[timer_num].revents & POLLIN) {
            uint64_t dummy;

            if (read(mem.timers[timer_num].fd, &dummy, sizeof(dummy)) < 0) {
                cout<<"timer"<<endl;
                exit(1);
            }

            mem.timers[timer_num].revents = 0;
            string id = it->first;
			
			it++;
            disconnect_player(mem, id);
            continue;
        }
        
        it++;
    }
}

// Disconnects client if necessary, afterwards checks if he is good enough to be connected.
void add_client(memory_server_t &mem, string &id, client_mess_t &mess, sockaddr_in6 &client_addr) {
    if (mem.players.find(id) != mem.players.end() && mem.players[id].session_id < mess.session_id) {
        disconnect_player(mem, id);
    }

    if (mem.players.find(id) == mem.players.end()) {
        player_t new_player {};
        new_player.session_id = mess.session_id;
        new_player.turn_direction = mess.turn_direction;
        new_player.name = reinterpret_cast<char*>(mess.player_name);
        new_player.addr = client_addr;

        for (uint32_t i = 1; i < TIMERS_AMOUNT; i++) {
            if (!mem.used_timers[i]) {
                new_player.timer_num = i;
                mem.used_timers[i] = true;
                mem.timers[i].revents = 0;
                timerfd_settime(mem.timers[i].fd, 0, &mem.player_timeout, nullptr);
                break;
            }
        }
        
        mem.players[id] = new_player;
    }
}

// Reads message from client. Makes proper action if it's not ignored.
// Resets client's timer.
bool read_from_client(memory_server_t &mem) {
    client_mess_t mess {};
    sockaddr_in6 client_addr {};
   	int mess_len;
    socklen_t addr_len = sizeof(client_addr);

    memset(&mess, 0, sizeof(client_mess_t));
    mess_len = recvfrom(mem.sock, &mess, sizeof(client_mess_t), 0, (sockaddr*) &client_addr, &addr_len);
    
    if (mess_len <= 0) {
        return false;
    }
	
	uint64_t tmp = mess_len;
	if (tmp > CLIENT_MESS_SIZE || tmp < CLIENT_MESS_SIZE - PLAYER_NAME_LENGTH) {
		return true;
	}
	
	mess.session_id = be64toh(mess.session_id);
	mess.next_expected_event_no = be32toh(mess.next_expected_event_no);
    string id = get_player_id(client_addr);
	
    if (!is_ignored(mem, mess, id)) {
        add_client(mem, id, mess, client_addr);
        player_t *player = &mem.players[id];
        player->turn_direction = mess.turn_direction;
        send_events_to_client(mem, mess.next_expected_event_no, client_addr);
        timerfd_settime(mem.timers[player->timer_num].fd, 0, &mem.player_timeout, nullptr);
		
        if (player->worm_num >= 0) {
            mem.worms[player->worm_num].turn_direction = mess.turn_direction;
        }

        if (mess.turn_direction != 0 && mess.player_name[0] != '\0') {
            player->ready = true;
        }
    }
    
    return true;
}

// Checks whether conditions for starting the game are fulfiled.
bool check_for_game_start(memory_server_t &mem) {
    int cnt = 0;

    for (auto &player : mem.players) {
        if (player.second.ready) {
            cnt++;
        }
        else if (!player.second.name.empty()) {
            return false;
        }
    }

    return (cnt >= 2);
}

// Waits for proper conditions to start the game and performs
// reading from players and disscontcting as well.
bool start_game(memory_server_t &mem) {
    while (true) {
        disconnect_timeout(mem);

        if (check_for_game_start(mem)) {
            break;
        }

        for (uint32_t i = 0; i < CLIENTS_AT_ONCE; i++) {
        	if (!read_from_client(mem)) {
        		break;
        	}
        }
    }

    return initialize_game(mem);
}

// Main structure of one game.
void make_turns(memory_server_t &mem) {
	timeval tv {};
	
    while (true) {
        gettimeofday(&tv, nullptr);
        uint64_t t = tv.tv_sec * 1000000 + tv.tv_usec;

        while (mem.next_message <= t) {
            mem.next_message += mem.turn_span.it_value.tv_nsec / 1000;
            
            if (make_moves(mem)) {
	            return;
	        }
        }

        disconnect_timeout(mem);
        
        for (uint32_t i = 0; i < CLIENTS_AT_ONCE; i++) {
        	if (!read_from_client(mem)) {
        		break;
        	}
        }
    }
}

// Main structure of program flow.
void play(memory_server_t &mem) {
    while (true) {
        if (start_game(mem)) {
            continue;
        }

        make_turns(mem);
    }
}

int main(int argc, char *argv[]) {
    memory_server_t mem {};
    
    update_options(mem, argc, argv);
    create_socket(mem);
    set_timers(mem);
    
    play(mem);
}
