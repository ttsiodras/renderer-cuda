################################################################################
#
# Copyright 1993-2013 NVIDIA Corporation.  All rights reserved.
#
# NOTICE TO USER:   
#
# This source code is subject to NVIDIA ownership rights under U.S. and 
# international Copyright laws.  
#
# NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE 
# CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR 
# IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH 
# REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF 
# MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.   
# IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL, 
# OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS 
# OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
# OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE 
# OR PERFORMANCE OF THIS SOURCE CODE.  
#
# U.S. Government End Users.  This source code is a "commercial item" as 
# that term is defined at 48 C.F.R. 2.101 (OCT 1995), consisting  of 
# "commercial computer software" and "commercial computer software 
# documentation" as such terms are used in 48 C.F.R. 12.212 (SEPT 1995) 
# and is provided to the U.S. Government only as a commercial end item.  
# Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through 
# 227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the 
# source code with only those rights set forth herein.
#
################################################################################
#
#  findgllib.mk is used to find the necessary GL Libraries for specific distributions
#               this is supported on Mac OSX and Linux Platforms
#
################################################################################

# OS Name (Linux or Darwin)
OSUPPER = $(shell uname -s 2>/dev/null | tr "[:lower:]" "[:upper:]")
OSLOWER = $(shell uname -s 2>/dev/null | tr "[:upper:]" "[:lower:]")

# Flags to detect 32-bit or 64-bit OS platform
OS_SIZE = $(shell uname -m | sed -e "s/i.86/32/" -e "s/x86_64/64/" -e "s/armv7l/32/")
OS_ARCH = $(shell uname -m | sed -e "s/i386/i686/")

