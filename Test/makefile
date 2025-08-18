.PHONY: a.out

a.out: request.cc
	rm -rf a.out
	g++ $^ -o $@ -std=c++11
.PHONY:clean
clean:
	rm -rf a.out