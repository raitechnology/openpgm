# openpgm makefile
lsb_dist     := $(shell if [ -f /etc/os-release ] ; then \
                  grep '^NAME=' /etc/os-release | sed 's/.*=[\"]*//' | sed 's/[ \"].*//' ; \
                  elif [ -x /usr/bin/lsb_release ] ; then \
                  lsb_release -is ; else echo Linux ; fi)
lsb_dist_ver := $(shell if [ -f /etc/os-release ] ; then \
		  grep '^VERSION=' /etc/os-release | sed 's/.*=[\"]*//' | sed 's/[ \"].*//' ; \
                  elif [ -x /usr/bin/lsb_release ] ; then \
                  lsb_release -rs | sed 's/[.].*//' ; else uname -r | sed 's/[-].*//' ; fi)
#lsb_dist     := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -is ; else echo Linux ; fi)
#lsb_dist_ver := $(shell if [ -x /usr/bin/lsb_release ] ; then lsb_release -rs | sed 's/[.].*//' ; else uname -r | sed 's/[-].*//' ; fi)
uname_m      := $(shell uname -m)

short_dist_lc := $(patsubst CentOS,rh,$(patsubst RedHatEnterprise,rh,\
                   $(patsubst RedHat,rh,\
                     $(patsubst Fedora,fc,$(patsubst Ubuntu,ub,\
                       $(patsubst Debian,deb,$(patsubst SUSE,ss,$(lsb_dist))))))))
short_dist    := $(shell echo $(short_dist_lc) | tr a-z A-Z)
pwd           := $(shell pwd)
rpm_os        := $(short_dist_lc)$(lsb_dist_ver).$(uname_m)

# this is where the targets are compiled
build_dir ?= $(short_dist)$(lsb_dist_ver)_$(uname_m)$(port_extra)
bind      := $(build_dir)/bin
libd      := $(build_dir)/lib64
objd      := $(build_dir)/obj
dependd   := $(build_dir)/dep

default_cflags := -ggdb -O3
# use 'make port_extra=-g' for debug build
ifeq (-g,$(findstring -g,$(port_extra)))
  default_cflags := -ggdb
endif
ifeq (-a,$(findstring -a,$(port_extra)))
  default_cflags := -fsanitize=address -ggdb -O3
endif
ifeq (-mingw,$(findstring -mingw,$(port_extra)))
  CC    := /usr/bin/x86_64-w64-mingw32-gcc
  CXX   := /usr/bin/x86_64-w64-mingw32-g++
  mingw := true
endif
# msys2 using ucrt64
ifeq (MSYS2,$(lsb_dist))
  mingw := true
endif
CC          ?= gcc
CXX         ?= g++
cc          := $(CC)
cpp         := $(CXX)
clink       := $(CC)
arch_cflags := -mavx2 -maes -fno-omit-frame-pointer
gcc_wflags  := -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-stringop-truncation -Wno-cpp
# if windows cross compile
ifeq (true,$(mingw))
dll       := dll
soflag    := -shared -Wl,--subsystem,windows
fpicflags := -fPIC -DPGM_SHARED
else
dll       := so
soflag    := -shared
fpicflags := -fPIC
endif
# make apple shared lib
ifeq (Darwin,$(lsb_dist))
dll       := dylib
endif
# rpmbuild uses RPM_OPT_FLAGS
ifeq ($(RPM_OPT_FLAGS),)
CFLAGS ?= $(default_cflags)
else
CFLAGS ?= $(RPM_OPT_FLAGS)
endif
cflags   := $(gcc_wflags) $(CFLAGS) -std=c99 $(default_cflags) $(arch_cflags)
cppflags := $(gcc_wflags) $(CFLAGS) -std=c++11 -fno-rtti -fno-exceptions $(default_cflags) $(arch_cflags)

