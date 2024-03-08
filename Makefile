ifndef POPLAR_LIB_FOLDER
	POPLAR_LIB_FOLDER := $(shell realpath .)/lib64
endif 

GCC := gcc
GXX := g++
LIB := $(LD_LIBRARY_PATH)

UP_GCC_DIR := ~/gcc9.2
ifneq ("$(wildcard $(UP_GCC_DIR)/bin/gcc)","")
	GCC := $(UP_GCC_DIR)/bin/gcc
	GXX := $(UP_GCC_DIR)/bin/g++
	LIB := $(UP_GCC_DIR)/lib64:$(LIB)
endif

INCLUDE_LIBS := $(addprefix -L , $(subst :, ,$(LIB)))
INCLUDE_HEADER := $(addprefix -I , $(subst :,/../include ,$(LIB)))
FLAGS :=  -Wfatal-errors -lm -lpthread -lrt

JAVA_CLASS_FILES  := $(addprefix $(POPLAR_LIB_FOLDER)/,$(subst src/,,$(subst .java,.class,$(shell ls src/*.java))))

ifdef debug
	COMPILE_TYPE := -g
else
	COMPILE_TYPE := -O3 -DNDEBUG
endif

all: $(POPLAR_LIB_FOLDER)/libpoplar.so JLIBS $(JAVA_CLASS_FILES)

$(POPLAR_LIB_FOLDER)/libpoplar.so: src/* include/* Makefile
	@if [ `$(GCC) -dumpversion | cut -f1 -d.` -le 8 ]; then\
		$(GCC) -dumpversion; \
		echo -e "\033[0;33mError:\033[0;37m Version 9 or newer is required for gcc.\n\n";\
		exit -1;\
	fi

	@echo -e "\n\033[1;32mPOPLAR_LIB_FOLDER: "$(POPLAR_LIB_FOLDER)"\033[0;37m"
	@echo -e "\033[1;34mCompiling Poplar\033[0;37m"
	mkdir -p $(POPLAR_LIB_FOLDER)
	$(GCC) $(INCLUDE_LIBS) $(FLAGS) $(COMPILE_TYPE) -fpic -shared src/poplar.c -o $(POPLAR_LIB_FOLDER)/libpoplar.so
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
	@if [ ! -d $(POPLAR_LIB_FOLDER)/jlibs ]; then\
		wget -P $(POPLAR_LIB_FOLDER) "https://hpgp.net/download/jlibs.zip";\
		unzip $(POPLAR_LIB_FOLDER)/jlibs.zip -d $(POPLAR_LIB_FOLDER); \
		rm $(POPLAR_LIB_FOLDER)/jlibs.zip;\
		echo "Java libararies downloaded.";\
	fi

$(POPLAR_LIB_FOLDER)/%.class: src/%.java Makefile
	@echo -e "\033[1;34mCompiling $<\033[0;37m"
	javac -cp $(POPLAR_LIB_FOLDER)/jlibs/*:src: -d $(POPLAR_LIB_FOLDER) $<

test: FORCE all
	@echo -e "\n\033[1;32mPOPLAR_LIB_FOLDER: "$(POPLAR_LIB_FOLDER)"\033[0;37m"
	POPLAR_LIB_FOLDER=$(POPLAR_LIB_FOLDER) make -C test $(dataset)

test%: FORCE all
	@echo -e "\n\033[1;32mPOPLAR_LIB_FOLDER: "$(POPLAR_LIB_FOLDER)"\033[0;37m"
	POPLAR_LIB_FOLDER=$(POPLAR_LIB_FOLDER) make -C test $@ $(dataset)

download: 
	make -C test download

	
clean:
	rm -f $(POPLAR_LIB_FOLDER)/*.so $(POPLAR_LIB_FOLDER)/*.class /dev/shm/poplar_*
	touch src/* include/*
	make clean -C test
	
touch:
	touch src/* include/*

FORCE: ;