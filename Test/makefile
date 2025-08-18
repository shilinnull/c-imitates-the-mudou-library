.PHONY: a.out

a.out: any.cc
	rm -rf a.out
	g++ $^ -o $@ -std=c++17
.PHONY:clean
clean:
	rm -rf a.out