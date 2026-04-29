AUTOMAKE_OPTIONS = subdir-objects
bin_PROGRAMS = realtime_raytracer
CXX = $(HIPCC)

AM_CXXFLAGS = @HIP_CFLAGS@ -O3 -g -march=native

realtime_raytracer_SOURCES = main_rocm.cpp hiprenderer.hip.cpp \
    ../common/Algebra.cpp ../common/BVH.cpp ../common/Camera.cpp ../common/Keyboard.cpp \
    ../common/Loader.cpp ../common/Utility.cpp

realtime_raytracer_CPPFLAGS = @SDL_CFLAGS@ -I$(srcdir)/../../lib3ds-1.3.0/ -I$(srcdir)/../common/ @HIP_CFLAGS@

realtime_raytracer_LDADD = @SDL_LIBS@ @HIP_LIBS@ @SYS_GL_LIBS@ ../../lib3ds-1.3.0/lib3ds/.libs/lib3ds.a


