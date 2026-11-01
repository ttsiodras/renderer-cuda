AC_INIT([RealtimeRaytracer], [3.1], [ttsiodras@gmail.com])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([ac-macros])

AC_CHECK_SIZEOF([long])
if test "$ac_cv_sizeof_long" -eq 8 ; then
    SUFFIX64="64"
else
    SUFFIX64=""
fi

AC_ARG_WITH([cuda],
    [  --with-cudaSDK=PATH        prefix where CUDA SDK is installed])
if test "x$with_cudaSDK" != x; then
    CUDA_CFLAGS="-I$with_cudaSDK/include"
    CUDA_LIBS="-L$with_cudaSDK/lib$SUFFIX64 -lcudart"
    NVCC="$with_cudaSDK/bin/nvcc"
else
    if test -d /usr/local/cuda -o -L /usr/local/cuda ; then
        with_cudaSDK=/usr/local/cuda
        CUDA_CFLAGS="-I$with_cudaSDK/include"
        CUDA_LIBS="-L$with_cudaSDK/lib$SUFFIX64 -lcudart"
        NVCC="$with_cudaSDK/bin/nvcc"
    else 
        if test -d /opt/cuda ; then
            with_cudaSDK=/opt/cuda
            CUDA_CFLAGS="-I$with_cudaSDK/include"
            CUDA_LIBS="-L$with_cudaSDK/lib$SUFFIX64 -lcudart"
            NVCC="$with_cudaSDK/bin/nvcc"
        else
            # Assume it is installed in global directories
            CUDA_CFLAGS=""
            CUDA_LIBS="-lcudart"
            NVCC="nvcc"
        fi
    fi
fi
NVCCFLAGS="-O2 -use_fast_math --compiler-options -fno-inline -I ."

OLDCFLAGS="$CFLAGS"
OLDCPPFLAGS="$CPPFLAGS"
CFLAGS="$OLDCFLAGS"
CPPFLAGS="$OLDCPPFLAGS"
AC_SUBST(CUDA_CFLAGS)
AC_SUBST(CUDA_LIBS)
AC_SUBST(NVCC)
AC_SUBST(NVCCFLAGS)

# Options for selecting backend
AC_ARG_ENABLE([cuda],
  [AS_HELP_STRING([--enable-cuda],[build with CUDA (default auto-detect)])],
  [enable_cuda=$enableval],
  [enable_cuda=auto])
AC_ARG_ENABLE([rocm],
  [AS_HELP_STRING([--enable-rocm],[build with ROCm/HIP (default auto-detect)])],
  [enable_rocm=$enableval],
  [enable_rocm=auto])
AC_ARG_ENABLE([sycl],
  [AS_HELP_STRING([--enable-sycl],[build with SYCL (default auto-detect)])],
  [enable_sycl=$enableval],
  [enable_sycl=auto])

# Detect CUDA (if enabled)
if test "x$enable_cuda" != "xno"; then
  AC_PATH_PROG([NVCC], [nvcc])
  if test -n "$NVCC" && $NVCC --version >/dev/null 2>&1; then
    HAVE_CUDA=yes
  else
    HAVE_CUDA=no
  fi
else
  HAVE_CUDA=no
fi

# Detect HIP (ROCm) if enabled
if test "x$enable_rocm" != "xno"; then
  AC_PATH_PROG([HIPCC], [hipcc])
  if test -n "$HIPCC" && $HIPCC --version >/dev/null 2>&1; then
    HAVE_HIP=yes
    # Basic flags; can be overridden by environment variables
    HIP_CFLAGS="-I$(dirname $HIPCC)/../include"
    HIP_LIBS="-L$(dirname $HIPCC)/../lib -lhipblas -lamdhip64"
  else
    HAVE_HIP=no
  fi
else
  HAVE_HIP=no
fi

# Detect SYCL (Intel oneAPI DPC++/icpx, or the dpcpp driver)
SYCL_CFLAGS=""
SYCL_LIBS="-lsycl"
if test "x$enable_sycl" != "xno"; then
  AC_PATH_PROGS([SYCLCXX], [icpx dpcpp], [])
  if test -z "$SYCLCXX"; then
    for d in /opt/intel/oneapi/compiler/latest/bin \
             /opt/intel/oneapi/compiler/*/bin \
             /opt/intel/oneapi/compiler/*/bin/compiler ; do
      if test -x "$d/icpx"; then
        SYCLCXX="$d/icpx"
        break
      fi
      if test -x "$d/dpcpp"; then
        SYCLCXX="$d/dpcpp"
        break
      fi
    done
  fi
  if test -n "$SYCLCXX" && $SYCLCXX -fsycl -x c++ -fsyntax-only /dev/null >/dev/null 2>&1; then
    HAVE_SYCL=yes
  else
    HAVE_SYCL=no
  fi
