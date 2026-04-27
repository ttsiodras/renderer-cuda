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

// #include "config.h"

#ifdef _WIN32
#include <windows.h>
#include <GL/glew.h>
#include <sstream>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/glew.h>
#include <GL/gl.h>
#endif
#include <GL/glut.h>

#include <iostream>

#include <math.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include <SDL.h>

// ROCm/HIP headers
#include <hip/hip_runtime.h>

#define DEFINE_GLOBALS
#include "hiprenderer_globals.h"

#include "Clock.h"
#include "Loader.h"
#include "Utility.h"

#include "hiprenderer.h"

#ifndef M_PI
#define M_PI 3.14156265
#endif

using namespace std;

void create_texture(GLuint* tex)
{
    // Create test pixels (solid color to verify texture works)
    unsigned char *testPixels = (unsigned char*)malloc(MAXX*MAXY*4);
    for(unsigned i = 0; i < MAXX*MAXY; i++) {
        testPixels[i*4+0] = 128;  // R
        testPixels[i*4+1] = 64;   // G
        testPixels[i*4+2] = 32;   // B
        testPixels[i*4+3] = 255;  // A
    }
    
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MAXX, MAXY, 0, GL_RGBA, GL_UNSIGNED_BYTE, testPixels);
    
    free(testPixels);
    
    // Check for OpenGL errors
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "OpenGL error creating texture: %d\n", err);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
}

void destroy_texture(GLuint* tex)
{
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, tex);
}

void ModeDescription()
{
    string result;
    result += "Points(F4):"; result += g_bUsePoints?"ON,":"OFF,";
    result += " Specular(F5):"; result += g_bUseSpecular?"ON,":"OFF,";
    result += " NrmIntrp(F6):"; result += g_bUsePhongInterp?"ON,":"OFF,";
    result += " Reflect(F7):"; result += g_bUseReflections?"ON,":"OFF,";
    result += " Shadows(F8):"; result += g_bUseShadows?"ON,":"OFF,";
    result += " Antialias(F9):"; result += g_bUseAntialiasing?"ON":"OFF";
    SDL_WM_SetCaption(result.c_str(), result.c_str());
}

bool g_benchmark = false;