# Determine OS platform and unix distribution
ifeq ("$(OSLOWER)","linux")
   # first search lsb_release
   DISTRO  = $(shell lsb_release -i -s 2>/dev/null | tr "[:upper:]" "[:lower:]")
   DISTVER = $(shell lsb_release -r -s 2>/dev/null)
   # $(info DISTRO1 = $(DISTRO) $(DISTVER))
   ifeq ($(DISTRO),)
     # second search and parse /etc/issue
     DISTRO = $(shell more /etc/issue | awk '{print $$1}' | sed '1!d' | sed -e "/^$$/d" 2>/dev/null | tr "[:upper:]" "[:lower:]")
     DISTVER= $(shell more /etc/issue | awk '{print $$2}' | sed '1!d' 2>/dev/null)
     # $(info DISTRO2 = $(DISTRO) $(DISTVER))
   endif
   ifeq ($(DISTRO),)
     # third, we can search in /etc/os-release or /etc/{distro}-release
     DISTRO = $(shell awk '/ID/' /etc/*-release | sed 's/ID=//' | grep -v "VERSION" | grep -v "ID" | grep -v "DISTRIB")
     DISTVER= $(shell awk '/DISTRIB_RELEASE/' /etc/*-release | sed 's/DISTRIB_RELEASE=//' | grep -v "DISTRIB_RELEASE")
     # $(info DISTRO3 = $(DISTRO) $(DISTVER))
   endif
endif

# Take command line flags that override any of these settings
ifeq ($(i386),1)
	OS_SIZE = 32
	OS_ARCH = i686
endif
ifeq ($(x86_64),1)
	OS_SIZE = 64
	OS_ARCH = x86_64
endif
ifeq ($(ARMv7),1)
	OS_SIZE = 32
	OS_ARCH = armv7l
endif

ifeq ("$(OSUPPER)","LINUX")
    # $(info) >> findgllib.mk -> LINUX path <<<)
    # Each set of Linux Distros have different paths for where to find their OpenGL libraries reside
    ifeq ("$(DISTRO)","ubuntu")
        GLPATH    ?= /usr/lib/nvidia-current
        GLLINK    ?= -L/usr/lib/nvidia-current
        DFLT_PATH ?= /usr/lib
    endif
    ifeq ("$(DISTRO)","kubuntu")
        GLPATH    ?= /usr/lib/nvidia-current
        GLLINK    ?= -L/usr/lib/nvidia-current
        DFLT_PATH ?= /usr/lib
    endif
    ifeq ("$(DISTRO)","debian")
        GLPATH    ?= /usr/lib/nvidia-current
        GLLINK    ?= -L/usr/lib/nvidia-current
        DFLT_PATH ?= /usr/lib
    endif
    ifeq ("$(DISTRO)","suse")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/X11R6/lib64 /usr/X11R6/lib
        GLLINK    ?= -L/usr/X11R6/lib64 -L/usr/X11R6/lib
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?= /usr/X11R6/lib
        GLLINK    ?= -L/usr/X11R6/lib
        DFLT_PATH ?= /usr/lib
      endif
    endif
    ifeq ("$(DISTRO)","suse linux")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/X11R6/lib64 /usr/X11R6/lib
        GLLINK    ?= -L/usr/X11R6/lib64 -L/usr/X11R6/lib
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?= /usr/X11R6/lib
        GLLINK    ?= -L/usr/X11R6/lib
        DFLT_PATH ?= /usr/lib
      endif
    endif
    ifeq ("$(DISTRO)","opensuse")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/X11R6/lib64 /usr/X11R6/lib
        GLLINK    ?= -L/usr/X11R6/lib64 -L/usr/X11R6/lib
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?= /usr/X11R6/lib
        GLLINK    ?= -L/usr/X11R6/lib
        DFLT_PATH ?= /usr/lib
      endif
    endif
    ifeq ("$(DISTRO)","fedora")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/lib64/nvidia
        GLLINK    ?= -L/usr/lib64/nvidia
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?=
        GLLINK    ?=
        DFLT_PATH ?= /usr/lib
      endif
    endif
    ifeq ("$(DISTRO)","redhat")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/lib64/nvidia
        GLLINK    ?= -L/usr/lib64/nvidia
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?=
        GLLINK    ?=
        DFLT_PATH ?= /usr/lib
      endif
    endif
    ifeq ("$(DISTRO)","red")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/lib64/nvidia
        GLLINK    ?= -L/usr/lib64/nvidia
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?=
        GLLINK    ?=
        DFLT_PATH ?= /usr/lib
      endif
    endif
    ifeq ("$(DISTRO)","redhatenterpriseworkstation")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/lib64/nvidia
        GLLINK    ?= -L/usr/lib64/nvidia
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?=
        GLLINK    ?=
        DFLT_PATH ?= /usr/lib
      endif
    endif
    ifeq ("$(DISTRO)","centos")
      ifeq ($(OS_SIZE),64)
        GLPATH    ?= /usr/lib64/nvidia
        GLLINK    ?= -L/usr/lib64/nvidia
        DFLT_PATH ?= /usr/lib64
      else
        GLPATH    ?=
        GLLINK    ?=
        DFLT_PATH ?= /usr/lib
      endif
    endif
 
    ifeq ($(ARMv7),1)
      GLPATH := /usr/arm-linux-gnueabihf/lib
      GLLINK := -L/usr/arm-linux-gnueabihf/lib
      ifneq ($(TARGET_FS),) 
        GLPATH += $(TARGET_FS)/usr/lib/nvidia-current $(TARGET_FS)/usr/lib/arm-linux-gnueabihf
        GLLINK += -L$(TARGET_FS)/usr/lib/nvidia-current -L$(TARGET_FS)/usr/lib/arm-linux-gnueabihf
      endif 
    endif

  # find libGL, libGLU, libXi, 
  GLLIB  := $(shell find $(GLPATH) $(DFLT_PATH) -name libGL.so  -print 2>/dev/null)
  GLULIB := $(shell find $(GLPATH) $(DFLT_PATH) -name libGLU.so -print 2>/dev/null)
  X11LIB := $(shell find $(GLPATH) $(DFLT_PATH) -name libX11.so -print 2>/dev/null)
  XILIB  := $(shell find $(GLPATH) $(DFLT_PATH) -name libXi.so  -print 2>/dev/null)
  XMULIB := $(shell find $(GLPATH) $(DFLT_PATH) -name libXmu.so -print 2>/dev/null)

  ifeq ("$(GLLIB)",'')
      $(info >>> WARNING - libGL.so not found, refer to CUDA Samples release notes for how to find and install them. <<<)
      EXEC=@echo "[@]"
  endif
  ifeq ("$(GLULIB)",'')
      $(info >>> WARNING - libGLU.so not found, refer to CUDA Samples release notes for how to find and install them. <<<)
      EXEC=@echo "[@]"
  endif
  ifeq ("$(X11LIB)",'')
      $(info >>> WARNING - libX11.so not found, refer to CUDA Samples release notes for how to find and install them. <<<)
      EXEC=@echo "[@]"
  endif
  ifeq ("$(XILIB)",'')
      $(info >>> WARNING - libXi.so not found, refer to CUDA Samples release notes for how to find and install them. <<<)
      EXEC=@echo "[@]"
  endif
  ifeq ("$(XMULIB)",'')
      $(info >>> WARNING - libXmu.so not found, refer to CUDA Samples release notes for how to find and install them. <<<)
      EXEC=@echo "[@]"
  endif
else
    # This would be the Mac OS X path if we had to do anything special
endif

