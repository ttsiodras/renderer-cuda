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

#ifdef _WIN32
#include <windows.h>
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif
#include <GL/glut.h>

#include <SDL.h>

#include <cfloat>

// HIP headers (ROCm equivalent of CUDA)
#include <hip/hip_runtime.h>
#include <hip/hip_vector_types.h>

#include "Types.h"
#include "Base3d.h"
#include "Camera.h"

#include "hiprenderer_globals.h"
#include "hiprenderer.h"

/////////////////////////////////
// Raytracing configuration
#define THREADS_PER_BLOCK   64

// What depth to stop reflections and refractions?
#define MAX_RAY_DEPTH	    2

// Ray intersections of a distance <=NUDGE_FACTOR (from the origin) don't count
#define NUDGE_FACTOR     1e-5f

// How much the reflected color contributes to the overall
#define REFLECTIONS_RATE 0.375f

//////////////////////////////
// Enable ambient occlusion?
//#define AMBIENT_OCCLUSION
// How many ambient rays to spawn per ray intersection?
#define AMBIENT_SAMPLES  32
// How close to check for ambient occlusion?
#define AMBIENT_RANGE    0.15f

__constant__ unsigned VERTICES;
__constant__ unsigned TRIANGLES;

// Textures for vertices, triangles and BVH data
// (see HipRender() below, as well as main() to see the data setup process)
// Using HIP texture object API
// Store texture object handles in constant memory for device access
__constant__ hipTextureObject_t g_triIdxListTexture_const;
__constant__ hipTextureObject_t g_pCFBVHlimitsTexture_const;
__constant__ hipTextureObject_t g_pCFBVHindexesOrTrilistsTexture_const;
__constant__ hipTextureObject_t g_verticesTexture_const;
__constant__ hipTextureObject_t g_trianglesTexture_const;

// Host-side texture object variables
hipTextureObject_t g_triIdxListTexture = 0;
hipTextureObject_t g_pCFBVHlimitsTexture = 0;
hipTextureObject_t g_pCFBVHindexesOrTrilistsTexture = 0;
hipTextureObject_t g_verticesTexture = 0;
hipTextureObject_t g_trianglesTexture = 0;

// Utility functions

// HIP dot product
__device__ coord dotHIP(const Vector3& l, const Vector3& r)
{
    return l._x*r._x +l._y*r._y +l._z*r._z;
}

__device__ coord dotHIP(const float4& l, const Vector3& r)
{
    return l.x*r._x +l.y*r._y +l.z*r._z;
}

__device__ coord dotHIP(const Vector3& l, const float4& r)
{
    return l._x*r.x +l._y*r.y +l._z*r.z;
}

// HIP cross
__device__ Vector3 crossHIP(const Vector3& l, const Vector3& r)
{
    coord x,y,z;
    const coord &aax=l._x;
    const coord &aay=l._y;
    const coord &aaz=l._z;
    const coord &bbx=r._x;
    const coord &bby=r._y;
    const coord &bbz=r._z;
    x=aay*bbz-bby*aaz;
    y=bbx*aaz-aax*bbz;
    z=aax*bby-aay*bbx;
    return Vector3(x,y,z);
}

// HIP distance of two points
__device__ coord distanceHIP(const Vector3& a, const Vector3& b)
{
    coord dx=a._x - b._x;
    coord dy=a._y - b._y;
    coord dz=a._z - b._z;
    return sqrt(dx*dx + dy*dy + dz*dz);
}

// Sometime you just want to compare, so no sqrt is needed
__device__ coord distancesqHIP(const Vector3& a, const Vector3& b)
{
    coord dx=a._x - b._x;
    coord dy=a._y - b._y;
    coord dz=a._z - b._z;
    return dx*dx + dy*dy + dz*dz;
}

