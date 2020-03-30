
build: socketproxy.o
	g++ -Wall -c socketproxy.cpp -o socketproxy.o
	g++ -levent socketproxy.o -o socketproxy
