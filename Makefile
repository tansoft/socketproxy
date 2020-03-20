
build: socketproxy.o
	g++ -Wall -c socketproxy.cpp -o socketproxy.o
	g++ -levent -Wl socketproxy.o -o socketproxy