else
  HAVE_SYCL=no
fi

# Determine which backend to use
requested_backends=0
if test "x$enable_cuda" = "xyes"; then requested_backends=`expr $requested_backends + 1`; fi
if test "x$enable_rocm" = "xyes"; then requested_backends=`expr $requested_backends + 1`; fi
if test "x$enable_sycl" = "xyes"; then requested_backends=`expr $requested_backends + 1`; fi
if test $requested_backends -gt 1; then
  AC_MSG_ERROR([Only one of --enable-cuda, --enable-rocm and --enable-sycl may be used])
fi

if test "x$enable_cuda" = "xyes"; then
  if test "$HAVE_CUDA" != "yes"; then
    AC_MSG_ERROR([CUDA was explicitly requested, but no working nvcc was found])
  fi
  BACKEND=cuda
elif test "x$enable_rocm" = "xyes"; then
  if test "$HAVE_HIP" != "yes"; then
    AC_MSG_ERROR([ROCm/HIP was explicitly requested, but no working hipcc was found])
  fi
  BACKEND=rocm
elif test "x$enable_sycl" = "xyes"; then
  if test "$HAVE_SYCL" != "yes"; then
    AC_MSG_ERROR([SYCL was explicitly requested, but no working icpx/dpcpp was found])
  fi
  BACKEND=sycl
else
  # Auto-detect
  if test "$HAVE_CUDA" = "yes"; then
    BACKEND=cuda
  elif test "$HAVE_HIP" = "yes"; then
    BACKEND=rocm
  elif test "$HAVE_SYCL" = "yes"; then
    BACKEND=sycl
  else
    AC_MSG_ERROR([Neither CUDA, ROCm/HIP nor SYCL toolchains were found. Use --enable-cuda, --enable-rocm or --enable-sycl])
  fi
fi

if test "$BACKEND" = "cuda"; then
  BACKEND_DIR=src/cuda
elif test "$BACKEND" = "rocm"; then
  BACKEND_DIR=src/rocm_hip
else
  BACKEND_DIR=src/sycl
fi
backend_dir=$BACKEND_DIR
AC_SUBST([backend_dir])
AC_SUBST([BACKEND])
AC_SUBST([NVCC])
AC_SUBST([NVCCFLAGS])
AC_SUBST([HIPCC])
AC_SUBST([HIP_CFLAGS])
AC_SUBST([HIP_LIBS])
AC_SUBST([SYCLCXX])
AC_SUBST([SYCL_CFLAGS])
AC_SUBST([SYCL_LIBS])


# Define macros for backend presence
if test "$HAVE_CUDA" = "yes"; then
  AC_DEFINE([HAVE_CUDA], [1], [Define if CUDA backend is available])
fi
if test "$HAVE_HIP" = "yes"; then
  AC_DEFINE([HAVE_HIP], [1], [Define if HIP backend is available])
fi
if test "$HAVE_SYCL" = "yes"; then
  AC_DEFINE([HAVE_SYCL], [1], [Define if SYCL backend is available])
fi
if test "$BACKEND" = "sycl"; then
  AC_DEFINE([BACKEND_SYCL], [1], [Define if the selected backend is SYCL])
fi

# Detect the canonical host and target build environment
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_LANG(C++)

AM_INIT_AUTOMAKE([-Wall -Werror])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

if test x"${CXXFLAGS}" = x ; then
    CXXFLAGS="-O3 -g -Wall"
fi

CXXFLAGS+=" ${CUDA_CFLAGS}"

AM_PROG_CC_C_O

AC_PROG_CXX

AC_CHECK_FUNCS([atexit])
AC_CHECK_HEADERS([stdlib.h string.h])
AC_FUNC_MALLOC
AC_FUNC_VPRINTF
AC_HEADER_STDC

AC_CONFIG_SUBDIRS([lib3ds-1.3.0])

# Best optimization flags for our compiler
if test x"${enable_debug}" = xyes ; then
	AC_MSG_NOTICE([Disabling optimizations])
	CXXFLAGS=`echo "$CXXFLAGS" | sed 's,-O3,,g'`
else
        AX_CXXFLAGS_GCC_OPTION(-Wall)
        AX_CXXFLAGS_GCC_OPTION(-Wextra)
#         AX_CXXFLAGS_GCC_OPTION(-pedantic)
        AX_CXXFLAGS_GCC_OPTION(-fomit-frame-pointer)
        AX_CXXFLAGS_GCC_OPTION(-ffast-math)
        AX_CXXFLAGS_GCC_OPTION(-funsafe-math-optimizations)
        AX_CXXFLAGS_GCC_OPTION(-mtune=native)

