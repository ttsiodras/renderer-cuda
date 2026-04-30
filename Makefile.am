DISTCLEANFILES = *~ *.~*
AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = lib3ds-1.3.0 $(backend_dir)

EXTRA_DIST = README COPYING INSTALL AUTHORS NEWS ChangeLog \
	3D-objects/trainColor.ply 3D-objects/chessboard.tri \
	contrib/tryCombinations.py

bench:
	make -C $(backend_dir) bench