int main(int argc, char *argv[])
{
    bool bench = false;
    int benchFrames = 0;
    bool debugDump = false;

    glutInit(&argc,argv);

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "hipRenderer 2.x (ROCm/HIP)"
#endif

#ifndef __TIMESTAMP__
#define __TIMESTAMP__ "Unknown"
#endif
    if (argc < 2)
	panic("%s\nBuilt on: %s\nUsage: %s [-b frames] [-d] <filenameOf3dObject.ply>\n",
	    PACKAGE_STRING, __TIMESTAMP__, argv[0]);

    printf("%s\nBuilt on: %s\n", PACKAGE_STRING, __TIMESTAMP__);

    // Parse arguments
    int argIdx = 1;
    while(argIdx < argc) {
        if (!strcmp(argv[argIdx], "-b")) {
            bench = true;
            benchFrames = atoi(argv[argIdx+1]);
            argIdx += 2;
        } else if (!strcmp(argv[argIdx], "-d")) {
            debugDump = true;
            argIdx++;
        } else {
            break;
        }
    }

    float maxi = load_object(argv[argc-1]);

    UpdateBoundingVolumeHierarchy(argv[argc-1]);

    if ( SDL_Init(SDL_INIT_VIDEO) < 0 )
        panic("Couldn't initialize SDL: %s\n", SDL_GetError());
    atexit(SDL_Quit);
    if (!SDL_SetVideoMode( MAXX, MAXY, 0, SDL_OPENGL))
        panic("Couldn't set video mode: %s\n", SDL_GetError());
    SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);

    cout << "Loading extensions: " << glewGetErrorString(glewInit()) << endl;
    if (!glewIsSupported( "GL_VERSION_1_5 GL_ARB_pixel_buffer_object" ))
	panic("Missing GL_ARB_pixel_buffer_object\n");

    glViewport(0, 0, MAXX, MAXY);
    glClearColor(0.3f, 0.3f, 0.3f, 0.5f);
    glEnable(GL_TEXTURE_2D);
    glLoadIdentity();

    create_texture(&tex);

    Vertex *hipVertices;
    Triangle *hipTriangles;

    float *pVerticesData = (float*)malloc(g_verticesNo*8*sizeof(float));
    for(unsigned f=0; f<g_verticesNo; f++) {
	pVerticesData[f*8+0]=g_vertices[f]._x;
	pVerticesData[f*8+1]=g_vertices[f]._y;
	pVerticesData[f*8+2]=g_vertices[f]._z;
	pVerticesData[f*8+3]=g_vertices[f]._ambientOcclusionCoeff;
	pVerticesData[f*8+4]=g_vertices[f]._normal._x;
	pVerticesData[f*8+5]=g_vertices[f]._normal._y;
	pVerticesData[f*8+6]=g_vertices[f]._normal._z;
	pVerticesData[f*8+7]=0.f;
    }
    SAFE( hipMalloc((void**)&hipVertices, g_verticesNo*8*sizeof(float)) );
    SAFE( hipMemcpy(hipVertices, pVerticesData, g_verticesNo*8*sizeof(float), hipMemcpyHostToDevice) );

    float *pTrianglesIntersectionData = (float *)malloc(g_trianglesNo*20*sizeof(float));
    for(unsigned e=0; e<g_trianglesNo; e++) {
	pTrianglesIntersectionData[20*e+0]  = g_triangles[e]._center._x;
	pTrianglesIntersectionData[20*e+1]  = g_triangles[e]._center._y;
	pTrianglesIntersectionData[20*e+2]  = g_triangles[e]._center._z;
	pTrianglesIntersectionData[20*e+3]  = g_triangles[e]._twoSided?1.0f:0.0f;
	pTrianglesIntersectionData[20*e+4]  = g_triangles[e]._normal._x;
	pTrianglesIntersectionData[20*e+5]  = g_triangles[e]._normal._y;
	pTrianglesIntersectionData[20*e+6]  = g_triangles[e]._normal._z;
	pTrianglesIntersectionData[20*e+7]  = g_triangles[e]._d;
	pTrianglesIntersectionData[20*e+8]  = g_triangles[e]._e1._x;
	pTrianglesIntersectionData[20*e+9]  = g_triangles[e]._e1._y;
	pTrianglesIntersectionData[20*e+10] = g_triangles[e]._e1._z;
	pTrianglesIntersectionData[20*e+11] = g_triangles[e]._d1;
	pTrianglesIntersectionData[20*e+12] = g_triangles[e]._e2._x;
	pTrianglesIntersectionData[20*e+13] = g_triangles[e]._e2._y;
	pTrianglesIntersectionData[20*e+14] = g_triangles[e]._e2._z;
	pTrianglesIntersectionData[20*e+15] = g_triangles[e]._d2;
	pTrianglesIntersectionData[20*e+16] = g_triangles[e]._e3._x;
	pTrianglesIntersectionData[20*e+17] = g_triangles[e]._e3._y;
	pTrianglesIntersectionData[20*e+18] = g_triangles[e]._e3._z;
	pTrianglesIntersectionData[20*e+19] = g_triangles[e]._d3;
    }
    float *hipTriangleIntersectionData;
    SAFE( hipMalloc((void**)&hipTriangleIntersectionData, g_trianglesNo*20*sizeof(float)) );
    SAFE( hipMemcpy(hipTriangleIntersectionData, pTrianglesIntersectionData, g_trianglesNo*20*sizeof(float), hipMemcpyHostToDevice) );

    SAFE( hipMalloc((void**)&hipTriangles, g_trianglesNo*sizeof(Triangle)) );
    SAFE( hipMemcpy(hipTriangles, g_triangles, g_trianglesNo*sizeof(Triangle), hipMemcpyHostToDevice) );
    setHipConstants();

    float angle1=0.0f;
    float angle2=0.0f;
    float angle3=45.0f*M_PI/180.f;
    const coord EyeDistanceFactor = 4.0;
    const coord LightDistanceFactor = 4.0;

    Vector3 light(LightDistanceFactor*maxi, LightDistanceFactor*maxi, LightDistanceFactor*maxi);
    Vector3 *pLight = &light;

    int framesDrawn = 0;
    bool autoRotate = true;

    Keyboard keys;
    Vector3 eye(maxi*EyeDistanceFactor, 0.0, 0.0);
    Vector3 lookat(eye._x + 1.0f*cos(angle2)*cos(angle1),
		 eye._y + 1.0f*cos(angle2)*sin(angle1),
		 eye._z + 1.0f*sin(angle2));

    Camera sony(eye, lookat);

    Clock watch;

