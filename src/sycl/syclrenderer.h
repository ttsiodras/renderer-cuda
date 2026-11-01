/*
 *  renderer - A simple implementation of polygon-based 3D algorithms.
 *  Copyright (C) 2004  Thanassis Tsiodras (ttsiodras@gmail.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SYCLRENDERER_H
#define SYCLRENDERER_H

#include <sycl/sycl.hpp>

struct Matrix3;
struct Vertex;
struct Triangle;
struct Vector3;

// Render one frame with the SYCL backend.
//
// All pointers except 'pixels' point to USM device allocations that were
// prepared once by main_sycl.cpp.  'pixels' is a USM shared allocation that
// the kernel writes into, and that the host can then upload to OpenGL.
void Raytrace(
    sycl::queue &q,
    Matrix3 *worldToCameraSpace,
    Vertex *vertices, Triangle *triangles, float *triangleIntersectionData,
    int *triIdxList, float *bvhLimits, int *bvhIndexesOrTrilists,
    Vector3 *eyePosInWorldSpace, Vector3 *lightPosInWorldSpace,
    unsigned *mortonTable,
    int *pixels);

#endif // SYCLRENDERER_H
