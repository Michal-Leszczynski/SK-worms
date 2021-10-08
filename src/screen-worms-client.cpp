#include "screen-worms-client.h"
#include "common.h"

// Creates socket to given address and port. Socket is set not to block
// and (in terms of TCP) Nagle's algorithm is disabled.
// Socket is also connected in order to use read and write.
void create_socket(const char *ip, const char *port, int &sock, bool UDP) {
    addrinfo addr_hints {};
    addrinfo *addr_result;
    
    memset(&addr_hints, 0, sizeof(addrinfo));
    addr_hints.ai_family = AF_UNSPEC;
    addr_hints.ai_flags = 0;
    addr_hints.ai_addrlen = 0;
    addr_hints.ai_addr = nullptr;
    addr_hints.ai_canonname = nullptr;
    addr_hints.ai_next = nullptr;

    if (UDP) {
        addr_hints.ai_socktype = SOCK_DGRAM;
        addr_hints.ai_protocol = IPPROTO_UDP;
    }
    else {
        addr_hints.ai_socktype = SOCK_STREAM;
        addr_hints.ai_protocol = IPPROTO_TCP;
    }

    if (getaddrinfo(ip, port, &addr_hints, &addr_result) != 0) {
        cout<<"addr info"<<endl;
        exit(1);
    }

    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);

    if (sock < 0) {
        cout<<"socket"<<endl;
        exit(1);
    }

    struct timeval tv {};
    tv.tv_sec = 0;
    tv.tv_usec = RCV_WAIT;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) < 0) {
        cout<<"socket options"<<endl;
        exit(1);
    }

    if (!UDP) {
        int nagle = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nagle ,sizeof(nagle)) < 0) {
            cout<<"nagle"<<endl;
            exit(1);
        }
    }

    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0) {
        cout<<"connect"<<endl;
        exit(1);
    }

    freeaddrinfo(addr_result);
}


// Updates options taken from command line. First argument from command line
// is taken for server's address.
void update_options(memory_client_t &mem, int argc, char *argv[]) {
    string server_port = "2021";
    string gui_port = "20210";
    string gui_ip = "localhost";
    int opt;
	
	if (argc < 2) {
		cout<<"missing server address"<<endl;
		exit(1);
	}
	
    while ((opt = getopt(argc - 1, &argv[1], "n:p:i:r:")) != -1) {
        if (opt == '?') {
            cout<<"unknown option"<<endl;
            exit(1);
        }

        if (opt == 'n') {
            mem.name = optarg;

            if (mem.name.size() > PLAYER_NAME_LENGTH) {
                cout<<"player name is too long"<<endl;
                exit(1);
            }

            for (char i : mem.name) {
                if (i < PLAYER_MIN_CHAR || i > PLAYER_MAX_CHAR) {
                    cout<<"incorrect player name"<<endl;
                    exit(1);
                }
            }
        }
        else if (opt == 'p') {
            server_port = optarg;
        }
        else if (opt == 'r') {
            gui_port = optarg;
        }
        else if (opt == 'i') {
            gui_ip = optarg;
        }
    }
	
    create_socket(argv[1], server_port.c_str(), mem.server_sock, true);
    create_socket(gui_ip.c_str(), gui_port.c_str(), mem.gui_sock, false);
}

// Checks whether given string is prefix of buffer.
bool check_for_string(const uint8_t *buff, string &str) {
    for (size_t i = 0; i < str.size(); i++) {
        if (buff[i] != str[i]) {
            return false;
        }
    }

    return true;
}

// Updates direction to latest arrow action.
void update_direction(memory_client_t &mem, int arrow) {
    if (arrow == LD) {
        mem.direction = LEFT;
    }
    else if (arrow == RD) {
        mem.direction = RIGHT;
    }
    else if ((arrow == LU && mem.direction == LEFT) ||
            (arrow == RU && mem.direction == RIGHT)) {
        mem.direction = STRAIGHT;
    }
}

// Reads grom gui to buffer untill new line character is encountered. 
void read_until_new_line(memory_client_t &mem, uint8_t *buff, int &len) {
    while (true) {
        int mess_len = read(mem.gui_sock, buff + len, BYTE);

        if (mess_len <= 0) {
            continue;
        }
        len += mess_len;

        if (buff[len - 1] == '\n') {
            break;
        }
    }
}