// Matrix3x3 multipled by Vector3
__device__ Vector3 multiplyRightWith(const Matrix3& mv, const Vector3& r)
{
    coord xnew = mv._row1._x*r._x + mv._row1._y*r._y + mv._row1._z*r._z;
    coord ynew = mv._row2._x*r._x + mv._row2._y*r._y + mv._row2._z*r._z;
    coord znew = mv._row3._x*r._x + mv._row3._y*r._y + mv._row3._z*r._z;
    return Vector3(xnew, ynew, znew);
}

// Transform Vector3 to any space, given Matrix3 and origin
__device__ Vector3 inline TransformToSomeSpace(Vector3 point, Matrix3 *mv, Vector3 *origin)
{
    point -= *origin;
    return multiplyRightWith(*mv, point);
}

// After transformation in camera space, project and plot (used for point rendering)
#define CLIPPLANEDISTANCE 0.2f

__device__ void inline ProjectAndPlot(const Vector3& xformed, int *pixels, int defaultColor=0x00FFFFFF )
{
    if (xformed._z>CLIPPLANEDISTANCE) {
	int x = (int)(MAXX/2.f  + FOV * xformed._y/xformed._z);
	int y = (int)(MAXY/2.f - FOV * xformed._x/xformed._z);
	if (y>=0.f && y<(int)MAXY && x>=0.f && x<(int)MAXX)
	    pixels[y*MAXX + x] = defaultColor;
    }
}

////////////////////////////////////////
// Rendering kernel for MODE_POINTS
////////////////////////////////////////

__global__ void CoreLoopVertices(int *pixels, Matrix3 *hipWorldToCameraSpace, Vector3 *eye)
{
    unsigned idx = blockIdx.x*blockDim.x + threadIdx.x;

    if (idx >= VERTICES)
	return;

    // Simple projection and ploting of a white point per vertex

    // Plot projected coordinates (on screen)
    Vector3 v(tex1Dfetch<float4>(g_verticesTexture_const, 2*idx));
    ProjectAndPlot(
	TransformToSomeSpace(v, hipWorldToCameraSpace, eye),
	pixels);
}

//////////////////////////////////////////////
// Rendering kernel for MODE_POINTSHIDDEN
//////////////////////////////////////////////

// Create OpenGL BGR value for assignment in PBO buffer
__device__ int getColor(Pixel& p)
{
    return (((unsigned)p._b) << 16) | (((unsigned)p._g) << 8) | (((unsigned)p._r));
}

__global__ void CoreLoopTriangles(int *pixels, Matrix3 *hipWorldToCameraSpace, Triangle *pTriangles, Vector3 *eye)
{
    unsigned idx = blockIdx.x*blockDim.x + threadIdx.x;

    if (idx >= TRIANGLES)
	return;

    // First check if the triangle is visible from where we stand
    // (closed objects only)

    float4 center = tex1Dfetch<float4>(g_trianglesTexture_const, 5*idx);
    float4 normal = tex1Dfetch<float4>(g_trianglesTexture_const, 5*idx+1);

    Vector3 triToEye = *eye;
    triToEye -= center;
    // Normally we would normalize, but since we just need the sign
    // of the dot product (to determine if it facing us or not)...
    //triToEye.normalize();
    //if (!pTriangles[idx]._twoSided && dotHIP(triToEye, pTriangles[idx]._normal)<0.f)
    if (center.w == 0.f && dotHIP(triToEye, normal)<0.f)
	return;

    int color = getColor(pTriangles[idx]._colorf);

    // For each of the 3 vertices of triangle j of object i,
    // transform to camera space, project and plot them
    Vector3 v1(tex1Dfetch<float4>(g_verticesTexture_const, 2*pTriangles[idx]._idx1));
    Vector3 v2(tex1Dfetch<float4>(g_verticesTexture_const, 2*pTriangles[idx]._idx2));
    Vector3 v3(tex1Dfetch<float4>(g_verticesTexture_const, 2*pTriangles[idx]._idx3));
    ProjectAndPlot( TransformToSomeSpace(v1, hipWorldToCameraSpace, eye), pixels, color);
    ProjectAndPlot( TransformToSomeSpace(v2, hipWorldToCameraSpace, eye), pixels, color);
    ProjectAndPlot( TransformToSomeSpace(v3, hipWorldToCameraSpace, eye), pixels, color);
}
///////////////////////////////////////////////
// Raytracing modes
///////////////////////////////////////////////

