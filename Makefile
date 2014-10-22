all: stable.so test

stable.so: stable.c
	clang -g -Wall -undefined dynamic_lookup -o $@ $^

test: test.c stable.c
	clang -g -Wall -o $@ $^ -L/usr/local/lib -llua

.PHONY : clean

clean: .
		rm -rf stable.so test