// Tries to read from gui. In case of error or no message
// ignores the read.
void read_from_gui(memory_client_t &mem) {
    static uint8_t buff[MAX_TCP] {};
    static string ld = "LEFT_KEY_DOWN";
    static string lu = "LEFT_KEY_UP";
    static string rd = "RIGHT_KEY_DOWN";
    static string ru = "RIGHT_KEY_UP";

    int arrow;
    int len = read(mem.gui_sock, buff, lu.size());

    if (len <= 0) {
        return;
    }
	
    read_until_new_line(mem, buff, len);

    if (check_for_string(buff, ld)) {
        arrow = LD;
    }
    else if (check_for_string(buff, lu)) {
        arrow = LU;
    }
    else if (check_for_string(buff, rd)) {
        arrow = RD;
    }
    else if (check_for_string(buff, ru)) {
        arrow = RU;
    }
    else {
        cout<<"gui error"<<endl;
        exit(1);
    }

    update_direction(mem, arrow);
}

// Converts given amoutn of bytes from buffer (with offset) from Big Endian to Host.
// Updates offset of the amount of converted bytes.
uint64_t convert_bytes_to_number(const uint8_t *buff, uint64_t &offset, int bytes) {
    uint64_t result = 0;

    for (int i = 0; i < bytes; i++) {
        result *= BYTE_RANGE;
        result += (buff + offset)[i];
    }

    offset += bytes;
    return result;
}

// Checks crc from data of length len.
bool check_crc(uint8_t *data, uint64_t len) {
    uint32_t crc = 0xFFFFFFFF;

    for (uint64_t i = 0; i < len; i++) {
        uint64_t temp = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_tab[temp];
    }

    crc = crc ^ 0xFFFFFFFF;

    uint32_t mess_crc = convert_bytes_to_number(data, len, DWORD);
    return (crc == mess_crc);
}

// Returns parsed PIXEL event for gui. Moves offset to the next event.
string parse_PIXEL(memory_client_t &mem, uint8_t *buff, uint64_t &offset) {
    uint8_t player = convert_bytes_to_number(buff, offset, BYTE);
    uint32_t x = convert_bytes_to_number(buff, offset, DWORD);
    uint32_t y = convert_bytes_to_number(buff, offset, DWORD);
	
    if (player < mem.players_cnt && x < mem.max_x && y < mem.max_y) {
        offset += DWORD;
        return "PIXEL " + to_string(x) + " " + to_string(y) + " " + mem.player_names[player] + "\n";
    }

    cout<<"wrong pixel event"<<endl;
    exit(1);
}

// Returns parsed PLAYER ELIMINATED event for gui. Moves offset to the next event.
string parse_ELIMINATED(memory_client_t &mem, uint8_t *buff, uint64_t &offset) {
    uint8_t player = convert_bytes_to_number(buff, offset, BYTE);

    if (player < mem.players_cnt) {
        offset += DWORD;
        return "PLAYER_ELIMINATED " + mem.player_names[player] + "\n";
    }

    cout<<"wrong player eliminated event"<<endl;
    exit(1);
}

// Returns parsed player name. Moves offset to the next name.
string parse_player_name(uint8_t *buff, uint64_t &offset) {
    string str;

    for (int i = 0; ; i++) {
        if ((buff + offset)[0] == '\0') {
            offset++;
            return str;
        }

        if (i == PLAYER_NAME_LENGTH || (buff + offset)[0] < PLAYER_MIN_CHAR ||
            (buff + offset)[0] > PLAYER_MAX_CHAR) {
            cout<<"incorrect name"<<endl;
            exit(1);
        }

        str += (buff + offset)[0];
        offset++;
    }
}

// Returns parsed NEW GAME event for gui. Moves offset to the next event.
string parse_NEW_GAME(memory_client_t &mem, uint8_t *buff, uint64_t &offset, uint64_t end_of_list) {
    uint32_t max_x = convert_bytes_to_number(buff, offset, DWORD);
    uint32_t max_y = convert_bytes_to_number(buff, offset, DWORD);
    int player_cnt = 0;
    string str = "NEW_GAME " + to_string(max_x) + " " + to_string(max_y);
	
    while (offset < end_of_list) {
        mem.player_names[player_cnt] = parse_player_name(buff, offset);
        str += " " + mem.player_names[player_cnt];
        player_cnt++;
		
        if (player_cnt > MAX_PLAYERS) {
            cout<<"too many players"<<endl;
            exit(1);
        }
    }

    offset += DWORD;
    mem.players_cnt = player_cnt;
    mem.max_x = max_x;
    mem.max_y = max_y;
    str += "\n";
    
    return str;
}

