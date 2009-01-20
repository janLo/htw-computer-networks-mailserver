CFLAGS = -Wall -g
LDFLAGS = -lsqlite3 `pkg-config --libs-only-l openssl` -lresolv

OBJS = mailbox.o main.o config.o connection.o fail.o smtp.o forward.o pop3.o ssl.o
BIN  = mailtool

REVISION = `svn info *.c *.h | awk '$$1 ~ "Revision" {print $$2}' | sort -n | tail -n1`
CFLAGS += -D"REVISION_MAIN=\"$(REVISION)\""
#CFLAGS += -D"REVISION_HTW=\"$(REVISION)\""

$(BIN): $(OBJS)
	gcc $(LDFLAGS) -o $(BIN) $(OBJS)

clean: 
	rm -f $(BIN) $(OBJS)

include deps
