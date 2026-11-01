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

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <sycl/sycl.hpp>

#include "Types.h"
#include "Base3d.h"
#include "Camera.h"

#include "ConstantsAndGlobals.h"

#include "syclrenderer.h"

/////////////////////////////////
// Raytracing configuration
//
// The SYCL backend does not use textures.  All scene data lives in ordinary
// USM buffers, which are read through the normal global-memory path.  On the
// Intel iGPU (and on modern dGPUs) these reads are cached by the L1/L2/L3
// hierarchy, so the old CUDA texture trick is unnecessary.
#define THREADS_PER_BLOCK 32

// What depth to stop reflections and refractions?
#define MAX_RAY_DEPTH 2

// Ray intersections of a distance <=NUDGE_FACTOR (from the origin) don't count
#define NUDGE_FACTOR 1e-5f

// How much the reflected color contributes to the overall
#define REFLECTIONS_RATE 0.375f

////////////////////////////////////////////////////////////////
// Everything the device needs in order to trace a frame.

struct SceneView {
    // Two float4 per vertex: position+AO, normal+padding
    const float4 *vertices;
    // Original Triangle structs (color and vertex indices)
    const Triangle *triangles;
    // Five float4 per triangle: center+twoSided, normal+d, e1+d1, e2+d2, e3+d3
    const float4 *triIntersection;
    // Leaf triangle lists
    const int *triIdxList;
    // Three float2 per BVH node: (bottom.x,top.x), (bottom.y,top.y), (bottom.z,top.z)
    const float2 *bvhLimits;
    // One uint4 per BVH node: count, idxRight, idxLeft, startIndexInTriIndexList
    const uint4 *bvhIndexes;

    unsigned verticesNo;
    unsigned trianglesNo;
};

////////////////////////////////////////////////////////////////
// Small device-side math helpers (the CUDA/HIP files call these CUDA/HIP)

inline coord dotSYCL(const Vector3 &l, const Vector3 &r)
{
    return l._x * r._x + l._y * r._y + l._z * r._z;
}

inline coord dotSYCL(const float4 &l, const Vector3 &r)
{
    return l.x * r._x + l.y * r._y + l.z * r._z;
}

inline coord dotSYCL(const Vector3 &l, const float4 &r)
{
    return l._x * r.x + l._y * r.y + l._z * r.z;
}

inline Vector3 crossSYCL(const Vector3 &l, const Vector3 &r)
{
    coord x, y, z;
    const coord &aax = l._x;
    const coord &aay = l._y;
    const coord &aaz = l._z;
    const coord &bbx = r._x;
    const coord &bby = r._y;
    const coord &bbz = r._z;
    x = aay * bbz - bby * aaz;
    y = bbx * aaz - aax * bbz;
    z = aax * bby - aay * bbx;
    return Vector3(x, y, z);
}