# GCC support for -flto is still buggy, internal compiler error triggered...
#        AX_CXXFLAGS_GCC_OPTION(-flto)
#	 AX_CHECK_LINKER_FLAGS(-fwhole-program,[WHOLEPROGRAM="-fwhole-program"],[])
#	 AC_SUBST(WHOLEPROGRAM)

	# SIMD detection - core logic from mplayer

	AC_MSG_CHECKING(for SSE)
	AC_RUN_IFELSE([AC_LANG_SOURCE([#include <stdlib.h>
#include <signal.h>
void catchme(int) { exit(1); }
int main(void) {
signal(SIGILL, catchme);
__asm__ volatile ("xorps %%xmm0, %%xmm0":::"memory"); return 0;
}
	])], [HAVESSE=yes], [HAVESSE=no])
	AC_MSG_RESULT($HAVESSE)
	if test x"${HAVESSE}" = xyes ; then
	    AC_DEFINE([SIMD_SSE], 1, [Define this to use SSE intrinsics.])
	fi

	AC_MSG_CHECKING(for SSE2)
	AC_RUN_IFELSE([AC_LANG_SOURCE([#include <stdlib.h>
#include <signal.h>
void catchme(int) { exit(1); }
int main(void) {
signal(SIGILL, catchme);
__asm__ volatile ("xorpd %%xmm0, %%xmm0":::"memory"); return 0;
}
	])], [HAVESSE2=yes], [HAVESSE2=no])
	AC_MSG_RESULT($HAVESSE2)

	AC_MSG_CHECKING(for SSSE3)
	AC_RUN_IFELSE([AC_LANG_SOURCE([#include <stdlib.h>
#include <signal.h>
void catchme(int) { exit(1); }
int main(void) {
signal(SIGILL, catchme);
__asm__ volatile ("pabsd %%xmm0, %%xmm0":::"memory"); return 0;
}
	])], [HAVESSSE3=yes], [HAVESSSE3=no])
	AC_MSG_RESULT($HAVESSSE3)

	# SSE/CPU specific flags disabled
	CXXFLAGS="$CXXFLAGS -DNDEBUG"
fi


# Figure out which OpenGL and GLU libraries to use
case "$target" in
    *-*-cygwin* | *-*-mingw32*)
        SYS_GL_LIBS="-lopengl32 -lglu32 -lglut"
        ;;
    *-*-beos*)
        SYS_GL_LIBS="-lGL -lGLU -lGLEW -lglut"
        ;;
    *-*-aix*)
        if test x$ac_cv_prog_gcc = xyes; then
            CFLAGS="-mthreads"
        fi
        ;;
    *-*-darwin*)
    	CPPFLAGS="$CPPFLAGS -I/usr/X11R6/include"
	SYS_GL_LIBS="-L/usr/X11R6/lib -lGL -lGLU -lGLEW -lglut"
        ;;
    *)
        AC_PATH_X
        AC_PATH_XTRA
        if test x$have_x = xyes; then
            CPPFLAGS="$CPPFLAGS $X_CFLAGS"
            SYS_GL_LIBS="$X_LIBS -lGL -lGLU -lGLEW -lglut"
        else
            AC_MSG_ERROR([*** No X detected!])
        fi
        ;;
esac

# Check for OpenGL
AC_MSG_CHECKING(for OpenGL GLUT and GLEW support)
HAVE_OPENGL=no
AC_TRY_COMPILE([
 #include <GL/glew.h>
 #include <GL/gl.h>
 #include <GL/glut.h>
],[
],[
HAVE_OPENGL=yes
])
AC_MSG_RESULT($HAVE_OPENGL)
if test x$HAVE_OPENGL != xyes; then
    AC_MSG_ERROR([*** OpenGL GLEW and GLUT must be installed!])
fi
AC_SUBST(SYS_GL_LIBS)

# Check for SDL (minimum: 1.2.0)
SDL_VERSION=1.2.0
AM_PATH_SDL($SDL_VERSION, :,
            AC_MSG_ERROR([*** SDL version $SDL_VERSION or later was not found!]))
AC_SUBST(SDL_CFLAGS)
AC_SUBST(SDL_LIBS)

# Finally create all the generated files
AC_CONFIG_FILES([Makefile src/cuda/Makefile src/rocm_hip/Makefile src/sycl/Makefile src/common/Makefile])
AC_OUTPUT

# Final summary printed after everything
AC_MSG_NOTICE([backend configuration:])
AC_MSG_NOTICE([  CUDA     : $HAVE_CUDA])
AC_MSG_NOTICE([  HIP      : $HAVE_HIP])
AC_MSG_NOTICE([  SYCL     : $HAVE_SYCL])
AC_MSG_NOTICE([  selected : $BACKEND])