#define DEGREES_TO_RADIANS(x) ((coord)((x)*M_PI/180.0))
    coord dAngle = DEGREES_TO_RADIANS(0.3f);

    keys.poll();

    Matrix3 *hipSony = NULL;
    SAFE( hipMalloc((void**)&hipSony, sizeof(sony._mv)) );
    Vector3 *hipEye = NULL;
    SAFE( hipMalloc((void**)&hipEye, sizeof(Vector3)) );
    Vector3 *hipLightInWorldSpace = NULL;
    SAFE( hipMalloc((void**)&hipLightInWorldSpace, sizeof(Vector3)) );

    int *hipTriIdxList = NULL;
    SAFE( hipMalloc((void**)&hipTriIdxList, g_triIndexListNo*sizeof(int)) );
    SAFE( hipMemcpy(hipTriIdxList, g_triIndexList, g_triIndexListNo*sizeof(int), hipMemcpyHostToDevice) );

    float *pLimits = (float *)malloc(g_pCFBVH_No*6*sizeof(float));
    for(unsigned h=0; h<g_pCFBVH_No; h++) {
	pLimits[6*h+0] = g_pCFBVH[h]._bottom._x;
	pLimits[6*h+1] = g_pCFBVH[h]._top._x;
	pLimits[6*h+2] = g_pCFBVH[h]._bottom._y;
	pLimits[6*h+3] = g_pCFBVH[h]._top._y;
	pLimits[6*h+4] = g_pCFBVH[h]._bottom._z;
	pLimits[6*h+5] = g_pCFBVH[h]._top._z;
    }
    float *hipBVHlimits = NULL;
    SAFE( hipMalloc((void**)&hipBVHlimits, g_pCFBVH_No*6*sizeof(float)) );
    SAFE( hipMemcpy(hipBVHlimits, pLimits, g_pCFBVH_No*6*sizeof(float), hipMemcpyHostToDevice) );

    int *pIndexesOrTrilists = (int *)malloc(g_pCFBVH_No*4*sizeof(unsigned));
    for(unsigned g=0; g<g_pCFBVH_No; g++) {
	pIndexesOrTrilists[4*g+0] = g_pCFBVH[g].u.leaf._count;
	pIndexesOrTrilists[4*g+1] = g_pCFBVH[g].u.inner._idxRight;
	pIndexesOrTrilists[4*g+2] = g_pCFBVH[g].u.inner._idxLeft;
	pIndexesOrTrilists[4*g+3] = g_pCFBVH[g].u.leaf._startIndexInTriIndexList;
    }
    int *hipBVHindexesOrTrilists = NULL;
    SAFE( hipMalloc((void**)&hipBVHindexesOrTrilists, g_pCFBVH_No*4*sizeof(unsigned)) );
    SAFE( hipMemcpy(hipBVHindexesOrTrilists, pIndexesOrTrilists, g_pCFBVH_No*4*sizeof(unsigned), hipMemcpyHostToDevice) );

    unsigned *hipMortonTable = NULL;
    SAFE( hipMalloc((void**)&hipMortonTable, MAXX*MAXY*sizeof(unsigned)) );
    unsigned *pMortonTable = (unsigned *)malloc(MAXX*MAXY*sizeof(unsigned));
    unsigned maxDim = MAXX;
    if (maxDim<MAXY) maxDim = MAXY;
    maxDim--;
    maxDim |= maxDim >> 1;
    maxDim |= maxDim >> 2;
    maxDim |= maxDim >> 4;
    maxDim |= maxDim >> 8;
    maxDim |= maxDim >> 16;
    maxDim++;
    int ofs = 0;
    for(unsigned work=0; work<maxDim*maxDim; work++) {
	unsigned topBit = 0x80000000;
	unsigned w = work;
	int x=0, y=0;
	for(unsigned i=0; i<32; i+=2) {
	    y = (y<<1) | ((w&topBit)?1:0);
	    w<<=1;
	    x = (x<<1) | ((w&topBit)?1:0);
	    w<<=1;
	}
	if (x>=0 && y>=0 && x<MAXX && y<MAXY) {
	    unsigned value = x | (y<<16);
	    pMortonTable[ofs++] = value;
	    if (ofs == MAXX*MAXY)
		break;
	}
    }
    SAFE( hipMemcpy(hipMortonTable, pMortonTable, ofs*sizeof(unsigned), hipMemcpyHostToDevice) );

    pLight->_x = LightDistanceFactor*maxi*cos(angle3);
    pLight->_y = LightDistanceFactor*maxi*sin(angle3);

    g_benchmark = bench;

    ModeDescription();
    watch.reset();
    
    // Allocate device and host memory for pixel buffer
    int *hipPixels = NULL;
    int *hostPixels = NULL;
    unsigned char *rgbaPixels = NULL;
    SAFE( hipMalloc((void**)&hipPixels, MAXX*MAXY*sizeof(int)) );
    hostPixels = (int*)malloc(MAXX*MAXY*sizeof(int));
    rgbaPixels = (unsigned char*)malloc(MAXX*MAXY*4);

    while(!keys.Abort()) {
	framesDrawn++;
	if (bench && framesDrawn>benchFrames)
	    break;
	if (!bench) {
	    if (keys.Left())
		angle1-=dAngle;
	    if (keys.Right())
		angle1+=dAngle;
	    if (keys.Up())
		angle2=min(angle2+dAngle, DEGREES_TO_RADIANS(89.0f));
	    if (keys.Down())
		angle2=max(angle2-dAngle, DEGREES_TO_RADIANS(-89.0f));
	    if (keys.Forward() || keys.Backward()) {
		Vector3 fromEyeToLookat(lookat);
		fromEyeToLookat -= eye;
		if (autoRotate)
		    fromEyeToLookat *= 0.05f;
		else
		    fromEyeToLookat *= 0.05f*maxi;
		if (keys.Forward())
		    eye += fromEyeToLookat;
		else
		    eye -= fromEyeToLookat;
	    }
	    if (keys.isF4()) {
		watch.reset(); framesDrawn = 1;
		while (keys.isF4()) keys.poll(); g_bUsePoints       = !g_bUsePoints;       ModeDescription(); }
	    if (keys.isF5()) {
		watch.reset(); framesDrawn = 1;
		while (keys.isF5()) keys.poll(); g_bUseSpecular     = !g_bUseSpecular;     ModeDescription(); }
	    if (keys.isF6()) {
		watch.reset(); framesDrawn = 1;
		while (keys.isF6()) keys.poll(); g_bUsePhongInterp  = !g_bUsePhongInterp;  ModeDescription(); }
	    if (keys.isF7()) {
		watch.reset(); framesDrawn = 1;
		while (keys.isF7()) keys.poll(); g_bUseReflections  = !g_bUseReflections;  ModeDescription(); }
	    if (keys.isF8()) {
		watch.reset(); framesDrawn = 1;
		while (keys.isF8()) keys.poll(); g_bUseShadows      = !g_bUseShadows;      ModeDescription(); }
	    if (keys.isF9()) {
		watch.reset(); framesDrawn = 1;
		while (keys.isF9()) keys.poll(); g_bUseAntialiasing = !g_bUseAntialiasing; ModeDescription(); }

	    if (keys.isS() || keys.isF() || keys.isE() || keys.isD()) {
		Vector3 eyeToLookatPoint = lookat;
		eyeToLookatPoint -= eye;
		eyeToLookatPoint.normalize();
		Vector3 zenith(0., 0., 1.);
		Vector3 rightAxis = cross(eyeToLookatPoint, zenith);
		rightAxis.normalize();
		Vector3 upAxis = cross(rightAxis, eyeToLookatPoint);
		upAxis.normalize();
		if (keys.isS()) { rightAxis *= 0.05f*maxi; eye -= rightAxis; }
		if (keys.isF()) { rightAxis *= 0.05f*maxi; eye += rightAxis; }
		if (keys.isD()) { upAxis *= 0.05f*maxi; eye -= upAxis; }
		if (keys.isE()) { upAxis *= 0.05f*maxi; eye += upAxis; }
	    }
	    if (keys.isR()) {
		while(keys.isR())
		    keys.poll();
		autoRotate = !autoRotate;
		if (!autoRotate) {
		    Vector3 eyeToAxes = eye;
		    eyeToAxes.normalize();
		    angle2 = asin(-eyeToAxes._z);
		    angle1 = (eye._y<0)?acos(eyeToAxes._x/cos(angle2)):-acos(eyeToAxes._x/cos(angle2));
		} else {
		    angle1 = -angle1;
		    angle2 = -angle2;
		}
		watch.reset();
		framesDrawn = 1;
	    }
	    if (keys.Light()) {
		angle3 += 4*dAngle;
		pLight->_x = LightDistanceFactor*maxi*cos(angle3);
		pLight->_y = LightDistanceFactor*maxi*sin(angle3);
	    }
	    if (keys.Light2()) {
		angle3 -= 4*dAngle;
		pLight->_x = LightDistanceFactor*maxi*cos(angle3);
		pLight->_y = LightDistanceFactor*maxi*sin(angle3);
	    }
	    if (keys.isH()) {
		while(keys.isH()) keys.poll();
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_2D);
		glColor3f(1.f, 1.f, 1.f);
		const char *help =
		    "KEYS: (available only when not benchmarking)\n"
		    "\n"
		    "- Hit 'R' to stop/start auto-rotation of the camera around the object.\n"
		    "- Fly using the cursor keys,A,Z - and rotate the light with W and Q.\n"
		    "- S and F are 'strafe' left/right\n"
		    "- E and D are 'strafe' up/down\n"
		    "  (strafe keys don't work in auto-rotation mode).\n"
		    "- F4 toggles points mode\n"
		    "- F5 toggles specular lighting\n"
		    "- F6 toggles phong normal interpolation\n"
		    "- F7 toggles reflections\n"
		    "- F8 toggles shadows\n"
		    "- F9 toggles anti-aliasing\n"
		    "- ESC quits.\n\n"
		    "Now press H to get out of this help screen\n";

		glRasterPos2f(-0.95, 0.9);
		int lines = 0;
		for(const char *p=help; *p; p++) {
		    if (*p == '\n')
			glRasterPos2f(-0.95, 0.9 - ((12+3)*2.f/MAXY)*lines++);
		    else
			glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);
		}
		SDL_GL_SwapBuffers();
		while(!keys.isH() && !keys.Abort()) keys.poll();
		while(keys.isH() || keys.Abort()) keys.poll();
		framesDrawn = 1;
		watch.reset();
	    }
	}
	if (!autoRotate) {
	    lookat._x = eye._x - 1.0f*cos(angle2)*cos(angle1);
	    lookat._y = eye._y + 1.0f*cos(angle2)*sin(angle1);
	    lookat._z = eye._z + 1.0f*sin(angle2);
	} else {
	    angle1-=dAngle;
	    lookat._x = 0;
	    lookat._y = 0;
	    lookat._z = 0;
	    coord distance = sqrt(eye._x*eye._x + eye._y*eye._y + eye._z*eye._z);
	    eye._x = distance*cos(angle2)*cos(angle1);
	    eye._y = distance*cos(angle2)*sin(angle1);
	    eye._z = distance*sin(angle2);
	}

	sony.set(eye, lookat);

	SAFE( hipMemcpy(hipEye, (Vector3*)&sony, sizeof(Vector3), hipMemcpyHostToDevice) );
	SAFE( hipMemcpy(hipLightInWorldSpace, pLight, sizeof(Vector3), hipMemcpyHostToDevice) );
	SAFE( hipMemcpy(hipSony, &sony._mv, sizeof(sony._mv), hipMemcpyHostToDevice) );

	// Render to device memory
	HipRender(
	    hipSony,
	    hipVertices, hipTriangles, hipTriangleIntersectionData,
	    hipTriIdxList, hipBVHlimits, hipBVHindexesOrTrilists,
	    hipEye, hipLightInWorldSpace,
	    hipMortonTable,
	    hipPixels);

	// Copy rendered pixels back to host
	SAFE( hipMemcpy(hostPixels, hipPixels, MAXX*MAXY*sizeof(int), hipMemcpyDeviceToHost) );

	// Convert BGR int format to RGBA byte format for OpenGL (no flip)
	for(unsigned i = 0; i < MAXX*MAXY; i++) {
		int pixel = hostPixels[i];
		rgbaPixels[i*4+0] = (pixel & 0xFF);           // Red
		rgbaPixels[i*4+1] = ((pixel >> 8) & 0xFF);    // Green
		rgbaPixels[i*4+2] = ((pixel >> 16) & 0xFF);   // Blue
		rgbaPixels[i*4+3] = 255;                      // Alpha
	}

	// Upload to OpenGL texture
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MAXX, MAXY, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
	
	// Check for OpenGL errors
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		fprintf(stderr, "OpenGL error uploading texture: %d\n", err);
	}
	
	glBindTexture(GL_TEXTURE_2D, 0);

	// Debug: save frame to PPM file
	if (debugDump && framesDrawn <= 5) {
		char filename[256];
		snprintf(filename, sizeof(filename), "frame_%04d.ppm", framesDrawn);
		FILE *fp = fopen(filename, "wb");
		if (fp) {
			fprintf(fp, "P6\n%d %d\n255\n", MAXX, MAXY);
			for(unsigned i = 0; i < MAXX*MAXY; i++) {
				fputc(rgbaPixels[i*4+0], fp);  // R
				fputc(rgbaPixels[i*4+1], fp);  // G
				fputc(rgbaPixels[i*4+2], fp);  // B
			}
			fclose(fp);
			printf("Saved %s\n", filename);
		}
	}

	glClear(GL_COLOR_BUFFER_BIT);
	
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
	glEnd();
	glBindTexture(GL_TEXTURE_2D, 0);

	if (!g_benchmark) {
		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_2D);
		glColor3f(1.f, 1.f, 1.f);
		glRasterPos2f(-0.95, 0.9);
		const char *help = "Press H for help";
		for(unsigned o=0;o<strlen(help); o++)
		    glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, help[o]);
	}
	SDL_GL_SwapBuffers();

	keys.poll();

	dAngle += (DEGREES_TO_RADIANS(9.0f/(framesDrawn/(watch.readMS()/1000.0f)))-dAngle)/15.0f;
    }
    framesDrawn--;

