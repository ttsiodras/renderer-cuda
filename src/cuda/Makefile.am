AUTOMAKE_OPTIONS = subdir-objects
noinst_PROGRAMS = realtime_raytracer

realtime_raytracer_SOURCES = cudarenderer.cu ../common/Algebra.cpp ../common/Camera.cpp ../common/Keyboard.cpp main.cpp ../common/3d.h ../common/Algebra.h ../common/Base3d.h ../common/Camera.h ../common/Clock.h ../common/Keyboard.h ../common/Types.h ../common/ConstantsAndGlobals.h ../common/Loader.cpp ../common/Loader.h ../common/Utility.cpp ../common/Utility.h ../common/BVH.h ../common/BVH.cpp

realtime_raytracer_CPPFLAGS = @SDL_CFLAGS@ -I$(srcdir)/../../lib3ds-1.3.0/ -I$(srcdir)/../../src/common

realtime_raytracer_LDADD = @SDL_LIBS@ @CUDA_LIBS@ @SYS_GL_LIBS@ ../../lib3ds-1.3.0/lib3ds/.libs/lib3ds.a

LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@

cudarenderer.o:	cudarenderer.cu ../common/Types.h ../common/Base3d.h ../common/Camera.h
	$(AM_V_GEN)$(NVCC) -o $@ -c $< @NVCCFLAGS@ @SDL_CFLAGS@ @CUDA_CFLAGS@ -I../common

.cu.o:
	$(AM_V_GEN)$(NVCC) -o $@ -c $< @NVCCFLAGS@ @SDL_CFLAGS@ @CUDA_CFLAGS@

bench:
	@for i in 1 2 3 4 5 ; do ./cudaRenderer -b 150 ../3D-objects/chessboard.tri | tail -1 | awk '{print substr($$(NF-1),2);}' ; done | perl -e '$$total=0; $$totalSq=0; $$n=0; my @allOfThem; while(<>) { print; chomp; $$total += $$_; $$totalSq += $$_*$$_; $$n++; push @allOfThem, $$_; } my $$variance = ($$totalSq - $$total*$$total/$$n)/($$n-1); my @srted = sort {$$a cmp $$b} @allOfThem; my $$len = scalar(@allOfThem); if ($$len % 2) { $$len++; } my @measurements = ( ["Average value",$$total/$$n], ["Std deviation",sqrt($$variance)], ["Median",$$srted[-1 + $$len/2]], ["Min",$$srted[0]], ["Max",$$srted[-1]]); foreach (@measurements) { printf("%*s: %f\n", 15, $$_->[0], $$_->[1]);}'
