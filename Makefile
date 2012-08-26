HAMAMCE_OBJS = hama_mce.o
HAMAMCE_EXE = xbmc_hama_mce
LIBS = -lusb-1.0 -lrt

ifeq ($(strip $(DEBUG)),Y)
  CXXFLAGS += -g -DDEBUG
else
  ifndef CXXFLAGS
    CXXFLAGS += -O2
  endif
  LDFLAGS += -s
endif

CXXFLAGS += -Wall -Werror -pipe -ansi

all : $(HAMAMCE_EXE)

update:
	install -m755 $(HAMAMCE_EXE) $(DESTDIR)/usr/bin

install:
	install -Dm755 $(HAMAMCE_EXE) $(DESTDIR)/usr/bin/$(HAMAMCE_EXE)
	install -Dm644 hama_mce.rules $(DESTDIR)/etc/udev/rules.d/99_hama_mce.rules
	install -Dm644 hama_mce_pm $(DESTDIR)/etc/pm/sleep.d/99_hama_mce
	install -Dm644 hama_mce.fdi $(DESTDIR)/etc/hal/fdi/preprobe/99_hama_mce.fdi
	install -Dm644 remote.xml $(DESTDIR)/usr/share/xbmc/system/keymaps/remote.Hama.MCE.xml

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(HAMAMCE_EXE)
	rm -f $(DESTDIR)/etc/udev/rules.d/hama_mce.rules
	rm -f $(DESTDIR)/etc/pm/sleep.d/99_hama_mce
	rm -f $(DESTDIR)/etc/hal/fdi/preprobe/99_hama_mce.fdi
	rm -f $(DESTDIR)/usr/share/xbmc/system/keymaps/remote.Hama.MCE.xml
	
$(HAMAMCE_EXE) : $(HAMAMCE_OBJS)
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY : clean
clean :
	-rm $(HAMAMCE_EXE) $(HAMAMCE_OBJS)