# where to find the raids/xyz.h files
INCLUDES    ?= -Iopenpgm/pgm/include
includes    := $(INCLUDES)
DEFINES     ?= -D_REENTRANT \
	       -DHAVE_CLOCK_GETTIME \
	       -DHAVE_GETTIMEOFDAY \
	       -DHAVE_PTHREAD_SPINLOCK \
	       -DHAVE_GETPROTOBYNAME_R \
	       -DHAVE_GETNETENT \
	       -DHAVE_ALLOCA_H \
	       -DHAVE_EVENTFD \
	       -DHAVE_PROC_CPUINFO \
	       -DHAVE_BACKTRACE \
	       -DHAVE_DEV_RTC \
	       -DHAVE_RDTSC \
	       -DHAVE_DEV_HPET \
	       -DHAVE_POLL \
	       -DHAVE_EPOLL_CTL \
	       -DHAVE_GETIFADDRS \
	       -DHAVE_STRUCT_IFADDRS_IFR_NETMASK \
	       -DHAVE_STRUCT_GROUP_REQ \
	       -DHAVE_VASPRINTF \
	       -DUSE_BIND_INADDR_ANY \
	       -DHAVE_STRERROR_R \
	       -DSTRERROR_R_CHAR_P \
	       -D_XOPEN_SOURCE=600 \
	       -D_GNU_SOURCE \
	       -DHAVE_ISO_VARARGS \
	       -DHAVE_GNUC_VARARGS \
	       -DHAVE_STRUCT_IP_MREQN \
	       -DHAVE_SPRINTF_GROUPING \
	       -DHAVE_DSO_VISIBILITY \
	       -DUSE_TICKET_SPINLOCK \
	       -DUSE_DUMB_RWSPINLOCK \
	       -DUSE_GALOIS_MUL_LUT \
	       -DDISABLE_IP_MULTICAST_ALL \
	       -DUSE_GALOIS_SSE3 \
               -DNO_PGM_NOTIFY -DNO_PGM_THREADS
WIN_DEFS    ?= -DWIN32 \
	       -D_CRT_SECURE_NO_WARNINGS \
	       -D_WINSOCK_DEPRECATED_NO_WARNINGS \
	       -DHAVE_FTIME \
	       -DHAVE_ISO_VARARGS \
	       -DHAVE_RDTSC \
	       -DHAVE_WSACMSGHDR \
	       -DUSE_BIND_INADDR_ANY \
	       -DUSE_GALOIS_SSE3 \
               -DNO_PGM_NOTIFY -DNO_PGM_THREADS
#	       -DHAVE_DSO_VISIBILITY 
ifeq (true,$(mingw))
defines     := $(WIN_DEFS) -DMINGW
sock_lib    := -lws2_32 -lwinmm -liphlpapi
math_lib    := -lm
thread_lib  :=
else
defines     := $(DEFINES)
sock_lib    :=
math_lib    := -lm
thread_lib  := -pthread -lrt
endif
#st_defines  := -DNO_PGM_NOTIFY -DNO_PGM_THREADS

lnk_lib     :=
dlnk_lib    :=
lnk_dep     :=
dlnk_dep    :=

.PHONY: everything
everything: all

clean_subs :=
dlnk_dll_depend :=
dlnk_lib_depend :=

# copr/fedora build (with version env vars)
# copr uses this to generate a source rpm with the srpm target
-include .copr/Makefile

# debian build (debuild)
# target for building installable deb: dist_dpkg
-include deb/Makefile

# targets filled in below
all_exes    :=
all_libs    :=
all_dlls    :=
all_depends :=
gen_files   :=

openpgm/pgm/galois_tables.c: openpgm/pgm/galois_generator.pl
	perl openpgm/pgm/galois_generator.pl > openpgm/pgm/galois_tables.c

openpgm/pgm/version.c: openpgm/pgm/version_generator.py
	python openpgm/pgm/version_generator.py > openpgm/pgm/version.c

libopenpgm_files1 := \
                cpu thread mem string list slist queue hashtable \
                messages error math packet_parse packet_test \
                sockaddr time if inet_lnaof getifaddrs get_nprocs \
                getnetbyname getnodeaddr getprotobyname indextoaddr \
                indextoname nametoindex inet_network md5 rand \
                gsi tsi skbuff socket source receiver \
                recv engine timer net rate_control checksum \
                reed_solomon galois_tables wsastrerror histogram \
		atomic version
libopenpgm_files2 := rxw txw
libopenpgm_files := $(libopenpgm_files1) $(libopenpgm_files2)
libopenpgm_cfile := $(addprefix openpgm/pgm/, $(addsuffix .c, $(libopenpgm_files1))) \
                    $(addprefix openpgm/pgm/, $(addsuffix .cpp, $(libopenpgm_files2)))
libopenpgm_objs  := $(addprefix $(objd)/, $(addsuffix .o, $(libopenpgm_files)))
libopenpgm_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.o, $(libopenpgm_files)))
libopenpgm_deps  := $(addprefix $(dependd)/, $(addsuffix .d, $(libopenpgm_files))) \
                     $(addprefix $(dependd)/, $(addsuffix .fpic.d, $(libopenpgm_files)))
