CC := gcc

PLATFORM := $(shell uname -s)

GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)

LIBXML_CFLAGS := $(shell pkg-config --cflags libxml-2.0)
LIBXML_LIBS := $(shell pkg-config --libs libxml-2.0)

ifdef DEBUG
  CFLAGS += -ggdb
else
  CFLAGS += -O2
endif

SIMPLE_WARNINGS := -Wextra -ansi -std=c99 -Wno-unused-parameter

CFLAGS += -Wall # $(SIMPLE_WARNINGS)

datadir := /usr/share
libdir := /usr/lib
sysconfdir := /etc
ssl_dir := /usr/share

override CFLAGS += -DBR_PTHREADS=0 \
	-DDATADIR=\"$(datadir)\" \
	-DLIBDIR=\"$(libdir)/purple-1.0/\" \
	-DLOCALEDIR=\"$(datadir)/locale\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DSSL_CERTIFICATES_DIR=\"$(ssl_dir)\"

LDFLAGS := -Wl,--no-undefined

prefix := $(DESTDIR)/$(PURPLE_PREFIX)
plugin_dir := $(prefix)/lib/purple-2
data_dir := $(prefix)/share

objects = account.o \
	  accountopt.o \
	  blist.o \
	  buddyicon.o \
	  certificate.o \
	  cipher.o \
	  circbuffer.o \
	  cmds.o \
	  connection.o \
	  conversation.o \
	  core.o \
	  debug.o \
	  desktopitem.o \
	  eventloop.o \
	  ft.o \
	  idle.o \
	  imgstore.o \
	  log.o \
	  mime.o \
	  nat-pmp.o \
	  network.o \
	  ntlm.o \
	  notify.o \
	  plugin.o \
	  pluginpref.o \
	  pounce.o \
	  prefs.o \
	  privacy.o \
	  proxy.o \
	  prpl.o \
	  request.o \
	  roomlist.o \
	  savedstatuses.o \
	  server.o \
	  signals.o \
	  smiley.o \
	  dnsquery.o \
	  dnssrv.o\
	  status.o \
	  stringref.o \
	  stun.o \
	  sound.o \
	  sslconn.o \
	  upnp.o \
	  util.o \
	  value.o \
	  version.o \
	  xmlnode.o \
	  whiteboard.o

sources := $(patsubst %.o,%.c,$(objects))

SHLIBEXT := so

target := libpurple.$(SHLIBEXT)
override CFLAGS += -fPIC

.PHONY: all clean

all: $(target)

# from Lauri Leukkunen's build system
ifdef V
Q = 
P = @printf "" # <- space before hash is important!!!
else
P = @printf "[%s] $@\n" # <- space before hash is important!!!
Q = @
endif

$(target): $(objects)
$(target): CFLAGS := $(CFLAGS) $(GOBJECT_CFLAGS) $(LIBXML_CFLAGS) -D VERSION='"2.5.1"'
$(target): LIBS := $(GOBJECT_LIBS) $(LIBXML_LIBS) -lresolv

%.so::
	$(P)SHLIB
	$(Q)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

%.a::
	$(P)ARCHIVE
	$(AR) rcs $@ $^

%.o:: %.c
	$(P)CC
	$(Q)$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(target) $(objects)
