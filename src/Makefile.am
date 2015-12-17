noinst_PROGRAMS = cudaRenderer

cudaRenderer_SOURCES = cudarenderer.cu Algebra.cpp Camera.cpp Keyboard.cpp main.cpp 3d.h Algebra.h Base3d.h Camera.h Clock.h Keyboard.h Types.h cudarenderer.h Loader.cpp Loader.h Utility.cpp Utility.h BVH.h BVH.cpp

cudaRenderer_CPPFLAGS = @SDL_CFLAGS@ -I$(srcdir)/../lib3ds-1.3.0/

cudaRenderer_LDADD = @SDL_LIBS@ @CUDA_LIBS@ @SYS_GL_LIBS@ ../lib3ds-1.3.0/lib3ds/.libs/lib3ds.a

LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(AM_LDFLAGS) $(LDFLAGS) -o $@

cudarenderer.o:	cudarenderer.cu cudarenderer.h Types.h Base3d.h Camera.h
	$(AM_V_GEN)$(NVCC) -o $@ -c $< @NVCCFLAGS@ @SDL_CFLAGS@ @CUDA_CFLAGS@

.cu.o:
	$(AM_V_GEN)$(NVCC) -o $@ -c $< @NVCCFLAGS@ @SDL_CFLAGS@ @CUDA_CFLAGS@

bench:
	@for i in 1 2 3 4 5 ; do ./cudaRenderer -b 150 ../3D-objects/chessboard.tri | tail -1 | awk '{print substr($$(NF-1),2);}' ; done | perl -e '$$total=0; $$totalSq=0; $$n=0; my @allOfThem; while(<>) { print; chomp; $$total += $$_; $$totalSq += $$_*$$_; $$n++; push @allOfThem, $$_; } my $$variance = ($$totalSq - $$total*$$total/$$n)/($$n-1); my @srted = sort {$$a cmp $$b} @allOfThem; my $$len = scalar(@allOfThem); if ($$len % 2) { $$len++; } my @measurements = ( ["Average value",$$total/$$n], ["Std deviation",sqrt($$variance)], ["Median",$$srted[-1 + $$len/2]], ["Min",$$srted[0]], ["Max",$$srted[-1]]); foreach (@measurements) { printf("%*s: %f\n", 15, $$_->[0], $$_->[1]);}'