// Parses one whole event for gui. Uses arithmetics for server messages.
string parse_event(memory_client_t &mem, uint8_t* buff, uint64_t &offset, uint64_t size) {
    if (size < EVENT_BASIC_LENGTH) {
        return "ignore";
    }
	
	uint32_t len = convert_bytes_to_number(buff, offset, DWORD);
    	
	if (len < DWORD + BYTE || len + 2 * DWORD > size) {
    	return "ignore";
	}
	
    if (check_crc(buff + offset - DWORD, len + DWORD)) {
        uint32_t event_no = convert_bytes_to_number(buff, offset, DWORD);
        uint8_t event_type = convert_bytes_to_number(buff, offset, BYTE);
		
        if (event_no != mem.next_event_no) {
            return "ignore";
        }

        mem.next_event_no++;

        if (event_type == NEW_GAME_TYPE) {           
            if (buff[offset + len - DWORD - 2 * BYTE] != '\0') {
                cout<<"incorrect player list"<<endl;
                exit(1);
            }

            return parse_NEW_GAME(mem, buff, offset, offset + len - DWORD - BYTE);
        }
        else if (event_type == PIXEL_TYPE && len == PIXEL_LENGTH) {
            return parse_PIXEL(mem, buff, offset);
        }
        else if (event_type == ELIMINATED_TYPE && len == ELIMINATED_LENGTH) {
            return parse_ELIMINATED(mem, buff, offset);
        }
        else if (event_type == GAME_OVER_TYPE && len == GAME_OVER_LENGTH) {
            offset += DWORD;
            
            return "game over";
        }
        else {
            offset += len + DWORD - BYTE;
            
            return "type";
        }
    }
    else {
        return "ignore";
    }
}

// Reads one whole message from server. Returns true if we were able to read any bytes.
bool read_from_server(memory_client_t &mem) {
    static uint8_t buff[MAX_UDP];

    uint64_t offset = 0;
    memset(buff, 0, sizeof(buff));
    int size = read(mem.server_sock, buff, sizeof(buff));
	
    if (size <= 0) {
    	return false;
    }
    else if (size < DWORD * DWORD) {
        return true;
    }
	
    uint32_t game_id = convert_bytes_to_number(buff, offset, DWORD);

    if (buff[3 * DWORD] == NEW_GAME_TYPE && game_id != mem.game_id) {  
        mem.game_id = game_id;
        mem.next_event_no = 0;
    }

    if (mem.game_id != game_id) {
        return true;
    }

    while (offset < static_cast<uint64_t>(size)) {
        string result = parse_event(mem, buff, offset, size);
        auto *mess = reinterpret_cast<uint8_t*>(&result[0]);

        if (result == "ignore") {
            return true;
        }
        else if (result == "type") {
            continue;
        }
        else if (result != "game over") {
            if (write(mem.gui_sock, mess, result.size()) == -1) {
                cout<<"write to gui"<<endl;
                exit(1);
            }
        }
    }
    
    return true;
}

// Sends one message with current arrow pressed to server.
void send_to_server(memory_client_t &mem) {
    client_mess_t mess {};
    mess.session_id = htobe64(mem.session_id);
    mess.turn_direction = mem.direction;
    mess.next_expected_event_no = htobe32(mem.next_event_no);
    memcpy(mess.player_name, &mem.name[0], mem.name.size());
	
    if (write(mem.server_sock, &mess, MESS_BASIC_LEN + mem.name.size()) == -1) {
        cout<<"write server"<<endl;
        exit(1);
    }
}

// Main structure of program flow.
void play(memory_client_t &mem) {
    timeval tv {};

    while (true) {
        gettimeofday(&tv, nullptr);
        uint64_t t = tv.tv_sec * 1000000 + tv.tv_usec;

        while (mem.next_message <= t) {
            send_to_server(mem);
            mem.next_message += MESSAGE_SPAN;
        }
        
		read_from_gui(mem);
		
        for (int i = 0; i < SERVER_AT_ONCE; i++) {
        	if (!read_from_server(mem)) {
        		break;
        	}
        }
    }
}

int main(int argc, char *argv[]) {
    memory_client_t mem {};
    timeval tv {};
    
    gettimeofday(&tv, nullptr);
    
    mem.session_id = tv.tv_sec * 1000000 + tv.tv_usec;
	mem.next_message = mem.session_id + MESSAGE_SPAN;
	
    update_options(mem, argc, argv);
    
    play(mem);
}