libopenpgm_spec  := $(version)-$(build_num)_$(git_hash)
libopenpgm_dylib := $(version).$(build_num)
libopenpgm_ver   := $(major_num).$(minor_num)

$(libd)/libopenpgm.a: $(libopenpgm_objs)

$(libd)/libopenpgm.$(dll): $(libopenpgm_dbjs)

#all_depends += $(libopenpgm_deps)
#all_dirs    += $(bind) $(libd) $(objd) $(dependd)
#all_libs    += $(libd)/libopenpgm.a $(libd)/libopenpgm.$(dll)

libopenpgm_st_objs  := $(addprefix $(objd)/, $(addsuffix .sto, $(libopenpgm_files)))
libopenpgm_st_dbjs  := $(addprefix $(objd)/, $(addsuffix .fpic.sto, $(libopenpgm_files)))
libopenpgm_st_deps  := $(addprefix $(dependd)/, $(addsuffix .std, $(libopenpgm_files))) \
                     $(addprefix $(dependd)/, $(addsuffix .fpic.std, $(libopenpgm_files)))
libopenpgm_st_spec  := $(version)-$(build_num)_$(git_hash)
libopenpgm_st_dylib := $(version).$(build_num)
libopenpgm_st_ver   := $(major_num).$(minor_num)

$(libd)/libopenpgm_st.a: $(libopenpgm_st_objs)

$(libd)/libopenpgm_st.$(dll): $(libopenpgm_st_dbjs)

all_depends += $(libopenpgm_st_deps)
all_dirs    += $(bind) $(libd) $(objd) $(dependd)
all_libs    += $(libd)/libopenpgm_st.a $(libd)/libopenpgm_st.$(dll)

all_dirs := $(bind) $(libd) $(objd) $(dependd)

# the default targets
.PHONY: all
all: $(all_libs) $(all_dlls) $(all_exes) cmake

.PHONY: cmake
cmake: CMakeLists.txt

.ONESHELL: CMakeLists.txt
CMakeLists.txt: .copr/Makefile
	@cat <<'EOF' > $@
	cmake_minimum_required (VERSION 3.9.0)
	if (POLICY CMP0111)
	  cmake_policy(SET CMP0111 OLD)
	endif ()
	project (openpgm)
	include_directories (
	  openpgm/pgm/include
	)
	if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	  if ($$<CONFIG:Release>)
	    add_compile_options (/arch:AVX2 /GL /std:c11 /wd5105)
	  else ()
	    add_compile_options (/arch:AVX2 /std:c11 /wd5105)
	  endif ()
	  add_definitions ($(WIN_DEFS))
	else ()
	  set (CMAKE_C_FLAGS "$(cflags)")
	  set (CMAKE_CXX_FLAGS "$(cppflags)")
	  add_definitions ($(DEFINES))
	endif ()
	add_library (openpgm_st STATIC $(libopenpgm_cfile))
	EOF

.PHONY: dnf_depend
dnf_depend:
	sudo dnf -y install make gcc-c++ git redhat-lsb openpgm-devel chrpath

.PHONY: yum_depend
yum_depend:
	sudo yum -y install make gcc-c++ git redhat-lsb openpgm-devel chrpath

.PHONY: deb_depend
deb_depend:
	sudo apt-get install -y install make g++ gcc devscripts openpgm-dev chrpath git lsb-release

# create directories
$(dependd):
	@mkdir -p $(all_dirs)

# remove target bins, objs, depends
.PHONY: clean
clean: $(clean_subs)
	rm -r -f $(bind) $(libd) $(objd) $(dependd)
	if [ "$(build_dir)" != "." ] ; then rmdir $(build_dir) ; fi

.PHONY: clean_dist
clean_dist:
	rm -rf dpkgbuild rpmbuild

.PHONY: clean_all
clean_all: clean clean_dist

# force a remake of depend using 'make -B depend'
.PHONY: depend
depend: $(dependd)/depend.make

$(dependd)/depend.make: $(dependd) $(all_depends)
	@echo "# depend file" > $(dependd)/depend.make
	@cat $(all_depends) >> $(dependd)/depend.make

ifeq (SunOS,$(lsb_dist))
remove_rpath = rpath -r
else
ifeq (Darwin,$(lsb_dist))
remove_rpath = true
else
remove_rpath = chrpath -d
endif
endif

