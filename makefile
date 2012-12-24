SRCDIR:=src
TSTDIR:=src/test
BINDIR:=bin
OBJDIR:=bin/obj
SRCSUBDIRS:=$(notdir $(patsubst %/,%,$(filter %/,$(wildcard $(SRCDIR)/*/))))
GTESTLIBS:=-lgtest -lgtest_main
POCODIR:=deps/poco/install
GFLAGSDIR:=deps/gflags
GLOGDIR:=deps/glog
CXX:=g++ -std=c++0x -I$(POCODIR)/include -I$(GFLAGSDIR)/src -I$(GLOGDIR)/src
CFLAGS:=-Wall -O3
LIBS:=-Llibs -L$(POCODIR)/lib\
	-lpythia-net -lpythia-nlp\
	$(GLOGDIR)/.libs/libglog.a $(GFLAGSDIR)/.libs/libgflags.a\
	-lPocoNetSSL -lPocoCrypto -lPocoNet -lPocoUtil -lPocoXML -lPocoFoundation\
	-lpthread -lrt -lboost_system-mt -lssl
TSTFLAGS:=-O0 -Wall -g
TSTLIBS:=$(GTESTLIBS) $(LIBS)
BINS:=pythia
INTLIBS:=$(addprefix libpythia-, $(SRCSUBDIRS))

TSTBINS:=$(notdir $(basename $(wildcard $(TSTDIR)/*.cc)))
TSTOBJS:=$(addsuffix .o, $(notdir $(basename $(wildcard $(TSTDIR)/*.cc))))
OBJS:=$(notdir $(basename $(wildcard $(SRCDIR)/*.cc)))
OBJS:=$(addsuffix .o, $(filter-out $(BINS), $(OBJS)))
OBJS:=$(addprefix $(OBJDIR)/, $(OBJS))
BINS:=$(addprefix $(BINDIR)/, $(BINS))
TSTBINS:=$(addprefix $(BINDIR)/, $(TSTBINS))

all: libs compile
	@echo "compiled all"

compile: makedirs $(BINS)
	@echo "compiled pythia"

libs: makedirs $(INTLIBS)
	@echo "compiled libs"

profile: CFLAGS=-Wall -O3 -DPROFILE
profile: LIBS+=-lprofiler
profile: clean compile

opt: CFLAGS=-Ofast -flto -mtune=native -DNDEBUG
opt: clean all

debug: CFLAGS=-O0 -g
debug: clean all

depend: senna poco gflags glog cpplint
	@echo "compiled all dependencies"

makedirs:
	@mkdir -p libs
	@mkdir -p bin/obj

senna:
	@if [ ! -d deps/senna ]; then \
		cd deps; \
		if [ ! -f senna-v3.0.tgz ]; then \
			wget http://ml.nec-labs.com/senna/senna-v3.0.tgz; \
		fi; \
		tar xf senna-v3.0.tgz; \
		sed -i "s/_new()/_new(const char* path, const char* subpath)/g" senna/*.h; \
		cd ..; \
	fi
	@cd $(OBJDIR); gcc -c -O3 -ffast-math ../../deps/senna/*.c;

poco:
	@git submodule init;
	@git submodule update;
	@cd deps/poco/;\
		./configure --omit=Data/ODBC,Data/MySQL --prefix=install --no-tests\
		--no-samples --static;\
		make; make install;
	@echo "compiled poco"

gflags:
	@tar xf deps/gflags.tar.gz -C deps/;
	@cd deps/gflags/; ./configure; make;
	@echo "compiled gflags"

glog:
	@tar xf deps/glog.tar.gz -C deps/;
	@cd deps/glog/; ./configure; make;
	@echo "compiled glog"

cpplint:
	@git submodule init;
	@git submodule update;

check: makedirs $(TSTBINS)
	@for t in $(TSTBINS); do ./$$t; done
	@echo "completed tests"

checkstyle:
	@python tools/cpplint/cpplint.py \
		--filter=-readability/streams,-readability/multiline_string\
		$(SRCDIR)/*.h $(SRCDIR)/*.cc $(SRCDIR)/*/*.h $(SRCDIR)/*/*.cc

clean:
	@rm -f $(OBJDIR)/*.o
	@rm -f libs/*.a
	@rm -f $(BINS)
	@rm -f $(TSTBINS)
	@echo "cleaned"

.PRECIOUS: $(OBJS) $(TSTOBJS)
.PHONY: libs all compile profile opt depend makedirs poco gflags glog check\
	cpplint checkstyle clean senna

libpythia-nlp: senna $(SRCDIR)/nlp/*.cc
	$(eval LIBFILES:=$(notdir $(basename $(wildcard $(SRCDIR)/nlp/*.cc))))
	$(eval LIBOBJS:=$(wildcard $(OBJDIR)/SENNA*.o) $(addprefix $(OBJDIR)/$(@F)-, $(addsuffix .o, $(LIBFILES))))
	@for i in $(LIBFILES); do \
	  $(CXX) $(CFLAGS) -o $(OBJDIR)/$(@F)-$$i.o -c $(SRCDIR)/nlp/$$i.cc; \
	done;
	@ar rs libs/$(@F).a $(LIBOBJS) 2>/dev/null
	@echo "compiled libs/$(@F).a"

libpythia-%: $(SRCDIR)/%/*.cc
	$(eval LIBFILES:=$(notdir $(basename $(wildcard $(SRCDIR)/$*/*.cc))))
	$(eval LIBOBJS:=$(addprefix $(OBJDIR)/$(@F)-, $(addsuffix .o, $(LIBFILES))))
	@for i in $(LIBFILES); do \
	  $(CXX) $(CFLAGS) -o $(OBJDIR)/$(@F)-$$i.o -c $(SRCDIR)/$*/$$i.cc; \
	done;
	@ar rs libs/$(@F).a $(LIBOBJS) 2>/dev/null
	@echo "compiled libs/$(@F).a"

$(BINDIR)/%: $(OBJS) $(SRCDIR)/%.cc
	@$(CXX) $(CFLAGS) -o $(OBJDIR)/$(@F).o -c $(SRCDIR)/$(@F).cc
	@$(CXX) $(CFLAGS) -o $(BINDIR)/$(@F) $(OBJDIR)/$(@F).o $(OBJS) $(LIBS)
	@echo "compiled $(BINDIR)/$(@F)"

$(OBJDIR)/%.o: $(SRCDIR)/%.cc $(SRCDIR)/%.h
	@$(CXX) $(CFLAGS) -o $(OBJDIR)/$(@F) -c $<

$(BINDIR)/%-test: $(OBJS) $(TSTDIR)/*.cc
	@$(CXX) $(CFLAGS) -o $(OBJDIR)/$(@F).o -c $(SRCDIR)/test/$(@F).cc
	@$(CXX) $(CFLAGS) -o $(BINDIR)/$(@F) $(OBJDIR)/$(@F).o $(OBJS) $(TSTLIBS)
	@echo "compiled $(BINDIR)/$(@F)"

$(OBJDIR)/%-test.o: $(TSTDIR)/%.cc
	@$(CXX) $(CFLAGS) -o $(OBJDIR)/$(@F) -c $<
