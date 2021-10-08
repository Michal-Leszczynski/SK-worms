.PHONY: screen-worms clean

screen-worms:
	g++ -Wall -Wextra -O2 -std=c++17 -o screen-worms-server screen-worms-server.h common.h screen-worms-server.cpp
	g++ -Wall -Wextra -O2 -std=c++17 -o screen-worms-client screen-worms-client.h common.h screen-worms-client.cpp

clean:
	rm -f *.o screen-worms-server
	rm -f *.o screen-worms-client
