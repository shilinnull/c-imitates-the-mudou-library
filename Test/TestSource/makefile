.PHONY: server client clean
all: server client
server: server.cc
	rm -f $@
	g++ $^ -o $@ -std=c++17 -lpthread
client: client.cc
	rm -f $@
	g++ $^ -o $@ -std=c++17 -lpthread
clean:
	rm -f server client