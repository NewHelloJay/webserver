OBJS = ./tool/*.cpp ./heaptimer/*.cpp ./http/*.cpp ./main.cpp

all:$(OBJS)
	g++ $(OBJS) -o server -pthread -lmysqlclient