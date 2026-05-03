

#ifndef TRILERP_H
#define TRILERP_H

#ifdef WIN32
#include <windows.h> 
#endif

#include <glad/glad.h>
#include "vec3d.h"
#include "qt/qt_fltk_stubs.h"
#include <stdio.h>
#include <vector>

//#include "GL/gl.h"
#include <string>
Vec3D trilerp(Vec3D p, Vec3D ***cube, int side);


enum LUTWIDGETTYPES{LUTWIDGET_DELETE=0,LUTWIDGET_AUTOLOAD};
class lutParamInfo{
	public:
	lutParamInfo(){};
	lutParamInfo(int plutIndex,const char* pLutName, LUTWIDGETTYPES pType){
		lutIndex=plutIndex;
		lutName=pLutName;
		type=pType; //will control what to do with the LUT, delete it, autoload it.
	};
	int lutIndex;
	std::string lutName;
	LUTWIDGETTYPES type;
};

class CubeLUT{
public:
CubeLUT();
CubeLUT(int size);
~CubeLUT();
bool autoload;
float findMaximum(const char *filename, Fl_Progress *pprogress=NULL);
int load(const char *filename, float normal=2.048, Fl_Progress *pprogress=NULL,bool invert=false, int type=BASELIGHT3DCUBE);
int create3DTexture();
int create1DTexture();

Vec3D valueAt(Vec3D p);
Vec3D valueAt(double x, double y, double z);
Vec3D valueAtT(float x, float y, float z);
void set(int x, int y, int z, Vec3D value);
void draw( float scale=10, float rotX=45, float rotY=45, float rotZ=45);
void drawSkewed(float scale=10, float rotX=45, float rotY=45, float rotZ=45);
void freeResources();
std::string getNameNoPath();
char filename[250];
int getTypeFromExtension(const char* filename);
int fromBits; //used if the lut is a downscaling lut, if from and to are different, then apply it before and load the image in original depth. Downscaling luts should always be applied as the first lut if more than one is going to be applied. 
int toBits;
int type;
int size;

public:
enum LUTTYPES{BASELIGHT3DCUBE=0,JEFECHECK1D, IMAGELUT2D};
float maximum1DValue;
float lut1D[1024];
//Vec3D cube[16][16][16];
std::vector< std::vector< std::vector<Vec3D> > > cube;
void allocateCube(int pSize);
GLuint texture3D; //the 3D openGL texture representation of the cube.
GLuint texture1D; //the 1D openGL texture representation of the LUT.

GLuint getTextureID(); //returns either the 3D or 1D texture ID, depending on the type.
std::string md5Hash;
char* name;

Fl_Progress *progress;

protected:

private:
int createDisplayList(int skewed=0);
int displayListIsSkewed;
GLuint cubeDisplayList;
};

CubeLUT mergeLUTs(std::vector<int> lutList, int resolution=16);
int createCanonicCubeImage(const char *fileName="/tmp/canonicCubeImage.tga");
#endif
