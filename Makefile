CC := $(CROSS_COMPILE)gcc

PLATFORM := $(shell uname -s)

GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)

LIBXML_CFLAGS := $(shell pkg-config --cflags libxml-2.0)
LIBXML_LIBS := $(shell pkg-config --libs libxml-2.0)

CFLAGS := -O2 -ggdb -Wall
LDFLAGS := -Wl,--no-undefined

prefix := /usr
install_dir := $(DESTDIR)/$(prefix)

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

headers := account.h \
	   accountopt.h \
	   blist.h \
	   buddyicon.h \
	   certificate.h \
	   cipher.h \
	   circbuffer.h \
	   cmds.h \
	   connection.h \
	   conversation.h \
	   core.h \
	   dbus-maybe.h \
	   debug.h \
	   desktopitem.h \
	   eventloop.h \
	   ft.h \
	   gaim-compat.h \
	   idle.h \
	   imgstore.h \
	   log.h \
	   mime.h \
	   nat-pmp.h \
	   network.h \
	   notify.h \
	   ntlm.h \
	   plugin.h \
	   pluginpref.h \
	   pounce.h \
	   prefs.h \
	   privacy.h \
	   proxy.h \
	   prpl.h \
	   request.h \
	   roomlist.h \
	   savedstatuses.h \
	   server.h \
	   signals.h \
	   smiley.h \
	   dnsquery.h \
	   dnssrv.h \
	   status.h \
	   stringref.h \
	   stun.h \
	   sound.h \
	   sslconn.h \
	   upnp.h \
	   util.h \
	   value.h \
	   version.h \
	   xmlnode.h \
	   whiteboard.h

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

purple.pc: purple.pc.in
	sed -e 's#@prefix@#$(prefix)#g' $@.in > $@

install: $(target) purple.pc
	mkdir -p $(install_dir)/lib/pkgconfig
	mkdir -p $(install_dir)/include/libpurple
	install -m 644 $(target) $(install_dir)/lib/$(target).0
	ln -sf $(target).0 $(install_dir)/lib/$(target)
	install -m 644 purple.pc $(install_dir)/lib/pkgconfig
	install -m 644 $(headers) $(install_dir)/include/libpurple/
	install -m 644 purple-client.h $(install_dir)/include/libpurple/purple.h

%.o:: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -MMD -o $@ -c $<

%.so %.dll::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

clean:
	$(QUIET_CLEAN)$(RM) $(target) $(objects) $(deps)

-include $(deps)