#ifdef _WIN32
    stringstream speed;
    speed << "Rendering " << framesDrawn << " frames in " << (watch.readMS()/1000.0) << " seconds. (" << framesDrawn/(watch.readMS()/1000.0) << " fps)\n";
    MessageBoxA(0, (LPCSTR) speed.str().c_str(), (const char *)"Speed of rendering", MB_OK);
#else
    cout << "Rendering " << framesDrawn << " frames in ";
    cout << (watch.readMS()/1000.0) << " seconds. (";
    cout << framesDrawn/(watch.readMS()/1000.0) << " fps)\n";
#endif

    SAFE(hipFree(hipMortonTable));
    SAFE(hipFree(hipBVHindexesOrTrilists));
    SAFE(hipFree(hipBVHlimits));
    SAFE(hipFree(hipTriIdxList));
    SAFE(hipFree(hipLightInWorldSpace));
    SAFE(hipFree(hipEye));
    SAFE(hipFree(hipSony));
    SAFE(hipFree(hipTriangles));
    SAFE(hipFree(hipTriangleIntersectionData));
    SAFE(hipFree(hipVertices));
    SAFE(hipFree(hipPixels));
    free(hostPixels);
    free(rgbaPixels);
    destroy_texture(&tex);

    SAFE(hipDeviceSynchronize());

    return 0;
}
