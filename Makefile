ifndef PARAGRAPHER_LIB_FOLDER
	PARAGRAPHER_LIB_FOLDER := $(shell realpath .)/lib64
endif 

GCC := gcc
GXX := g++
LIB := $(LD_LIBRARY_PATH)
SHELL := /bin/bash

INCLUDE_LIBS := $(addprefix -L , $(subst :, ,$(LIB)))
INCLUDE_HEADER := $(addprefix -I , $(subst :,/../include ,$(LIB)))
FLAGS :=  -Wfatal-errors -lm -lpthread -lrt

JAVA_CLASS_FILES  := $(addprefix $(PARAGRAPHER_LIB_FOLDER)/,$(subst src/,,$(subst .java,.class,$(shell ls src/*.java))))

ifdef debug
	COMPILE_TYPE := -g
else
	COMPILE_TYPE := -O3 -DNDEBUG
endif

all: $(PARAGRAPHER_LIB_FOLDER)/libparagrapher.so JLIBS $(JAVA_CLASS_FILES) 

# Check if libfuse is accessible to enable PG_FUSE
$(shell printf '%s\n' '#define FUSE_USE_VERSION 31' '#if __has_include(<fuse.h>)' '#include <fuse.h>' '#else' '#include <fuse3/fuse.h>' '#endif' 'int main(){ (void)fuse_version(); return 0; }' > /tmp/.fuse_check.c)
CAN_USE_LIB_FUSE := $(shell if $(GCC) $(INCLUDE_HEADER) $(INCLUDE_LIBS) /tmp/.fuse_check.c -lfuse3 -o /tmp/.fuse_check  2>/dev/null; then echo 1; else echo 0; fi )
$(info CAN_USE_LIB_FUSE = $(CAN_USE_LIB_FUSE))
ifeq ($(CAN_USE_LIB_FUSE),1)
  $(info libfuse is accessible)
  all: $(all) $(PARAGRAPHER_LIB_FOLDER)/pg_fuse.o
else
  $(info libfuse is not accessible)
endif
$(shell rm /tmp/.fuse_check*)
FLAGS := $(FLAGS) -DCAN_USE_LIB_FUSE=$(CAN_USE_LIB_FUSE)

$(PARAGRAPHER_LIB_FOLDER)/libparagrapher.so: src/* include/* Makefile
	@if [ `$(GCC) -dumpversion | cut -f1 -d.` -le 8 ]; then\
		$(GCC) -dumpversion; \
		echo -e "\033[0;33mError:\033[0;37m Version 9 or newer is required for gcc.\n\n";\
		exit -1;\
	fi

	@echo -e "\n\033[1;32mPARAGRAPHER_LIB_FOLDER: "$(PARAGRAPHER_LIB_FOLDER)"\033[0;37m"
	@echo -e "\033[1;34mCompiling ParaGrapher\033[0;37m"
	mkdir -p $(PARAGRAPHER_LIB_FOLDER)
	$(GCC) $(INCLUDE_HEADER) $(INCLUDE_LIBS) $(FLAGS) $(COMPILE_TYPE) -fpic -shared -std=gnu11 src/paragrapher.c -o $(PARAGRAPHER_LIB_FOLDER)/libparagrapher.so
	@echo ""

JLIBS: FORCE 
	@if [ `javac  -version 2>&1 | cut -f2 -d' ' | cut -f1 -d.` -le 14 ]; then\
		javac  -version 2>&1;\
		echo -e "\033[0;33mError:\033[0;37m Version 15 or newer is required for javac.\n\n";\
		exit -1;\
	fi
	@if [ `java  -version 2>&1 | head -n1|cut -f2 -d\"|cut -f1 -d.` -le 14 ]; then\
		java  -version 2>&1;\
		echo -e "\033[0;33mError:\033[0;37m Version 15 or newer is required for java.\n\n";\
		exit -1;\
	fi
	@if [ ! -d $(PARAGRAPHER_LIB_FOLDER)/jlibs ]; then\
		wget -P $(PARAGRAPHER_LIB_FOLDER) "https://hpgp.net/download/jlibs.zip";\
		unzip $(PARAGRAPHER_LIB_FOLDER)/jlibs.zip -d $(PARAGRAPHER_LIB_FOLDER); \
		rm $(PARAGRAPHER_LIB_FOLDER)/jlibs.zip;\
		echo "Java libararies downloaded.";\
	fi

$(PARAGRAPHER_LIB_FOLDER)/%.class: src/%.java Makefile
	@echo -e "\033[1;34mCompiling $<\033[0;37m"
	javac -cp $(PARAGRAPHER_LIB_FOLDER)/jlibs/*:src: -d $(PARAGRAPHER_LIB_FOLDER) $<

$(PARAGRAPHER_LIB_FOLDER)/pg_fuse.o: src/pg_fuse.c Makefile
	@echo -e "\n\033[1;34mCompiling ParaGrapher FUSE (pg_fuse)\033[0;37m"
	$(GCC) $(INCLUDE_HEADER) $(INCLUDE_LIBS) -lnuma -lfuse3 $(FLAGS) $(COMPILE_TYPE) src/pg_fuse.c -o $(PARAGRAPHER_LIB_FOLDER)/pg_fuse.o

test: FORCE all
	@echo -e "\n\033[1;32mPARAGRAPHER_LIB_FOLDER: "$(PARAGRAPHER_LIB_FOLDER)"\033[0;37m"
	PARAGRAPHER_LIB_FOLDER=$(PARAGRAPHER_LIB_FOLDER) make -C test

test%: FORCE all
	@echo -e "\n\033[1;32mPARAGRAPHER_LIB_FOLDER: "$(PARAGRAPHER_LIB_FOLDER)"\033[0;37m"
	PARAGRAPHER_LIB_FOLDER=$(PARAGRAPHER_LIB_FOLDER) make -C test $@ 

download%: 
	make -C test $@
	
clean: unmount clean-shm-files
	rm -f $(PARAGRAPHER_LIB_FOLDER)/*.so $(PARAGRAPHER_LIB_FOLDER)/*.o $(PARAGRAPHER_LIB_FOLDER)/*.class 
	rm -rf /tmp/pg_fuse*
	touch src/* include/*
	make clean -C test

clean-shm-files: 
	rm -f  /dev/shm/paragrapher_*

unmount:
	for f in `findmnt -l | grep pg_fuse | cut -f1 -d' '`; do echo -e "\nUnmounting $$f"; fusermount -uz $$f; done
	findmnt -l | grep pg_fuse || echo
	ps -ef | grep pg_fuse || echo

touch:
	touch src/* include/*

FORCE: ;
