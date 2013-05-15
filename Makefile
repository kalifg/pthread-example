all:
	g++ ${CCFLAGS} thread-test.cpp -lpthread -o thread-test
