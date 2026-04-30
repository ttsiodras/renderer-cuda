DISTCLEANFILES = *~ *.~*
AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = lib3ds-1.3.0 $(backend_dir)

EXTRA_DIST = README COPYING INSTALL AUTHORS NEWS ChangeLog \
	contrib/tryCombinations.py

dist-hook:
	mkdir -p $(distdir)/3D-objects/
	cp 3D-objects/*.ply $(distdir)/3D-objects/
	cp 3D-objects/*.tri $(distdir)/3D-objects/
	mkdir -p $(distdir)/src/cuda/
	cp src/cuda/Makefile.* src/cuda/*cpp src/cuda/*cu $(distdir)/src/cuda/
	mkdir -p $(distdir)/src/rocm_hip
	cp src/rocm_hip/Makefile.* src/rocm_hip/*h src/rocm_hip/*pp $(distdir)/src/rocm_hip/
	mkdir -p $(distdir)/src/common
	cp src/common/Makefile.am src/common/Makefile.in src/common/*cpp src/common/*h $(distdir)/src/common/

bench:
	make -C $(backend_dir) bench
