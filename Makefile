CFLAGS = -Wall -g
LDFLAGS = -lsqlite3 `pkg-config --libs-only-l openssl`

OBJS = mailbox.o main.o config.o connection.o fail.o smtp.o
BIN  = mailtool

$(BIN): $(OBJS)
	gcc $(LDFLAGS) -o $(BIN) $(OBJS)

clean: 
	rm -f $(BIN) $(OBJS)
