CC := $(CROSS_COMPILE)gcc

PLATFORM := $(shell $(CC) -dumpmachine | cut -f 3 -d -)

GOBJECT_CFLAGS := $(shell pkg-config --cflags gobject-2.0)
GOBJECT_LIBS := $(shell pkg-config --libs gobject-2.0)

LIBXML_CFLAGS := $(shell pkg-config --cflags libxml-2.0)
LIBXML_LIBS := $(shell pkg-config --libs libxml-2.0)

ifeq ($(PLATFORM),mingw32)
GTHREAD_CFLAGS := $(shell pkg-config --cflags gthread-2.0)
GTHREAD_LIBS := $(shell pkg-config --libs gthread-2.0)
endif

CFLAGS := -O2 -ggdb -Wall
LDFLAGS := -Wl,--no-undefined

prefix := /usr
install_dir := $(DESTDIR)/$(prefix)

datadir := /usr/share
libdir := /usr/lib
sysconfdir := /etc
ssl_dir := /usr/share

ifneq ($(PLATFORM),mingw32)
override CFLAGS += -DBR_PTHREADS=0 \
	-DDATADIR=\"$(datadir)\" \
	-DLIBDIR=\"$(libdir)/purple-1.0/\" \
	-DLOCALEDIR=\"$(datadir)/locale\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DSSL_CERTIFICATES_DIR=\"$(ssl_dir)\"
endif

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
	  eventloop.o \
	  ft.o \
	  idle.o \
	  imgstore.o \
	  log.o \
	  media.o \
	  mediamanager.o \
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
	  sound-theme.o \
	  sound-theme-loader.o \
	  sslconn.o \
	  theme.o \
	  theme-loader.o \
	  theme-manager.o \
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
	   media.h \
	   media-gst.h \
	   mediamanager.h \
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
	   sound-theme.h \
	   sound-theme-loader.h \
	   sslconn.h \
	   upnp.h \
	   util.h \
	   value.h \
	   version.h \
	   valgrind.h \
	   xmlnode.h \
	   whiteboard.h

ifeq ($(PLATFORM),mingw32)
objects += win32/giowin32.o \
	   win32/libc_interface.o \
	   win32/win32dep.o
else
objects += desktopitem.o
endif

sources := $(patsubst %.o,%.c,$(objects))
deps := $(patsubst %.o,%.d,$(objects))

ifeq ($(PLATFORM),mingw32)
  SHLIBEXT := dll
  override CFLAGS += -I. -I./win32
  LIBS := -lintl -lws2_32 $(GTHREAD_LIBS)
else
  SHLIBEXT := so
  override CFLAGS += -fPIC
  LIBS := -lresolv
endif

target := libpurple.$(SHLIBEXT)

.PHONY: all clean

all: $(target)

# pretty print
V = @
Q = $(V:y=)
QUIET_CC    = $(Q:@=@echo '   CC         '$@;)
QUIET_LINK  = $(Q:@=@echo '   LINK       '$@;)
QUIET_CLEAN = $(Q:@=@echo '   CLEAN      '$@;)

version := 2.6.0

$(target): $(objects)
$(target): CFLAGS := $(CFLAGS) $(GOBJECT_CFLAGS) $(LIBXML_CFLAGS) \
	-D VERSION='"$(version)"' -D DISPLAY_VERSION='"$(version)"'
$(target): LIBS := $(LIBS) $(GOBJECT_LIBS) $(LIBXML_LIBS) -lm

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
