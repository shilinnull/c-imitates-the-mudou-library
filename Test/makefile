.PHONY: a.out

a.out: timewheel.cc
	rm -rf a.out
	g++ $^ -o $@ -std=c++11
.PHONY:clean
clean:
	rm -rf a.out