CFLAGS = -Wall -g
LDFLAGS = -lsqlite3 `pkg-config --libs-only-l openssl` -lresolv

OBJS = mailbox.o main.o config.o connection.o fail.o smtp.o forward.o
BIN  = mailtool

$(BIN): $(OBJS)
	gcc $(LDFLAGS) -o $(BIN) $(OBJS)

clean: 
	rm -f $(BIN) $(OBJS)
