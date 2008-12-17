CFLAGS = -Wall -g
LDFLAGS = -lsqlite3

OBJS = mailbox.o main.o config.o connection.o fail.o
BIN  = mailtool

$(BIN): $(OBJS)
	gcc $(LDFLAGS) -o $(BIN) $(OBJS)

clean: 
	rm -f $(BIN) $(OBJS)