.PHONY: dist_bins
dist_bins: $(all_libs)
	$(remove_rpath) $(libd)/libopenpgm_st.$(dll)

.PHONY: dist_rpm
dist_rpm: srpm
	( cd rpmbuild && rpmbuild --define "-topdir `pwd`" -ba SPECS/openpgm_st.spec )

# dependencies made by 'make depend'
-include $(dependd)/depend.make

ifeq ($(DESTDIR),)
# 'sudo make install' puts things in /usr/local/lib, /usr/local/include
install_prefix = /usr/local
else
# debuild uses DESTDIR to put things into debian/openpgm/usr
install_prefix = $(DESTDIR)/usr
endif

install: dist_bins
	install -d $(install_prefix)/lib $(install_prefix)/bin
	install -d $(install_prefix)/include/pgm
	for f in $(libd)/libopenpgm.* ; do \
	if [ -h $$f ] ; then \
	cp -a $$f $(install_prefix)/lib ; \
	else \
	install $$f $(install_prefix)/lib ; \
	fi ; \
	done
	install -m 644 openpgm/pgm/include/pgm/*.h $(install_prefix)/include/pgm

$(objd)/%.o: openpgm/pgm/%.c
	$(cc) $(cflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: openpgm/pgm/%.c
	$(cc) $(cflags) $(fpicflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.sto: openpgm/pgm/%.c
	$(cc) $(cflags) $(includes) $(st_defines) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.sto: openpgm/pgm/%.c
	$(cc) $(cflags) $(fpicflags) $(includes) $(st_defines) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.o: openpgm/pgm/%.cpp
	$(cpp) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.o: openpgm/pgm/%.cpp
	$(cpp) $(fpicflags) $(cppflags) $(includes) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.sto: openpgm/pgm/%.cpp
	$(cpp) $(cppflags) $(includes) $(st_defines) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(objd)/%.fpic.sto: openpgm/pgm/%.cpp
	$(cpp) $(cppflags) $(fpicflags) $(includes) $(st_defines) $(defines) $($(notdir $*)_includes) $($(notdir $*)_defines) -c $< -o $@

$(libd)/%.a:
	ar rc $@ $($(*)_objs)

ifeq (Darwin,$(lsb_dist))
$(libd)/%.dylib:
	$(clink) -dynamiclib $(cflags) -o $@.$($(*)_dylib).dylib -current_version $($(*)_dylib) -compatibility_version $($(*)_ver) $($(*)_dbjs) $($(*)_dlnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib) && \
	cd $(libd) && ln -f -s $(@F).$($(*)_dylib).dylib $(@F).$($(*)_ver).dylib && ln -f -s $(@F).$($(*)_ver).dylib $(@F)
else
$(libd)/%.$(dll):
	$(clink) $(soflag) $(rpath) $(cflags) -o $@.$($(*)_spec) -Wl,-soname=$(@F).$($(*)_ver) $($(*)_dbjs) $($(*)_dlnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib) && \
	cd $(libd) && ln -f -s $(@F).$($(*)_spec) $(@F).$($(*)_ver) && ln -f -s $(@F).$($(*)_ver) $(@F)
endif

$(bind)/%$(exe):
	$(clink) $(cflags) $(rpath) -o $@ $($(*)_objs) -L$(libd) $($(*)_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib)

$(bind)/%.static:
	$(clink) $(cflags) -o $@ $($(*)_objs) $($(*)_static_lnk) $(sock_lib) $(math_lib) $(thread_lib) $(malloc_lib) $(dynlink_lib)

$(dependd)/%.d: openpgm/pgm/%.c
	$(cc) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.fpic.d: openpgm/pgm/%.c
	$(cc) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.std: openpgm/pgm/%.c
	$(cc) $(arch_cflags) $(defines) $(st_defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).sto -MF $@

$(dependd)/%.fpic.std: openpgm/pgm/%.c
	$(cc) $(arch_cflags) $(defines) $(st_defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.sto -MF $@

$(dependd)/%.d: openpgm/pgm/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).o -MF $@

$(dependd)/%.fpic.d: openpgm/pgm/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.o -MF $@

$(dependd)/%.std: openpgm/pgm/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(st_defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).sto -MF $@

$(dependd)/%.fpic.std: openpgm/pgm/%.cpp
	$(cpp) $(arch_cflags) $(defines) $(st_defines) $(includes) $($(notdir $*)_includes) $($(notdir $*)_defines) -MM $< -MT $(objd)/$(*).fpic.sto -MF $@

