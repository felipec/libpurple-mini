CC := $(CROSS_COMPILE)gcc

PLATFORM := $(shell uname -s)

GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)

LIBXML_CFLAGS := $(shell pkg-config --cflags libxml-2.0)
LIBXML_LIBS := $(shell pkg-config --libs libxml-2.0)

CFLAGS := -O2 -ggdb -Wall
LDFLAGS := -Wl,--no-undefined

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
	  dnssrv.o \
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
deps := $(patsubst %.o,%.d,$(objects))

SHLIBEXT := so
override CFLAGS += -fPIC
LIBS := -lresolv

target := libpurple.$(SHLIBEXT)

.PHONY: all clean

all: $(target)

# pretty print
V = @
Q = $(V:y=)
QUIET_CC    = $(Q:@=@echo '   CC         '$@;)
QUIET_LINK  = $(Q:@=@echo '   LINK       '$@;)
QUIET_CLEAN = $(Q:@=@echo '   CLEAN      '$@;)

version := 2.5.1

$(target): $(objects)
$(target): CFLAGS := $(CFLAGS) $(GOBJECT_CFLAGS) $(LIBXML_CFLAGS) \
	-D VERSION='"$(version)"' -D DISPLAY_VERSION='"$(version)"'
$(target): LIBS := $(LIBS) $(GOBJECT_LIBS) $(LIBXML_LIBS)


%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.so %.dll::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

clean:
	$(QUIET_CLEAN)$(RM) $(target) $(objects) $(deps)

-include $(deps)