// Helper function, that checks whether a ray intersects a bbox
__device__ bool RayIntersectsBox(
    const Vector3& originInWorldSpace, const Vector3& rayInWorldSpace, int boxIdx)
{
    coord Tnear, Tfar;
    Tnear = -FLT_MAX;
    Tfar = FLT_MAX;

    float2 limits;

#define CHECK_NEAR_AND_FAR_INTERSECTION(c)							    \
    if (rayInWorldSpace._ ## c == 0.f) {							    \
	if (originInWorldSpace._##c < limits.x) return false;					    \
	if (originInWorldSpace._##c > limits.y) return false;					    \
    } else {											    \
	coord T1 = (limits.x - originInWorldSpace._##c)/rayInWorldSpace._##c;			    \
	coord T2 = (limits.y - originInWorldSpace._##c)/rayInWorldSpace._##c;			    \
	if (T1>T2) { coord tmp=T1; T1=T2; T2=tmp; }						    \
	if (T1 > Tnear) Tnear = T1;								    \
	if (T2 < Tfar)  Tfar = T2;								    \
	if (Tnear > Tfar)									    \
	    return false;									    \
	if (Tfar < 0.f)										    \
	    return false;									    \
    }

    limits = tex1Dfetch<float2>(g_pCFBVHlimitsTexture_const, 3*boxIdx);
    CHECK_NEAR_AND_FAR_INTERSECTION(x)
    limits = tex1Dfetch<float2>(g_pCFBVHlimitsTexture_const, 3*boxIdx+1);
    CHECK_NEAR_AND_FAR_INTERSECTION(y)
    limits = tex1Dfetch<float2>(g_pCFBVHlimitsTexture_const, 3*boxIdx+2);
    CHECK_NEAR_AND_FAR_INTERSECTION(z)

    return true;
}

template <bool stopAtfirstRayHit, bool doCulling>
__device__ bool BVH_IntersectTriangles(
    const Vector3& origin, const Vector3& ray, unsigned avoidSelf,
    int& pBestTriIdx,
    Vector3& pointHitInWorldSpace,
    coord& kAB, coord& kBC, coord& kCA)
{
    pBestTriIdx = -1;
    coord bestTriDist;

    Vector3& lightPos = pointHitInWorldSpace;

    if (stopAtfirstRayHit)
	bestTriDist = distancesqHIP(origin, lightPos);
    else
	bestTriDist = FLT_MAX;

    int stack[BVH_STACK_SIZE];
    int stackIdx = 0;
    stack[stackIdx++] = 0;
    while(stackIdx) {
	int boxIdx = stack[stackIdx-1];
	stackIdx--;

	uint4 data = tex1Dfetch<uint4>(g_pCFBVHindexesOrTrilistsTexture_const, boxIdx);

	if (!(data.x & 0x80000000)) {
	    if (RayIntersectsBox(origin, ray, boxIdx)) {
		stack[stackIdx++] = data.y;
		stack[stackIdx++] = data.z;
		if(stackIdx>BVH_STACK_SIZE)
		{
		    return false;
		}
	    }
	} else {
	    for(unsigned i=data.w; i<data.w + (data.x & 0x7fffffff); i++) {
		int idx = tex1Dfetch<uint1>(g_triIdxListTexture_const, i).x;

		if ((unsigned)avoidSelf == (unsigned)idx)
		    continue;

		float4 center = tex1Dfetch<float4>(g_trianglesTexture_const, 5*idx);
		float4 normal = tex1Dfetch<float4>(g_trianglesTexture_const, 5*idx+1);

		if (doCulling && (center.w == 0.f)) {
		    Vector3 fromTriToOrigin = origin;
		    fromTriToOrigin -= center;
		    if (dotHIP(fromTriToOrigin, normal)<0)
			continue;
		}

		coord k = dotHIP(normal, ray);
		if (k == 0.0f)
		    continue;

		coord s = (normal.w - dotHIP(normal, origin))/k;
		if (s <= 0.0f)
		    continue;
		if (s <= NUDGE_FACTOR)
		    continue;
		Vector3 hit = ray*s;
		hit += origin;

		float4 ee1 = tex1Dfetch<float4>(g_trianglesTexture_const, 5*idx+2);
		coord kt1 = dotHIP(ee1, hit) - ee1.w; if (kt1<0.0f) continue;
		float4 ee2 = tex1Dfetch<float4>(g_trianglesTexture_const, 5*idx+3);
		coord kt2 = dotHIP(ee2, hit) - ee2.w; if (kt2<0.0f) continue;
		float4 ee3 = tex1Dfetch<float4>(g_trianglesTexture_const, 5*idx+4);
		coord kt3 = dotHIP(ee3, hit) - ee3.w; if (kt3<0.0f) continue;

		if (stopAtfirstRayHit) {
		    coord dist = distancesqHIP(lightPos, hit);
		    if (dist < bestTriDist)
			return true;
		} else {
		    coord hitZ = distancesqHIP(origin, hit);
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

template <int depth, bool doSpecular, bool doPhongInterp, bool doReflections, bool doShadows, bool doCulling>
__device__ Pixel Raytrace(
    Vector3 originInWorldSpace, Vector3 rayInWorldSpace, int avoidSelf,
    Triangle *pTriangles,
    Vector3 *hipEyePosInWorldSpace, Vector3 *hipLightPosInWorldSpace)
{
    int pBestTriIdx = -1;
    const Triangle *pBestTri = NULL;
    Vector3 pointHitInWorldSpace;
    coord kAB=0.f, kBC=0.f, kCA=0.f;

    if (!BVH_IntersectTriangles<false,doCulling>(
	    originInWorldSpace, rayInWorldSpace, avoidSelf,
	    pBestTriIdx, pointHitInWorldSpace, kAB, kBC, kCA))
	return Pixel(0.f,0.f,0.f);

    avoidSelf = pBestTriIdx;
    pBestTri = &pTriangles[pBestTriIdx];

    Pixel color = pBestTri->_colorf;

    Vector3 phongNormal;
    coord ABx,BCx,CAx,area;
    float4 V1;
    float4 N1;
    float4 V2;
    float4 N2;
    float4 V3;
    float4 N3;
    V1 = tex1Dfetch<float4>(g_verticesTexture_const, 2*pBestTri->_idx1);
    V2 = tex1Dfetch<float4>(g_verticesTexture_const, 2*pBestTri->_idx2);
    V3 = tex1Dfetch<float4>(g_verticesTexture_const, 2*pBestTri->_idx3);
    if (doPhongInterp) {
	N1 = tex1Dfetch<float4>(g_verticesTexture_const, 2*pBestTri->_idx1+1);
	N2 = tex1Dfetch<float4>(g_verticesTexture_const, 2*pBestTri->_idx2+1);
	N3 = tex1Dfetch<float4>(g_verticesTexture_const, 2*pBestTri->_idx3+1);
	const Vector3 bestTriA = Vector3(V1.x,V1.y,V1.z);
	const Vector3 bestTriB = Vector3(V2.x,V2.y,V2.z);
	const Vector3 bestTriC = Vector3(V3.x,V3.y,V3.z);
	const Vector3 bestTriNrmA = Vector3(N1.x,N1.y,N1.z);
	const Vector3 bestTriNrmB = Vector3(N2.x,N2.y,N2.z);
	const Vector3 bestTriNrmC = Vector3(N3.x,N3.y,N3.z);

	Vector3 AB = bestTriB; AB-= bestTriA;
	Vector3 BC = bestTriC; BC-= bestTriB;
	Vector3 crossAB_BC = crossHIP(AB, BC);
	area = crossAB_BC.length();

	ABx = kAB*distanceHIP(bestTriA, bestTriB);
	BCx = kBC*distanceHIP(bestTriB, bestTriC);
	CAx = kCA*distanceHIP(bestTriC, bestTriA);

	Vector3 phongNormalA = bestTriNrmA; phongNormalA *= BCx / area;
	Vector3 phongNormalB = bestTriNrmB; phongNormalB *= CAx / area;
	Vector3 phongNormalC = bestTriNrmC; phongNormalC *= ABx / area;

	phongNormal = phongNormalA + phongNormalB + phongNormalC;
	phongNormal.normalize();
    } else
	phongNormal = pBestTri->_normal;

    coord ambientOcclusionCoeff;
    if (doPhongInterp) {
	ambientOcclusionCoeff =
	    V1.w*BCx/area +
	    V2.w*CAx/area +
	    V3.w*ABx/area;
    } else {
	ambientOcclusionCoeff = (V1.w + V2.w + V3.w)/3.f;
    }
    coord ambientFactor = (coord) ((AMBIENT*ambientOcclusionCoeff/255.0f)/255.0f);
    color *= ambientFactor;

    Vector3& light = *hipLightPosInWorldSpace;

    Pixel dColor = Pixel();

    Vector3 pointToLight = light;
    pointToLight -= pointHitInWorldSpace;

    bool inShadow = false;

    if (doShadows) {
	coord distanceFromLightSq = pointToLight.lengthsq();

	Vector3 shadowrayInWorldSpace = pointToLight;
	shadowrayInWorldSpace /= sqrt(distanceFromLightSq);

	int pDummy;
	if (BVH_IntersectTriangles<true,doCulling>(
	    pointHitInWorldSpace, shadowrayInWorldSpace, avoidSelf,
	    pDummy, light,
	    kAB, kAB, kAB))
	{
	    inShadow = true;
	}
    }

    if (!inShadow) {
	pointToLight.normalize();

	coord intensity = dotHIP(phongNormal, pointToLight);
	if (intensity<0.f) {
	    ;
	} else {
	    Pixel diffuse = pBestTri->_colorf;
	    diffuse *= (coord) (DIFFUSE*intensity/255.f);
	    dColor += diffuse;

	    if (doSpecular) {
		Vector3 pointToCamera = *hipEyePosInWorldSpace;
		pointToCamera -= pointHitInWorldSpace;
		pointToCamera.normalize();

		Vector3 half = pointToLight;
		half += pointToCamera;
		half.normalize();

		coord intensity2 = dotHIP(half, phongNormal);
		if (intensity2>0.f) {
		    intensity2 *= intensity2;
		    intensity2 *= intensity2;
		    intensity2 *= intensity2;
		    intensity2 *= intensity2;
		    intensity2 *= intensity2;
		    dColor += Pixel(
			(unsigned char)(SPECULAR*intensity2),
			(unsigned char)(SPECULAR*intensity2),
			(unsigned char)(SPECULAR*intensity2));
		}
	    }
	}
	color += dColor;
    }

    if (!doReflections)
	return color;
    else {
	originInWorldSpace = pointHitInWorldSpace;
	const Vector3& nrm = phongNormal;
	float c1 = -dotHIP(rayInWorldSpace, nrm);

	Vector3 reflectedRay = rayInWorldSpace;
	reflectedRay += nrm*(2.0f*c1);
	reflectedRay.normalize();

	return
	    color
	    + Raytrace<depth+1, doSpecular, doPhongInterp, doReflections, doShadows, true>(
		originInWorldSpace, reflectedRay, avoidSelf,
		pTriangles,
		hipEyePosInWorldSpace, hipLightPosInWorldSpace) * REFLECTIONS_RATE;
    }
}

#define STOP_RECURSION(a,b,c,d,e)							    \
template <>										    \
__device__ Pixel Raytrace<MAX_RAY_DEPTH,a,b,c,d,e>(					    \
    Vector3 originInWorldSpace, Vector3 rayInWorldSpace, int avoidSelf,			    \
    Triangle *pTriangles,								    \
    Vector3 *hipEyePosInWorldSpace, Vector3 *hipLightPosInWorldSpace)			    \
{											    \
    (void)originInWorldSpace; (void)rayInWorldSpace; (void)avoidSelf; (void)pTriangles; (void)hipEyePosInWorldSpace; (void)hipLightPosInWorldSpace; \
    return Pixel(0.f,0.f,0.f);								    \
}

#define f false
#define t true
STOP_RECURSION(f,f,f,f,f)
STOP_RECURSION(f,f,f,f,t)
STOP_RECURSION(f,f,f,t,f)
STOP_RECURSION(f,f,f,t,t)
STOP_RECURSION(f,f,t,f,f)
STOP_RECURSION(f,f,t,f,t)
STOP_RECURSION(f,f,t,t,f)
STOP_RECURSION(f,f,t,t,t)
STOP_RECURSION(f,t,f,f,f)
STOP_RECURSION(f,t,f,f,t)
STOP_RECURSION(f,t,f,t,f)
STOP_RECURSION(f,t,f,t,t)
STOP_RECURSION(f,t,t,f,f)
STOP_RECURSION(f,t,t,f,t)
STOP_RECURSION(f,t,t,t,f)
STOP_RECURSION(f,t,t,t,t)
STOP_RECURSION(t,f,f,f,f)
STOP_RECURSION(t,f,f,f,t)
STOP_RECURSION(t,f,f,t,f)
STOP_RECURSION(t,f,f,t,t)
STOP_RECURSION(t,f,t,f,f)
STOP_RECURSION(t,f,t,f,t)
STOP_RECURSION(t,f,t,t,f)
STOP_RECURSION(t,f,t,t,t)
STOP_RECURSION(t,t,f,f,f)
STOP_RECURSION(t,t,f,f,t)
STOP_RECURSION(t,t,f,t,f)
STOP_RECURSION(t,t,f,t,t)
STOP_RECURSION(t,t,t,f,f)
STOP_RECURSION(t,t,t,f,t)
STOP_RECURSION(t,t,t,t,f)
STOP_RECURSION(t,t,t,t,t)
#undef f
#undef t

template <bool doSpecular, bool doPhongInterp, bool doReflections, bool doShadows, bool antialias>
__global__ void CoreLoopTrianglesRaycaster(
    int *pixels,
    Matrix3 *hipWorldToCameraSpace,
    Triangle *pTriangles,
    Vector3 *hipEyePosInWorldSpace, Vector3 *hipLightPosInWorldSpace,
    unsigned *hipMortonTable)
{
    unsigned int idx = blockIdx.x*blockDim.x + threadIdx.x;
    if (idx>=MAXX*MAXY)
	return;

    int x = int(hipMortonTable[idx] & 0xFFFF);
    int y = int((hipMortonTable[idx] & 0xFFFF0000)>>16);

    Pixel finalColor(0,0,0);
    int pixelsTraced = 1;
    if (antialias)
	pixelsTraced = 4;

    while(pixelsTraced--) {
	coord xx = (coord)x;
	coord yy = (coord)y;

	if (antialias) {
	    xx += 0.25f - .5f*(pixelsTraced&1);
	    yy += 0.25f - .5f*((pixelsTraced&2)>>1);
	}
	coord lx = coord((MAXY/2)-yy)/SCREEN_DIST;
	coord ly = coord(xx-(MAXX/2))/SCREEN_DIST;
	coord lz = 1.0f;
	Vector3 rayInCameraSpace(lx,ly,lz);
	rayInCameraSpace.normalize();

	Vector3 originInWorldSpace = *hipEyePosInWorldSpace;

	Vector3 rayInWorldSpace = hipWorldToCameraSpace->_row1 * rayInCameraSpace._x;
	rayInWorldSpace += hipWorldToCameraSpace->_row2 * rayInCameraSpace._y;
	rayInWorldSpace += hipWorldToCameraSpace->_row3 * rayInCameraSpace._z;
	rayInWorldSpace.normalize();

	finalColor += Raytrace<0, doSpecular, doPhongInterp, doReflections, doShadows, true>(
	    originInWorldSpace, rayInWorldSpace, -1,
	    pTriangles,
	    hipEyePosInWorldSpace, hipLightPosInWorldSpace);
    }
    if (antialias)
	finalColor /= 4.f;
    if (finalColor._r>255.0f) finalColor._r=255.0f;
    if (finalColor._g>255.0f) finalColor._g=255.0f;
    if (finalColor._b>255.0f) finalColor._b=255.0f;

    int color = getColor(finalColor);
    pixels[y*MAXX+x] = color;
}

bool g_bFirstTime = true;

void HipRender(
    Matrix3 *hipWorldToCameraSpace,
    Vertex *hipPtrVertices, Triangle *hipPtrTriangles, float *hipTriangleIntersectionData,
    int *hipTriIdxList, float *hipBVHlimits, int *hipBVHindexesOrTrilists,
    Vector3 *hipEyePosInWorldSpace, Vector3 *hipLightPosInWorldSpace,
    unsigned *hipMortonTable,
    int *hipPixels)
{
    if (g_bFirstTime) {
	g_bFirstTime = false;

	hipChannelFormatDesc channel1desc = hipCreateChannelDesc<uint1>();
	hipChannelFormatDesc channel2desc = hipCreateChannelDesc<float2>();
	hipChannelFormatDesc channel3desc = hipCreateChannelDesc<uint4>();
	hipChannelFormatDesc channel4desc = hipCreateChannelDesc<float4>();
	hipChannelFormatDesc channel5desc = hipCreateChannelDesc<float4>();

	hipTextureDesc texDesc = {};
	texDesc.addressMode[0] = hipAddressModeClamp;
	texDesc.addressMode[1] = hipAddressModeClamp;
	texDesc.addressMode[2] = hipAddressModeClamp;
	texDesc.filterMode = hipFilterModePoint;
	texDesc.readMode = hipReadModeElementType;
	texDesc.normalizedCoords = 0;
	texDesc.sRGB = 0;
	texDesc.maxAnisotropy = 0;

	hipResourceDesc resDesc1 = {};
	resDesc1.resType = hipResourceTypeLinear;
	resDesc1.res.linear.desc = channel1desc;
	resDesc1.res.linear.devPtr = hipTriIdxList;
	resDesc1.res.linear.sizeInBytes = g_triIndexListNo*sizeof(uint1);
	(void)hipCreateTextureObject(&g_triIdxListTexture, &resDesc1, &texDesc, NULL);
	(void)hipMemcpyToSymbol(g_triIdxListTexture_const, &g_triIdxListTexture, sizeof(hipTextureObject_t));

	hipResourceDesc resDesc2 = {};
	resDesc2.resType = hipResourceTypeLinear;
	resDesc2.res.linear.desc = channel2desc;
	resDesc2.res.linear.devPtr = hipBVHlimits;
	resDesc2.res.linear.sizeInBytes = g_pCFBVH_No*6*sizeof(float);
	(void)hipCreateTextureObject(&g_pCFBVHlimitsTexture, &resDesc2, &texDesc, NULL);
	(void)hipMemcpyToSymbol(g_pCFBVHlimitsTexture_const, &g_pCFBVHlimitsTexture, sizeof(hipTextureObject_t));

	hipResourceDesc resDesc3 = {};
	resDesc3.resType = hipResourceTypeLinear;
	resDesc3.res.linear.desc = channel3desc;
	resDesc3.res.linear.devPtr = hipBVHindexesOrTrilists;
	resDesc3.res.linear.sizeInBytes = g_pCFBVH_No*sizeof(uint4);
	(void)hipCreateTextureObject(&g_pCFBVHindexesOrTrilistsTexture, &resDesc3, &texDesc, NULL);
	(void)hipMemcpyToSymbol(g_pCFBVHindexesOrTrilistsTexture_const, &g_pCFBVHindexesOrTrilistsTexture, sizeof(hipTextureObject_t));

	hipResourceDesc resDesc4 = {};
	resDesc4.resType = hipResourceTypeLinear;
	resDesc4.res.linear.desc = channel4desc;
	resDesc4.res.linear.devPtr = hipPtrVertices;
	resDesc4.res.linear.sizeInBytes = g_verticesNo*8*sizeof(float);
	(void)hipCreateTextureObject(&g_verticesTexture, &resDesc4, &texDesc, NULL);
	(void)hipMemcpyToSymbol(g_verticesTexture_const, &g_verticesTexture, sizeof(hipTextureObject_t));

	hipResourceDesc resDesc5 = {};
	resDesc5.resType = hipResourceTypeLinear;
	resDesc5.res.linear.desc = channel5desc;
	resDesc5.res.linear.devPtr = hipTriangleIntersectionData;
	resDesc5.res.linear.sizeInBytes = g_trianglesNo*20*sizeof(float);
	(void)hipCreateTextureObject(&g_trianglesTexture, &resDesc5, &texDesc, NULL);
	(void)hipMemcpyToSymbol(g_trianglesTexture_const, &g_trianglesTexture, sizeof(hipTextureObject_t));
    }

    if (g_bUsePoints) {
	(void)hipMemset(hipPixels, 0x40, MAXX*MAXY*sizeof(unsigned));
	int blocksVertices = (g_verticesNo + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;
	CoreLoopVertices<<< blocksVertices, THREADS_PER_BLOCK >>>(
	    hipPixels, hipWorldToCameraSpace, hipEyePosInWorldSpace);
    } else {
	int blockPixels = (MAXY*MAXX + THREADS_PER_BLOCK - 1)/THREADS_PER_BLOCK;

	#define PAINT(bDoSpecular,bDoPhongInterp,bDoReflections,bDoShadows,bDoAntialias)				\
	    CoreLoopTrianglesRaycaster<bDoSpecular,bDoPhongInterp,bDoReflections,bDoShadows,bDoAntialias>		\
	    <<< blockPixels, THREADS_PER_BLOCK >>>(									\
		hipPixels,													\
		hipWorldToCameraSpace,											\
		hipPtrTriangles,											\
		hipEyePosInWorldSpace, hipLightPosInWorldSpace,							\
		hipMortonTable);

	if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , false , false , false , false )
	} else if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , false , false , false , true )
	} else if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , false , false , true , false )
	} else if (!g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , false , false , true , true )
	} else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , false , true , false , false )
	} else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , false , true , false , true )
	} else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , false , true , true , false )
	} else if (!g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , false , true , true , true )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , true , false , false , false )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , true , false , false , true )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , true , false , true , false )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , true , false , true , true )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , true , true , false , false )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , true , true , false , true )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( false , true , true , true , false )
	} else if (!g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( false , true , true , true , true )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , false , false , false , false )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , false , false , false , true )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , false , false , true , false )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , false , false , true , true )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , false , true , false , false )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , false , true , false , true )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , false , true , true , false )
	} else if (g_bUseSpecular && !g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , false , true , true , true )
	} else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , true , false , false , false )
	} else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , true , false , false , true )
	} else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , true , false , true , false )
	} else if (g_bUseSpecular && g_bUsePhongInterp && !g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , true , false , true , true )
	} else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , true , true , false , false )
	} else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && !g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , true , true , false , true )
	} else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && !g_bUseAntialiasing) {
	   PAINT( true , true , true , true , false )
	} else if (g_bUseSpecular && g_bUsePhongInterp && g_bUseReflections && g_bUseShadows && g_bUseAntialiasing) {
	   PAINT( true , true , true , true , true )
	}
    }

    hipError_t error = hipGetLastError();
    if(error != hipSuccess) {
	printf("HIP kernel error: %s\n", hipGetErrorString(error));
	exit(-1);
    }

    (void)hipDeviceSynchronize();
}

void setHipConstants() {
    (void)hipMemcpyToSymbol(VERTICES, &g_verticesNo, sizeof(int));
    (void)hipMemcpyToSymbol(TRIANGLES, &g_trianglesNo, sizeof(int));
}