inline coord distanceSYCL(const Vector3 &a, const Vector3 &b)
{
    coord dx = a._x - b._x;
    coord dy = a._y - b._y;
    coord dz = a._z - b._z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

inline coord distancesqSYCL(const Vector3 &a, const Vector3 &b)
{
    coord dx = a._x - b._x;
    coord dy = a._y - b._y;
    coord dz = a._z - b._z;
    return dx * dx + dy * dy + dz * dz;
}

inline Vector3 multiplyRightWith(const Matrix3 &mv, const Vector3 &r)
{
    coord xnew = mv._row1._x * r._x + mv._row1._y * r._y + mv._row1._z * r._z;
    coord ynew = mv._row2._x * r._x + mv._row2._y * r._y + mv._row2._z * r._z;
    coord znew = mv._row3._x * r._x + mv._row3._y * r._y + mv._row3._z * r._z;
    return Vector3(xnew, ynew, znew);
}

inline Vector3 TransformToSomeSpace(Vector3 point, const Matrix3 *mv, const Vector3 *origin)
{
    point -= *origin;
    return multiplyRightWith(*mv, point);
}

// After transformation in camera space, project and plot (used for point rendering)
#define CLIPPLANEDISTANCE 0.2f

inline void ProjectAndPlot(const Vector3 &xformed, int *pixels, int defaultColor = 0x00FFFFFF)
{
    if (xformed._z > CLIPPLANEDISTANCE) {
        int x = (int)(MAXX / 2.f + FOV * xformed._y / xformed._z);
        int y = (int)(MAXY / 2.f - FOV * xformed._x / xformed._z);
        if (y >= 0.f && y < (int)MAXY && x >= 0.f && x < (int)MAXX)
            pixels[y * MAXX + x] = defaultColor;
    }
}

// Create OpenGL BGR value for assignment in the pixel buffer
inline int getColor(const Pixel &p)
{
    return (((unsigned)p._b) << 16) | (((unsigned)p._g) << 8) | (((unsigned)p._r));
}

////////////////////////////////////////
// Rendering kernel for MODE_POINTS
////////////////////////////////////////

struct PointsKernel {
    int *pixels;
    const Matrix3 *worldToCameraSpace;
    const Vector3 *eye;
    const float4 *vertices;
    unsigned count;

    void operator()(sycl::nd_item<1> item) const
    {
        unsigned idx = item.get_global_id(0);
        if (idx >= count)
            return;

        Vector3 v(vertices[2 * idx]);
        ProjectAndPlot(
            TransformToSomeSpace(v, worldToCameraSpace, eye),
            pixels);
    }
};

///////////////////////////////////////////////
// Raytracing modes
///////////////////////////////////////////////

// Helper function, that checks whether a ray intersects a bbox
inline bool RayIntersectsBox(
    const Vector3 &originInWorldSpace, const Vector3 &rayInWorldSpace,
    int boxIdx, const float2 *bvhLimits)
{
    coord Tnear, Tfar;
    Tnear = -FLT_MAX;
    Tfar = FLT_MAX;

    float2 limits;

#define CHECK_NEAR_AND_FAR_INTERSECTION(c)                                              \
    if (rayInWorldSpace._##c == 0.f) {                                                  \
        if (originInWorldSpace._##c < limits.x) return false;                           \
        if (originInWorldSpace._##c > limits.y) return false;                           \
    } else {                                                                            \
        coord T1 = (limits.x - originInWorldSpace._##c) / rayInWorldSpace._##c;         \
        coord T2 = (limits.y - originInWorldSpace._##c) / rayInWorldSpace._##c;         \
        if (T1 > T2) { coord tmp = T1; T1 = T2; T2 = tmp; }                             \
        if (T1 > Tnear) Tnear = T1;                                                     \
        if (T2 < Tfar)  Tfar = T2;                                                      \
        if (Tnear > Tfar)                                                               \
            return false;                                                               \
        if (Tfar < 0.f)                                                                 \
            return false;                                                               \
    }

    limits = bvhLimits[3 * boxIdx];
    CHECK_NEAR_AND_FAR_INTERSECTION(x)
    limits = bvhLimits[3 * boxIdx + 1];
    CHECK_NEAR_AND_FAR_INTERSECTION(y)
    limits = bvhLimits[3 * boxIdx + 2];
    CHECK_NEAR_AND_FAR_INTERSECTION(z)

    return true;

#undef CHECK_NEAR_AND_FAR_INTERSECTION
}

// Templated ray/triangle intersection function - offers two compile-time options:
//
// The first one is used to discriminate between shadow rays (that stop at the first hit)
// and normal rays, that have to find the closest hit.
//
// The second one enables or disables culling of backfacing triangles.
template <bool stopAtfirstRayHit, bool doCulling>
inline bool BVH_IntersectTriangles(
    const Vector3 &origin, const Vector3 &ray, unsigned avoidSelf,
    int &pBestTriIdx,
    Vector3 &pointHitInWorldSpace,
    coord &kAB, coord &kBC, coord &kCA,
    const SceneView &scene)
{
    pBestTriIdx = -1;
    coord bestTriDist;

    // light position passed-in pointHitInWorldSpace (only in shadow mode - i.e. stopAtfirstRayHit=true)
    Vector3 &lightPos = pointHitInWorldSpace;

    if (stopAtfirstRayHit)
        bestTriDist = distancesqSYCL(origin, lightPos);
    else
        bestTriDist = FLT_MAX;

    int stack[BVH_STACK_SIZE];
    int stackIdx = 0;
    stack[stackIdx++] = 0;
    while (stackIdx) {
        int boxIdx = stack[stackIdx - 1];
        stackIdx--;

        uint4 data = scene.bvhIndexes[boxIdx];

        if (!(data.x & 0x80000000)) {
            if (RayIntersectsBox(origin, ray, boxIdx, scene.bvhLimits)) {
                stack[stackIdx++] = data.y;
                stack[stackIdx++] = data.z;
                if (stackIdx > BVH_STACK_SIZE) {
                    return false; // XXX
                }
            }
        } else {
            for (unsigned i = data.w; i < data.w + (data.x & 0x7fffffff); i++) {
                int idx = scene.triIdxList[i];

                if (avoidSelf == (unsigned)idx)
                    continue; // avoid self-reflections/refractions

                float4 center = scene.triIntersection[5 * idx];
                float4 normal = scene.triIntersection[5 * idx + 1];

                if (doCulling && (center.w == 0.f)) {
                    // Check visibility of triangle via dot product
                    Vector3 fromTriToOrigin = origin;
                    fromTriToOrigin -= center;
                    if (dotSYCL(fromTriToOrigin, normal) < 0)
                        continue;
                }

                // Use the pre-computed triangle intersection data: normal, d, e1/d1, e2/d2, e3/d3
                coord k = dotSYCL(normal, ray);
                if (k == 0.0f)
                    continue; // this triangle is parallel to the ray, ignore it.

                coord s = (normal.w - dotSYCL(normal, origin)) / k;
                if (s <= 0.0f) // this triangle is "behind" the origin.
                    continue;
                if (s <= NUDGE_FACTOR)
                    continue;
                Vector3 hit = ray * s;
                hit += origin;

                // Is the intersection of the ray with the triangle's plane INSIDE the triangle?
                float4 ee1 = scene.triIntersection[5 * idx + 2];
                coord kt1 = dotSYCL(ee1, hit) - ee1.w; if (kt1 < 0.0f) continue;
                float4 ee2 = scene.triIntersection[5 * idx + 3];
                coord kt2 = dotSYCL(ee2, hit) - ee2.w; if (kt2 < 0.0f) continue;
                float4 ee3 = scene.triIntersection[5 * idx + 4];
                coord kt3 = dotSYCL(ee3, hit) - ee3.w; if (kt3 < 0.0f) continue;

                if (stopAtfirstRayHit) {
                    coord dist = distancesqSYCL(lightPos, hit);
                    if (dist < bestTriDist)
                        return true;
                } else {
                    coord hitZ = distancesqSYCL(origin, hit);
                    if (hitZ < bestTriDist) {
                        bestTriDist = hitZ;
                        pBestTriIdx = idx;
                        pointHitInWorldSpace = hit;
                        kAB = kt1;
                        kBC = kt2;
                        kCA = kt3;
                    }
                }
            }
        }
    }

    if (!stopAtfirstRayHit)
        return pBestTriIdx != -1;
    else
        return false;
}

// Compile-time recursion using the "depth" template parameter.
template <int depth, bool doSpecular, bool doPhongInterp, bool doReflections, bool doShadows, bool doCulling>
inline Pixel Raytrace(
    Vector3 originInWorldSpace, Vector3 rayInWorldSpace, int avoidSelf,
    const SceneView &scene,
    const Vector3 *eyePosInWorldSpace, Vector3 *lightPosInWorldSpace)
{
    int pBestTriIdx = -1;
    const Triangle *pBestTri = NULL;
    Vector3 pointHitInWorldSpace;
    coord kAB = 0.f, kBC = 0.f, kCA = 0.f;

    if (!BVH_IntersectTriangles<false, doCulling>(
            originInWorldSpace, rayInWorldSpace, avoidSelf,
            pBestTriIdx, pointHitInWorldSpace, kAB, kBC, kCA, scene))
        return Pixel(0.f, 0.f, 0.f);

    avoidSelf = pBestTriIdx;
    pBestTri = &scene.triangles[pBestTriIdx];

    Pixel color = pBestTri->_colorf;

    Vector3 phongNormal;
    coord ABx, BCx, CAx, area;
    float4 V1;
    float4 N1;
    float4 V2;
    float4 N2;
    float4 V3;
    float4 N3;
    V1 = scene.vertices[2 * pBestTri->_idx1];
    V2 = scene.vertices[2 * pBestTri->_idx2];
    V3 = scene.vertices[2 * pBestTri->_idx3];
    if (doPhongInterp) {
        N1 = scene.vertices[2 * pBestTri->_idx1 + 1];
        N2 = scene.vertices[2 * pBestTri->_idx2 + 1];
        N3 = scene.vertices[2 * pBestTri->_idx3 + 1];
        const Vector3 bestTriA = Vector3(V1.x, V1.y, V1.z);
        const Vector3 bestTriB = Vector3(V2.x, V2.y, V2.z);
        const Vector3 bestTriC = Vector3(V3.x, V3.y, V3.z);
        const Vector3 bestTriNrmA = Vector3(N1.x, N1.y, N1.z);
        const Vector3 bestTriNrmB = Vector3(N2.x, N2.y, N2.z);
        const Vector3 bestTriNrmC = Vector3(N3.x, N3.y, N3.z);

        Vector3 AB = bestTriB; AB -= bestTriA;
        Vector3 BC = bestTriC; BC -= bestTriB;
        Vector3 crossAB_BC = crossSYCL(AB, BC);
        area = crossAB_BC.length();

        ABx = kAB * distanceSYCL(bestTriA, bestTriB);
        BCx = kBC * distanceSYCL(bestTriB, bestTriC);
        CAx = kCA * distanceSYCL(bestTriC, bestTriA);

        Vector3 phongNormalA = bestTriNrmA; phongNormalA *= BCx / area;
        Vector3 phongNormalB = bestTriNrmB; phongNormalB *= CAx / area;
        Vector3 phongNormalC = bestTriNrmC; phongNormalC *= ABx / area;

        phongNormal = phongNormalA + phongNormalB + phongNormalC;
        phongNormal.normalize();
    } else {
        phongNormal = pBestTri->_normal;
    }

    // Use the pre-calculated ambient occlusion value from the model.
    coord ambientOcclusionCoeff;
    if (doPhongInterp) {
        ambientOcclusionCoeff =
            V1.w * BCx / area +
            V2.w * CAx / area +
            V3.w * ABx / area;
    } else {
        ambientOcclusionCoeff = (V1.w + V2.w + V3.w) / 3.f;
    }
    coord ambientFactor = (coord)((AMBIENT * ambientOcclusionCoeff / 255.0f) / 255.0f);
    color *= ambientFactor;

    Vector3 &light = *lightPosInWorldSpace;

    Pixel dColor = Pixel();

    Vector3 pointToLight = light;
    pointToLight -= pointHitInWorldSpace;

    bool inShadow = false;

    if (doShadows) {
        coord distanceFromLightSq = pointToLight.lengthsq();

        Vector3 shadowrayInWorldSpace = pointToLight;
        shadowrayInWorldSpace /= sqrt(distanceFromLightSq);

        int pDummy;
        if (BVH_IntersectTriangles<true, doCulling>(
                pointHitInWorldSpace, shadowrayInWorldSpace, avoidSelf,
                pDummy, light,
                kAB, kAB, kAB, scene)) {
            inShadow = true;
        }
    }

    if (!inShadow) {
        pointToLight.normalize();

        coord intensity = dotSYCL(phongNormal, pointToLight);
        if (intensity < 0.f) {
            ; // in shadow, let it be in ambient
        } else {
            Pixel diffuse = pBestTri->_colorf;
            diffuse *= (coord)(DIFFUSE * intensity / 255.f);
            dColor += diffuse;

            if (doSpecular) {
                Vector3 pointToCamera = *eyePosInWorldSpace;
                pointToCamera -= pointHitInWorldSpace;
                pointToCamera.normalize();

                Vector3 half = pointToLight;
                half += pointToCamera;
                half.normalize();

                coord intensity2 = dotSYCL(half, phongNormal);
                if (intensity2 > 0.f) {
                    intensity2 *= intensity2;
                    intensity2 *= intensity2;
                    intensity2 *= intensity2;
                    intensity2 *= intensity2;
                    intensity2 *= intensity2;
                    dColor += Pixel(
                        (unsigned char)(SPECULAR * intensity2),
                        (unsigned char)(SPECULAR * intensity2),
                        (unsigned char)(SPECULAR * intensity2));
                }
            }
        }
        color += dColor;
    }

    if (!doReflections)
        return color;
    else {
        originInWorldSpace = pointHitInWorldSpace;
        const Vector3 &nrm = phongNormal;
        float c1 = -dotSYCL(rayInWorldSpace, nrm);

        Vector3 reflectedRay = rayInWorldSpace;
        reflectedRay += nrm * (2.0f * c1);
        reflectedRay.normalize();

        return color
             + Raytrace<depth + 1, doSpecular, doPhongInterp, doReflections, doShadows, true>(
                   originInWorldSpace, reflectedRay, avoidSelf,
                   scene, eyePosInWorldSpace, lightPosInWorldSpace) * REFLECTIONS_RATE;
    }
}

// Stop the compile-time recursion at MAX_RAY_DEPTH level.
#define STOP_RECURSION(a, b, c, d, e)                                                   \
template <>                                                                             \
inline Pixel Raytrace<MAX_RAY_DEPTH, a, b, c, d, e>(                                    \
    Vector3 originInWorldSpace, Vector3 rayInWorldSpace, int avoidSelf,                 \
    const SceneView &scene,                                                             \
    const Vector3 *eyePosInWorldSpace, Vector3 *lightPosInWorldSpace)             \
{                                                                                       \
    (void)originInWorldSpace; (void)rayInWorldSpace; (void)avoidSelf;                   \
    (void)scene; (void)eyePosInWorldSpace; (void)lightPosInWorldSpace;                  \
    return Pixel(0.f, 0.f, 0.f);                                                        \
}

#define f false
#define t true
STOP_RECURSION(f, f, f, f, f)
STOP_RECURSION(f, f, f, f, t)
STOP_RECURSION(f, f, f, t, f)
STOP_RECURSION(f, f, f, t, t)
STOP_RECURSION(f, f, t, f, f)
STOP_RECURSION(f, f, t, f, t)
STOP_RECURSION(f, f, t, t, f)
STOP_RECURSION(f, f, t, t, t)
STOP_RECURSION(f, t, f, f, f)
STOP_RECURSION(f, t, f, f, t)
STOP_RECURSION(f, t, f, t, f)
STOP_RECURSION(f, t, f, t, t)
STOP_RECURSION(f, t, t, f, f)
STOP_RECURSION(f, t, t, f, t)
STOP_RECURSION(f, t, t, t, f)
STOP_RECURSION(f, t, t, t, t)
STOP_RECURSION(t, f, f, f, f)
STOP_RECURSION(t, f, f, f, t)
STOP_RECURSION(t, f, f, t, f)
STOP_RECURSION(t, f, f, t, t)
STOP_RECURSION(t, f, t, f, f)
STOP_RECURSION(t, f, t, f, t)
STOP_RECURSION(t, f, t, t, f)
STOP_RECURSION(t, f, t, t, t)
STOP_RECURSION(t, t, f, f, f)
STOP_RECURSION(t, t, f, f, t)
STOP_RECURSION(t, t, f, t, f)
STOP_RECURSION(t, t, f, t, t)
STOP_RECURSION(t, t, t, f, f)
STOP_RECURSION(t, t, t, f, t)
STOP_RECURSION(t, t, t, t, f)
STOP_RECURSION(t, t, t, t, t)
#undef f
#undef t

// Main SYCL kernel, templated to support each of the desired features:
//
// - using specular lights or not
// - doing Phong normal interpolation or not
// - doing reflections or not
// - doing shadows or not
// - doing anti-alias or not
//
template <bool doSpecular, bool doPhongInterp, bool doReflections, bool doShadows, bool antialias>
struct RaycastKernel {
    int *pixels;
    const Matrix3 *worldToCameraSpace;
    SceneView scene;
    const Vector3 *eyePosInWorldSpace;
    Vector3 *lightPosInWorldSpace;
    const unsigned *mortonTable;

    void operator()(sycl::nd_item<1> item) const
    {
        unsigned int idx = item.get_global_id(0);
        if (idx >= (unsigned)(MAXX * MAXY))
            return;

        int x = int(mortonTable[idx] & 0xFFFF);
        int y = int((mortonTable[idx] & 0xFFFF0000) >> 16);

        Pixel finalColor(0, 0, 0);
        int pixelsTraced = 1;
        if (antialias)
            pixelsTraced = 4;

        while (pixelsTraced--) {
            coord xx = (coord)x;
            coord yy = (coord)y;

            if (antialias) {
                // nudge in a cross pattern around the pixel center
                xx += 0.25f - .5f * (pixelsTraced & 1);
                yy += 0.25f - .5f * ((pixelsTraced & 2) >> 1);
            }
            coord lx = coord((MAXY / 2) - yy) / SCREEN_DIST;
            coord ly = coord(xx - (MAXX / 2)) / SCREEN_DIST;
            coord lz = 1.0f;
            Vector3 rayInCameraSpace(lx, ly, lz);
            rayInCameraSpace.normalize();

            Vector3 originInWorldSpace = *eyePosInWorldSpace;

            Vector3 rayInWorldSpace = worldToCameraSpace->_row1 * rayInCameraSpace._x;
            rayInWorldSpace += worldToCameraSpace->_row2 * rayInCameraSpace._y;
            rayInWorldSpace += worldToCameraSpace->_row3 * rayInCameraSpace._z;
            rayInWorldSpace.normalize();

            finalColor += Raytrace<0, doSpecular, doPhongInterp, doReflections, doShadows, true>(
                originInWorldSpace, rayInWorldSpace, -1,
                scene,
                eyePosInWorldSpace, lightPosInWorldSpace);
        }
        if (antialias)
            finalColor /= 4.f;
        if (finalColor._r > 255.0f) finalColor._r = 255.0f;
        if (finalColor._g > 255.0f) finalColor._g = 255.0f;
        if (finalColor._b > 255.0f) finalColor._b = 255.0f;

        int color = getColor(finalColor);
        pixels[y * MAXX + x] = color;
    }
};

template <bool a, bool b, bool c, bool d, bool e>
inline void launchRaycast(
    sycl::queue &q, int n, int *pixels, const Matrix3 *worldToCameraSpace,
    const SceneView &scene, const Vector3 *eye, Vector3 *light,
    const unsigned *morton)
{
    const size_t local = THREADS_PER_BLOCK;
    const size_t global = ((size_t)n + local - 1) / local * local;
    q.parallel_for<RaycastKernel<a, b, c, d, e>>(
        sycl::nd_range<1>(sycl::range<1>(global), sycl::range<1>(local)),
        RaycastKernel<a, b, c, d, e>{pixels, worldToCameraSpace, scene, eye, light, morton});
}

void Raytrace(
    sycl::queue &q,
    Matrix3 *worldToCameraSpace,
    Vertex *vertices, Triangle *triangles, float *triangleIntersectionData,
    int *triIdxList, float *bvhLimits, int *bvhIndexesOrTrilists,
    Vector3 *eyePosInWorldSpace, Vector3 *lightPosInWorldSpace,
    unsigned *mortonTable,
    int *pixels)
{
    SceneView scene;
    scene.vertices = reinterpret_cast<const float4 *>(vertices);
    scene.triangles = triangles;
    scene.triIntersection = reinterpret_cast<const float4 *>(triangleIntersectionData);
    scene.triIdxList = triIdxList;
    scene.bvhLimits = reinterpret_cast<const float2 *>(bvhLimits);
    scene.bvhIndexes = reinterpret_cast<const uint4 *>(bvhIndexesOrTrilists);
    scene.verticesNo = g_verticesNo;
    scene.trianglesNo = g_trianglesNo;

    if (g_bUsePoints) {
        q.memset(pixels, 0x40, MAXX * MAXY * sizeof(unsigned)); // Clear all pixels to ambient
        const size_t local = THREADS_PER_BLOCK;
        const size_t global = ((size_t)g_verticesNo + local - 1) / local * local;
        q.parallel_for<PointsKernel>(
            sycl::nd_range<1>(sycl::range<1>(global), sycl::range<1>(local)),
            PointsKernel{pixels, worldToCameraSpace, eyePosInWorldSpace,
                         reinterpret_cast<const float4 *>(vertices), g_verticesNo});
    } else {
        const int blockPixels = MAXX * MAXY;

#define PAINT(bDoSpecular, bDoPhongInterp, bDoReflections, bDoShadows, bDoAntialias)        \
        launchRaycast<bDoSpecular, bDoPhongInterp, bDoReflections, bDoShadows, bDoAntialias>( \
            q, blockPixels, pixels, worldToCameraSpace, scene,                             \
            eyePosInWorldSpace, lightPosInWorldSpace, mortonTable);

        if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, false, false, false, false)
        } else if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, false, false, false, true)
        } else if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, false, false, true, false)
        } else if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, false, false, true, true)
        } else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, false, true, false, false)
        } else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, false, true, false, true)
        } else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, false, true, true, false)
        } else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, false, true, true, true)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, true, false, false, false)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, true, false, false, true)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, true, false, true, false)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, true, false, true, true)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, true, true, false, false)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, true, true, false, true)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(false, true, true, true, false)
        } else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(false, true, true, true, true)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, false, false, false, false)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, false, false, false, true)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, false, false, true, false)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, false, false, true, true)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, false, true, false, false)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, false, true, false, true)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, false, true, true, false)
        } else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, false, true, true, true)
        } else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, true, false, false, false)
        } else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, true, false, false, true)
        } else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, true, false, true, false)
        } else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, true, false, true, true)
        } else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, true, true, false, false)
        } else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, true, true, false, true)
        } else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
            PAINT(true, true, true, true, false)
        } else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
            PAINT(true, true, true, true, true)
        }
#undef PAINT
    }

    // Wait for the kernel and report any asynchronous SYCL errors.
    q.wait_and_throw();
}
