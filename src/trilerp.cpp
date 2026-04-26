
//this define fixes not being able to find glTexiImage3D
#define GL_GLEXT_PROTOTYPES

#include "trilerp.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "gfcTextRenderer.h"
#include <glad/glad.h>
#include "vec3d.h"
//#include <GL/gl.h>
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#  include <GL/glu.h>
#endif


#ifdef WIN32
//#include <gl/glaux.h>
//#else
//#include <GL/glext.h>
#endif
#include <math.h>
#include <stdio.h>
#include <FL/gl.h>
#include <fstream>
#include <FL/Fl.H>
#include "gfcpixelbuffer.h"
#include "gfcStructures.h"
#include "xmlParser.h"
/*
Vec3D Lerp3D(Vec3D A, Vec3D B, float D)
{
    return Vec3D(A+(B-A)*D);
}*/

/*
extern PFNGLTEXIMAGE3DPROC glTexImage3D;
glTexImage3D = (PFNGLTEXIMAGE3DPROC) wglGetProcAddress("glTexImage3D");
*/

#define Lerp3D(A,B,D) Vec3D(A+(B-A)*D)



//extern std::vector<CubeLUT> lutArray;

/*
Interpolates a value inside a uniform lattice. Each vertex in the lattice holds a three component color value stored as a Vec3D.

The 8 neighbours of the desired point (p) are found and then the values held by each one are interpolated across X, Y and Z to obtain the color value at point p.

Receives a Vec3D for the point p, a pointer to three dimensional array of Vec3Ds (Vec3D cube[][][]), the size of the side of the cube (probably 16 for 3d LUTS).

Returns a Vec3D containing the obtained interpolated value for the color.

The cube is assumed to be spaced by 1.

*/

int createCanonicCubeImage(const char *fileName) {
    GFLC_BITMAP image(GFL_RGB,64,64);
    GFLC_SAVE_PARAMS saveParams;
    GFLC_COLOR colorStrip[64*64];

    int stripCount=0;
    for (int i=0; i<16;i++) {
        for (int j=0; j<16;j++) {
            for (int k=0; k<16;k++) {
                colorStrip[stripCount]=GFLC_COLOR(i/15.0*255,j/15.0*255,k/15.0*255);
                stripCount++;
            }
        }
    }

    stripCount=0;
    for (int i=0;i<64;i++) {
        for (int j=0;j<64;j++) {
            image.setPixel(i,j,colorStrip[stripCount]);
            stripCount++;
        }
    }


    image.saveIntoFile(fileName,"tga");

    return 0;
}

Vec3D trilerp(Vec3D p, Vec3D ***cube, int side) {
    //find the deltas on x, y, and z to the neighbours.
    double deltaX, deltaY, deltaZ;

    double nX, nX1, nY, nY1, nZ, nZ1;

    //find nX and nX1
    deltaX=modf(p.x*side,&nX);
    nX<side?nX1=nX+1:nX1=nX;

    //find nY and nY1
    deltaY=modf(p.y*side,&nY);
    nY<side?nY1=nY+1:nY1=nY;

    //find nZ and nZ1
    deltaZ=modf(p.z*side,&nZ);
    nZ<side?nZ1=nZ+1:nZ1=nZ;

    //find the 8 neighbours, the closest points on the lattice to the passed point p.
    Vec3D p000(nX, nY, nZ), p100(nX1, nY, nZ), p010(nX, nY, nZ1), p110(nX1, nY, nZ1);
    Vec3D p001(nX, nY1, nZ), p101(nX1, nY1, nZ), p011(nX, nY1, nZ1), p111(nX1, nY1, nZ1);


    //do linear interpolation on each axis.
    //four points for bilinear across X
    Vec3D p00(Lerp3D(p000,p100,deltaX)); // p00 comes from p000 and p1000
    Vec3D p01(Lerp3D(p001,p101,deltaX)); //p01 comes from p001 and p101
    Vec3D p10(Lerp3D(p010, p110, deltaX)); //p10 comes from p010 and p110
    Vec3D p11(Lerp3D(p011,p111,deltaX)); //p11 comes from p011 and p111


    //two points for trilinear interpolation across Z
    Vec3D p0(Lerp3D(p00,p10,deltaZ));
    Vec3D p1(Lerp3D(p01,p11,deltaZ));

    //return last lerp across Y
    return Lerp3D(p0, p1, deltaY);
}


/*****************CUBE LUT*************************/

void CubeLUT::allocateCube(int pSize)
{
	
	for (int i=0;i<cube.size();i++)
	{
		
		for (int j=0;j<cube[i].size();j++)
		{
			
			/*for (int k=0;k<cube[i][j].size();k++)
			{
				cube[i][j][k].clear()
			}*/
			
			cube[i][j].clear();
			
		}
		cube[i].clear();
	}
	cube.clear();


	size=pSize;
	cube.resize(size);
	for (int i=0;i<size;i++)
	{
		cube[i].resize(size);
		for (int j=0;j<size;j++)
		{
			cube[i][j].resize(size);
		}
	}	
}


std::string CubeLUT::getNameNoPath() {
#ifdef win32
    if (strrchr(filename,'\\')!=NULL) {
        return strrchr(filename,'\\')+1;
    }
#else
    if (strrchr(filename,'/')!=NULL) {
        return strrchr(filename,'/')+1;
    }
#endif
    else
        return filename;
}

GLuint CubeLUT::getTextureID()
{
	switch(this->type)
	{
	case JEFECHECK1D:
		return texture1D;
		break;

	case BASELIGHT3DCUBE:
	case IMAGELUT2D:
		return texture3D;
		break;

	default:
		return -1;
	}
}

Vec3D CubeLUT::valueAtT(float x, float y, float z) {

    if (type==JEFECHECK1D) {
        /*x*size<0?x=0:x;
        y*size<0?y=0:y;
        z*size<0?z=0:z;

        x*size>size?x=1:x;
        y*size>size?y=1:y;
        z*size>size?z=1:z;*/
        //printf("1D\n");
        //return Vec3D(1,1,1);
        Vec3D returnValue(lut1D[(int)(x*(size-1))],lut1D[(int)(y*(size-1))],lut1D[(int)(z*(size-1))]);
        return returnValue;//Vec3D(lut1D[(int)(x*(size-1))],lut1D[(int)(y*(size-1))],lut1D[(int)(z*(size-1))]);
    }

    /*float deltaX, deltaY, deltaZ;
    Vec3D p(x,y,z);
    double nX, nX1, nY, nY1, nZ, nZ1;

    //find nX and nX1
    deltaX=modf(p.x*size,&nX);

    if(nX<size-1) //the value is inside the cube;
    {
        nX1=nX+1; //get the roof neighbour
    }
    else //is it is not inside the cube clip to the largest value in the cube.
    {
        nX=nX1=size-1;
    }

    //find nY and nY1
    deltaY=modf(p.y*size,&nY);
    if(nY<size-1) //the value is inside the cube;
    {
        nY1=nY+1; //get the roof neighbour
    }
    else //is it is not inside the cube clip to the largest value in the cube.
    {
        nY=nY1=size-1;
    }

    //find nZ and nZ1
    deltaZ=modf(p.z*size,&nZ);

    if(nZ<size-1) //the value is inside the cube;
    {
        nZ1=nZ+1; //get the roof neighbour
    }
    else //is it is not inside the cube clip to the largest value in the cube.
    {
        nZ=nZ1=size-1;
    }*/

    float deltaX, deltaY, deltaZ;
    Vec3D p(x,y,z);
    double nX, nX1, nY, nY1, nZ, nZ1;

    //find nX and nX1
    deltaX=modf(p.x*(size-1),&nX);

    if (nX<size-1) { //the value is inside the cube;
        nX1=nX+1; //get the roof neighbour
    } else { //is it is not inside the cube clip to the largest value in the cube.
        nX=nX1=size-1;
        deltaX=0;
    }

    //find nY and nY1
    deltaY=modf(p.y*(size-1),&nY);
    if (nY<size-1) { //the value is inside the cube;
        nY1=nY+1; //get the roof neighbour
    } else { //is it is not inside the cube clip to the largest value in the cube.
        nY=nY1=size-1;
        deltaY=0;
    }

    //find nZ and nZ1
    deltaZ=modf(p.z*(size-1),&nZ);

    if (nZ<size-1) { //the value is inside the cube;
        nZ1=nZ+1; //get the roof neighbour
    } else { //is it is not inside the cube clip to the largest value in the cube.
        nZ=nZ1=size-1;
        deltaZ=0;
    }


    //Now that we have the sub-cube we split the tetrahedron
    if (deltaX>deltaY) {
        if (deltaY>deltaZ) {
            return (1-deltaX)*cube[(int)nX][(int)nY][(int)nZ]\
                   +(deltaX-deltaY)*cube[(int)nX1][(int)nY][(int)nZ]\
                   +(deltaY-deltaZ)*cube[(int)nX1][(int)nY1][(int)nZ]\
                   +deltaZ*cube[(int)nX1][(int)nY1][(int)nZ1];
        } else {
            if (deltaX>deltaZ) {
                return (1-deltaX)*cube[(int)nX][(int)nY][(int)nZ]\
                       +(deltaX-deltaZ)*cube[(int)nX1][(int)nY][(int)nZ]\
                       +(deltaZ-deltaY)*cube[(int)nX1][(int)nY][(int)nZ1]\
                       +deltaY*cube[(int)nX1][(int)nY1][(int)nZ1];
            } else {
                return (1-deltaZ)*cube[(int)nX][(int)nY][(int)nZ]\
                       +(deltaZ-deltaX)*cube[(int)nX][(int)nY][(int)nZ1]\
                       +(deltaX-deltaY)*cube[(int)nX1][(int)nY][(int)nZ1]\
                       +deltaY*cube[(int)nX1][(int)nY1][(int)nZ1];
            }
        }
    } else {
        if (deltaZ>deltaY) {
            return (1-deltaZ)*cube[(int)nX][(int)nY][(int)nZ]\
                   +(deltaZ-deltaY)*cube[(int)nX][(int)nY][(int)nZ1]\
                   +(deltaY-deltaX)*cube[(int)nX][(int)nY1][(int)nZ1]\
                   +deltaX*cube[(int)nX1][(int)nY1][(int)nZ1];
        } else {
            if (deltaZ>deltaX) {
                return (1-deltaY)*cube[(int)nX][(int)nY][(int)nZ]\
                       +(deltaY-deltaZ)*cube[(int)nX][(int)nY1][(int)nZ]\
                       +(deltaZ-deltaX)*cube[(int)nX][(int)nY1][(int)nZ1]\
                       +deltaX*cube[(int)nX1][(int)nY1][(int)nZ1];
            } else {
                return (1-deltaY)*cube[(int)nX][(int)nY][(int)nZ]\
                       +(deltaY-deltaX)*cube[(int)nX][(int)nY1][(int)nZ]\
                       +(deltaX-deltaZ)*cube[(int)nX1][(int)nY1][(int)nZ]\
                       +deltaZ*cube[(int)nX1][(int)nY1][(int)nZ1];
            }
        }
    }

}

Vec3D CubeLUT::valueAt(double x, double y, double z) {

    if (type==JEFECHECK1D) {
        x*size<0?x=0:x;
        y*size<0?y=0:y;
        z*size<0?z=0:z;

        x*size>size?x=1:x;
        y*size>size?y=1:y;
        z*size>size?z=1:z;
        //printf("1D\n");
        return Vec3D(lut1D[(int)(x*(size-1))],lut1D[(int)(y*(size-1))],lut1D[(int)(z*(size-1))]);
    }

    float deltaX, deltaY, deltaZ;
    Vec3D p(x,y,z);
    double nX, nX1, nY, nY1, nZ, nZ1;

    //find nX and nX1
    deltaX=modf(p.x*(size-1),&nX);

    if (nX<size-1) { //the value is inside the cube;
        nX1=nX+1; //get the roof neighbour
    } else { //is it is not inside the cube clip to the largest value in the cube.
        nX=nX1=size-1;
        deltaX=0;
    }

    //find nY and nY1
    deltaY=modf(p.y*(size-1),&nY);
    if (nY<size-1) { //the value is inside the cube;
        nY1=nY+1; //get the roof neighbour
    } else { //is it is not inside the cube clip to the largest value in the cube.
        nY=nY1=size-1;
        deltaY=0;
    }

    //find nZ and nZ1
    deltaZ=modf(p.z*(size-1),&nZ);

    if (nZ<size-1) { //the value is inside the cube;
        nZ1=nZ+1; //get the roof neighbour
    } else { //is it is not inside the cube clip to the largest value in the cube.
        nZ=nZ1=size-1;
        deltaZ=0;
    }



    //find the 8 neighbours, the closest points on the lattice to the passed point p.
    /*Vec3D p000(nX, nY, nZ), p100(nX1, nY, nZ), p010(nX, nY, nZ1), p110(nX1, nY, nZ1);
    Vec3D p001(nX, nY1, nZ), p101(nX1, nY1, nZ), p011(nX, nY1, nZ1), p111(nX1, nY1, nZ1);*/

    Vec3D p000(cube[(int)nX][(int)nY][(int)nZ]), p100(cube[(int)nX1][(int)nY][(int)nZ]), p010(cube[(int)nX][(int)nY][(int)nZ1]), p110(cube[(int)nX1][(int)nY][(int)nZ1]);
    Vec3D p001(cube[(int)nX][(int)nY1][(int)nZ]), p101(cube[(int)nX1][(int)nY1][(int)nZ]), p011(cube[(int)nX][(int)nY1][(int)nZ1]), p111(cube[(int)nX1][(int)nY1][(int)nZ1]);


    //do linear interpolation on each axis.
    //four points for bilinear across X
    Vec3D p00(Lerp3D(p000,p100,deltaX)); // p00 comes from p000 and p1000
    Vec3D p01(Lerp3D(p001,p101,deltaX)); //p01 comes from p001 and p101
    Vec3D p10(Lerp3D(p010, p110, deltaX)); //p10 comes from p010 and p110
    Vec3D p11(Lerp3D(p011,p111,deltaX)); //p11 comes from p011 and p111


    //two points for trilinear interpolation across Z
    Vec3D p0(Lerp3D(p00,p10,deltaZ));
    Vec3D p1(Lerp3D(p01,p11,deltaZ));

    //return last lerp across Y
    return Lerp3D(p0, p1, deltaY);

}

CubeLUT::CubeLUT() {
    autoload=false;
    progress=NULL;
    strcpy(filename,"");
    size=0;
	texture1D=-1;
	texture3D=-1;
	this->type=-1;
	//allocateCube(0);
    /*for (int i=0;i<size;i++) {
		
        for (int j=0;j<size;j++) {
		
            for (int k=0; k<size; k++) {
		
                cube[i][j][k].x=1.0/(float)(size-1)*i;
                cube[i][j][k].y=1.0/(float)(size-1)*j;
                cube[i][j][k].z=1.0/(float)(size-1)*k;
            }
        }
    }*/
}


CubeLUT::CubeLUT(int psize) {
    autoload=false;
    progress=NULL;
    strcpy(filename,"");
    size=psize;
	texture1D=-1;
	texture3D=-1;
	this->type=-1;	
	allocateCube(size);
    /*for (int i=0;i<size;i++) {
        for (int j=0;j<size;j++) {
            for (int k=0; k<size; k++) {
                cube[i][j][k].x=1.0/(float)(size-1)*i;
                cube[i][j][k].y=1.0/(float)(size-1)*j;
                cube[i][j][k].z=1.0/(float)(size-1)*k;
            }
        }
    }*/
}

CubeLUT::~ CubeLUT() {}

int CubeLUT::createDisplayList(int skewed)
{
	
	//decide if we need to create it or not
	if (!glIsList(cubeDisplayList) || skewed!=displayListIsSkewed)
	{
		displayListIsSkewed=skewed;
		glDeleteLists(cubeDisplayList,1);
		cubeDisplayList = glGenLists(1);
		
		printf("Created new display list!!!!\n");
		glNewList(cubeDisplayList,GL_COMPILE);
	
		glBegin(GL_POINTS);
		if (skewed)
		{
			for (int i=0;i<size;i++) {
				for (int j=0;j<size;j++) {
					for (int k=0; k<size; k++) {
						glColor3f(cube[i][j][k].x,cube[i][j][k].y,cube[i][j][k].z);
						glVertex3f(cube[i][j][k].x,cube[i][j][k].y,cube[i][j][k].z);

					}
				}
			}

		}
		else
		{
			for (int i=0;i<size;i++) {
				for (int j=0;j<size;j++) {
					for (int k=0; k<size; k++) {
						glColor3f(cube[i][j][k].x,cube[i][j][k].y,cube[i][j][k].z);
						glVertex3f(i,j,k);

					}
				}
			}
		}
		
		glEnd();
		glEndList();

	}

	glCallList(cubeDisplayList);

	return 1;
}

void CubeLUT::draw(float scale, float rotX, float rotY, float rotZ) {
    char tmp[120]="";

	glEnable(GL_DEPTH_TEST);
	
    if (type==BASELIGHT3DCUBE || type ==IMAGELUT2D) {
        glPushMatrix();
        glTranslatef(size*0.5,size*0.5,size*0.5);
        glScalef(scale*15,scale*15,scale*15);
        glRotatef(rotX,0,1,0);
        glRotatef(rotY,1,0,0);
        glRotatef(rotZ,0,0,1);
        glTranslatef(-size*0.5,-size*0.5,-size*0.5);


        glPointSize(4);
		glDisable(GL_POINT_SMOOTH);
		//createDisplayList creates and runs the display if necessary.
		createDisplayList(0);
    

        //glDisable(GL_DEPTH_TEST);
        gfc_gl_font(FL_HELVETICA, 12);
        textRenderer().setColor(1.0, 1.0, 1.0, 1.0);
        sprintf(tmp,"(0,0,0)=(%f %f %f)",cube[0][0][0].x,cube[0][0][0].y,cube[0][0][0].z);
        textRenderer().draw3D(tmp, 0, 0, 0);

        sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",size-1,size-1,size-1,cube[size-1][size-1][size-1].x,cube[size-1][size-1][size-1].y,cube[size-1][size-1][size-1].z);
        textRenderer().draw3D(tmp, size-1, size-1, size-1);

        sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",size-1,0,0,cube[size-1][0][0].x,cube[size-1][0][0].y,cube[size-1][0][0].z);
        textRenderer().draw3D(tmp, size-1, 0, 0);

        sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",0,size-1,0,cube[0][size-1][0].x,cube[0][size-1][0].y,cube[0][size-1][0].z);
        textRenderer().draw3D(tmp, 0, size-1, 0);

        glPopMatrix();
       
    } 
	else {
        glPushMatrix();


        //glTranslatef(-size*0.5,0,0);
        glTranslatef(-rotX,rotY,0);
        glScalef(scale,scale,scale);
        //glRotatef(rotX,1,0,0);
        //glRotatef(rotY,0,1,0);
        //glRotatef(rotZ,0,0,1);
        //glTranslatef(-size*0.5,-size*0.5,-size*0.5);
        glLineWidth(1);
        glBegin(GL_LINES);
        glColor3f(0,0,1);
        glVertex3f(0,0,0);
        glVertex3f(0,maximum1DValue*size*1.2,0);
        glColor3f(1,0,0);
        glVertex3f(0,0,0);
        glVertex3f(size*1.2,0,0);
        glEnd();

        glPointSize(4);
        for (int i=0;i<size;i++) {
            glBegin(GL_POINTS);
            glColor3f(lut1D[i],lut1D[i],lut1D[i]);
            glVertex3f(i,lut1D[i]*size,0);
            glEnd();
        }

        //printf("Drawing 1D");sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",size-1,size-1,size-1,cube[size-1][size-1][size-1].x,cube[size-1][size-1][size-1].y,cube[size-1][size-1][size-1].z);
        //glRasterPos3f(size-1,size-1,size-1),gl_draw(tmp);



        gfc_gl_font(FL_HELVETICA, (int)(10*scale));
        textRenderer().setColor(0, 0, 1.0, 1.0);
        sprintf(tmp,"%i",(int)(maximum1DValue*size*1.2));
        /*max Y point*/
        textRenderer().draw3D(tmp, -20/scale, maximum1DValue*size*1.2, 0);

        textRenderer().setColor(1, 0, 0.0, 1.0);
        sprintf(tmp,"%i",(int)(size*1.2));
        /*max X point*/
        textRenderer().draw3D(tmp, size*1.2, -8, 0);

        textRenderer().setColor(1.0, 0, 1.0, 1.0);
        /*origin*/
        textRenderer().draw3D("(0,0)", -20/scale, -8, 0);

        //Draw Relevant curve point info

        textRenderer().setColor(1, 1, 1.0, 1.0);
        for (int i=0;i<size;i+=31) {

            sprintf(tmp,"(%i,%i)",i,(int)(lut1D[i]*size));
            textRenderer().draw3D(tmp, i, lut1D[i]*size, 0);
        }
        //make sure we draw the maximum point
        sprintf(tmp,"(%i,%i)",size-1,(int)(lut1D[size-1]*size));
        textRenderer().draw3D(tmp, size-1, lut1D[size-1]*size, 0);


        glPopMatrix();
    }
    glDisable(GL_DEPTH_TEST);
}

void CubeLUT::drawSkewed( float scale, float rotX, float rotY, float rotZ) {
    char tmp[120]="";

    glEnable(GL_DEPTH_TEST);
	//glDisable(GL_DEPTH_TEST);
    if (type==BASELIGHT3DCUBE || type ==IMAGELUT2D) {
        glPushMatrix();
        //glTranslatef(300,0,0);
        glScalef(scale*size*15,scale*size*15,scale*size*15);
        glRotatef(rotX,0,1,0); //this is all screwed up, but it's just order problems
        glRotatef(rotY,1,0,0);
        glRotatef(rotZ,0,0,1);
        //glTranslatef(-size*0.5,-size*0.5,-size*0.5);

        glPointSize(4);
        glDisable(GL_POINT_SMOOTH);
        createDisplayList(1);

        gfc_gl_font(FL_HELVETICA, 12);
        textRenderer().setColor(1.0, 1.0, 1.0, 1.0);
        sprintf(tmp,"(0,0,0)=(%f %f %f)",cube[0][0][0].x,cube[0][0][0].y,cube[0][0][0].z);
        textRenderer().draw3D(tmp, cube[0][0][0].x, cube[0][0][0].y, cube[0][0][0].z);

        sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",size-1,size-1,size-1,cube[size-1][size-1][size-1].x,cube[size-1][size-1][size-1].y,cube[size-1][size-1][size-1].z);
        textRenderer().draw3D(tmp, cube[size-1][size-1][size-1].x, cube[size-1][size-1][size-1].y, cube[size-1][size-1][size-1].z);

        sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",size-1,0,0,cube[size-1][0][0].x,cube[size-1][0][0].y,cube[size-1][0][0].z);
        textRenderer().draw3D(tmp, cube[size-1][0][0].x, cube[size-1][0][0].y, cube[size-1][0][0].z);

        sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",0,size-1,0,cube[0][size-1][0].x,cube[0][size-1][0].y,cube[0][size-1][0].z);
        textRenderer().draw3D(tmp, cube[0][size-1][0].x, cube[0][size-1][0].y, cube[0][size-1][0].z);

        glPopMatrix();
      
    } else {
        glPushMatrix();


        //glTranslatef(-size*0.5,0,0);
        glTranslatef(-rotX,rotY,0);
        glScalef(scale,scale,scale);
        glRotatef(rotZ,0,1,0);
        //glRotatef(rotX,1,0,0);
        //glRotatef(rotY,0,1,0);
        //glRotatef(rotZ,0,0,1);
        //glTranslatef(-size*0.5,-size*0.5,-size*0.5);
        glLineWidth(1);
        glBegin(GL_LINES);
        glColor3f(0,0,1);
        glVertex3f(0,0,0);
        glVertex3f(0,maximum1DValue*size*1.2,0);
        glColor3f(1,0,0);
        glVertex3f(0,0,0);
        glVertex3f(size*1.2,0,0);
        glEnd();

        glPointSize(4);
        for (int i=0;i<size;i++) {
            glBegin(GL_POINTS);
            glColor3f(lut1D[i],lut1D[i],lut1D[i]);
            glVertex3f(i,lut1D[i]*size,0);
            glEnd();
        }

        //printf("Drawing 1D");sprintf(tmp,"(%i,%i,%i)=(%f %f %f)",size-1,size-1,size-1,cube[size-1][size-1][size-1].x,cube[size-1][size-1][size-1].y,cube[size-1][size-1][size-1].z);
        //glRasterPos3f(size-1,size-1,size-1),gl_draw(tmp);



        gfc_gl_font(FL_HELVETICA, (int)(10*scale));
        textRenderer().setColor(0, 0, 1.0, 1.0);
        sprintf(tmp,"%i",(int)(maximum1DValue*size*1.2));
        /*max Y point*/
        textRenderer().draw3D(tmp, -20/scale, maximum1DValue*size*1.2, 0);

        textRenderer().setColor(1, 0, 0.0, 1.0);
        sprintf(tmp,"%i",(int)(size*1.2));
        /*max X point*/
        textRenderer().draw3D(tmp, size*1.2, -8, 0);

        textRenderer().setColor(1.0, 0, 1.0, 1.0);
        /*origin*/
        textRenderer().draw3D("(0,0)", -20/scale, -8, 0);

        //Draw Relevant curve point info

        textRenderer().setColor(1, 1, 1.0, 1.0);
        for (int i=0;i<size;i+=31) {

            sprintf(tmp,"(%i,%i)",i,(int)(lut1D[i]*size));
            textRenderer().draw3D(tmp, i, lut1D[i]*size, 0);
        }
        //make sure we draw the maximum point
        sprintf(tmp,"(%i,%i)",size-1,(int)(lut1D[size-1]*size));
        textRenderer().draw3D(tmp, size-1, lut1D[size-1]*size, 0);


        glPopMatrix();
    }
    glDisable(GL_DEPTH_TEST);
}

int CubeLUT::getTypeFromExtension(const char* pfilename) {
    char* ext=strrchr((char*)pfilename,'.')+1;

    if (strcmp(ext,"tga")==0) { //2D LUT
        return IMAGELUT2D;
    }

    if (strcmp(ext,"lut")==0 || strcmp(ext,"jlut")==0) {//1D jefecheck LUT
        return JEFECHECK1D;
    }

    if (strcmp(ext,"cub")==0 /*|| strcmp(ext,"cube")==0*/) { //Truelight 3D Cube
        return BASELIGHT3DCUBE;
    }

    return -1; //not a recognized format.


}

int CubeLUT::create1DTexture() {
//creates a 1D texture from the data it has stored in the 1D LUT array.

    //allocate the space for the 3D data.
    GLfloat tmpData[1024][3];

    //for each vec3D in the cube, assign each component to the corresponding tmpData
    for (int i=0;i<size;i++) {

        tmpData[i][0]=tmpData[i][1]=tmpData[i][2]=lut1D[i];

        //printf("1D LUT texture: %i: %f %f %f\n",i,tmpData[i][0],tmpData[i][1],tmpData[i][2]);

    }
    glPrintError();
    //create a 3D texture ID
    glEnable(GL_TEXTURE_1D);
    glActiveTexture ( GL_TEXTURE0 );
    
    //if (!texture1D)
    //    glDeleteTextures(1,&texture1D);

    glGenTextures(1,&texture1D);
    
    

    glBindTexture(GL_TEXTURE_1D,texture1D);
//     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage1D(GL_TEXTURE_1D,0,GL_RGB,size,0,GL_RGB,GL_FLOAT,tmpData);
    glPrintError();
	
	GLint theTexSize[2];
	glGetTexLevelParameteriv(GL_TEXTURE_1D,0,GL_TEXTURE_WIDTH,&theTexSize[0]);
	glGetTexLevelParameteriv(GL_TEXTURE_1D,0,GL_TEXTURE_HEIGHT,&theTexSize[1]);

	printf("1D texture created: %i x %i id=%i\n",theTexSize[0],theTexSize[1],texture1D);

    glBindTexture(GL_TEXTURE_1D,0);
    glDisable(GL_TEXTURE_1D);

    return 0;
}

void removeCRLF(char *ptr)
{
	
	for(int i = strlen(ptr)-1; i >=0; i--)
	{
		switch(ptr[i])
		{
		
		//find cr and replace with \0
		case 0x0D:
			ptr[i]=0;
			printf("removed 0x0D\n");
			break;
		//find lf and replace with \0
		case 0x0A:
			ptr[i]=0;
			printf("removed 0x0A\n");
			break;
		}
	}
}

int CubeLUT::create3DTexture() {//creates a 3D texture from the data it has stored in the cube 3d array.

    //allocate the space for the 3D data.
    //GLfloat tmpData[16][16][16][3];
	GLfloat *tmpData;
	tmpData=new GLfloat[size*size*size*3];
    //for each vec3D in the cube, assign each component to the corresponding tmpData
	int counter=0;
    for (int i=0;i<size;i++) {
        for (int j=0;j<size ;j++ ) {
            for (int k=0;k<size;k++) {
                /*tmpData[i][j][k][0]=cube[k][j][i].x;
                tmpData[i][j][k][1]=cube[k][j][i].y;
                tmpData[i][j][k][2]=cube[k][j][i].z;*/
				tmpData[counter++]=cube[k][j][i].x;
				tmpData[counter++]=cube[k][j][i].y;
				tmpData[counter++]=cube[k][j][i].z;
                //printf("Cube %i %i %i: %f %f %f\n",i,j,k,tmpData[i][j][k][0],tmpData[i][j][k][1],tmpData[i][j][k][2]);
            }
        }
    }

    //create a 3D texture ID
    glEnable(GL_TEXTURE_3D);
    //if (!texture3D)
        //glDeleteTextures(1,&texture3D);

    glGenTextures(1,&texture3D);
    
    glBindTexture(GL_TEXTURE_3D,texture3D);
//     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPrintError();
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGB,size,size,size,0,GL_RGB,GL_FLOAT,tmpData);
    
    glPrintError();
    GLint theTexSize[2];
    glGetTexLevelParameteriv(GL_TEXTURE_3D,0,GL_TEXTURE_WIDTH,&theTexSize[0]);
    glGetTexLevelParameteriv(GL_TEXTURE_3D,0,GL_TEXTURE_HEIGHT,&theTexSize[1]);
    printf("3D texture created: %i x %i id=%i\n",theTexSize[0],theTexSize[1],texture3D);

    glBindTexture(GL_TEXTURE_3D,0);
    glDisable(GL_TEXTURE_3D);
	delete [] tmpData;
    return 0;
}

int CubeLUT::load(const char *pfilename, float normal,Fl_Progress *pprogress, bool invert, int ttype) {
    std::string inputForMD5;
    std::ifstream file;
    int cubeSize=16;
    autoload=true;
    progress=pprogress;
    type=getTypeFromExtension(pfilename);
    if (type==-1) {
        if (pprogress!=NULL) {
            progress->copy_label("Not a Recognized Format!");
            progress->value(0);
            return -1;
        }
    }

    inputForMD5=GetFilenameNoPath(pfilename);
    name=stringDup(GetFilenameNoPath(pfilename).c_str());
    inputForMD5+=type;
    switch (type) {
    case IMAGELUT2D: 
		{

        //printf("*Loading 2D LUT: %s\n",GetFilenameNoPath(pfilename).c_str());
        //printf("Type=%i\n",type);
        GFLC_BITMAP image;
        GFLC_LOAD_PARAMS loadParams;

        if (image.loadFromFile(pfilename,loadParams)) {
            if (pprogress!=NULL) {
                progress->copy_label("Error opening image!");
                progress->value(0);

            }
            return -1;
        }

		//DETERMINE THE TYPE OF IMAGE LUT THIS IS, JEFECHECK LUTs ARE 64x64, 
		//NUKE CMS Cube renders are bigger (448x448 for 16x16x16 cubes (7x7pixels per entry) or more for bigger cubes, for now, support only 16size)
		switch(image.getWidth())
		{
		case 64:
			{
				printf("Loading JefeCheck Image 3D LUT\n");
				Vec3D colorStrip[64*64];
				GFLC_COLOR tmpColor;
				int stripCount=0;
				if (pprogress!=NULL) {
					progress->copy_label("Error opening file!");
					progress->maximum(4096);
				}

				for (int i=0;i<image.getWidth();i++ ) {
					for (int j=0;j<image.getHeight();j++) {
						if (pprogress!=NULL) {
							progress->copy_label("Reading Image...");
							progress->value(stripCount/2.0);

						}
						image.getPixel(i,j,tmpColor);
						colorStrip[stripCount].x=tmpColor.getRed();
						colorStrip[stripCount].y=tmpColor.getGreen();
						colorStrip[stripCount].z=tmpColor.getBlue();
						stripCount++;
					}
				}
				
				printf("stripCount=%i\n",stripCount);
				
				size=16;

				stripCount=0;
				if (pprogress!=NULL) {
					progress->copy_label("Converting to cube...");
					progress->maximum(4096);

				}
				allocateCube(size);
				for (int i=0; i<size;i++) {
					for (int j=0; j<size;j++) {
						for (int k=0; k<size;k++) {
							cube[i][j][k]=colorStrip[stripCount]/255.0;
							inputForMD5+=cube[i][j][k].x;
							inputForMD5+=cube[i][j][k].y;
							inputForMD5+=cube[i][j][k].z;
							if (pprogress!=NULL) {
								progress->copy_label("Convertig to cube...");
								progress->value(2048+stripCount/2.0);

							}
							stripCount++;
						}
					}
				}
				
			}
			break;
		case 448:
			printf("Loading 2D image LUT of size %ix%i\n",image.getWidth(),image.getHeight());
			{
				int nukePixelsPerSample=7;
				printf("Loading Nuke Image 3D LUT\n");
				//Vec3D colorStrip[64*64];
				std::vector<Vec3D> colorStrip;
				int imageSamplesPerImageSide=image.getWidth()/nukePixelsPerSample;
				colorStrip.resize(imageSamplesPerImageSide*imageSamplesPerImageSide);
				GFLC_COLOR tmpColor;
				int stripCount=0;
				if (pprogress!=NULL) {
					progress->copy_label("Error opening file!");
					progress->maximum(4096);
				}
				image.rotate(image,90);
				for (int i=0;i<image.getWidth();i+=7 ) {
					for (int j=0;j<image.getHeight();j+=7) {
						
						image.getPixel(i,j,tmpColor);
						//printf("pixel(%i,%i)\n",i,j);
						colorStrip[stripCount].x=tmpColor.getRed();
						colorStrip[stripCount].y=tmpColor.getGreen();
						colorStrip[stripCount].z=tmpColor.getBlue();
						
						if (pprogress!=NULL) {
							progress->copy_label("Reading Image...");
							progress->value(stripCount/2.0);

						}
						stripCount++;
					}
				}
				
				printf("stripCount=%i\n",stripCount);

				stripCount=0;
				if (pprogress!=NULL) {
					progress->copy_label("Converting to cube...");
					progress->maximum(4096);

				}
				for (int i=0; i<16;i++) {
					for (int j=0; j<16;j++) {
						for (int k=0; k<16;k++) {
							cube[k][j][i]=colorStrip[stripCount]/255.0;
							inputForMD5+=cube[i][j][k].x;
							inputForMD5+=cube[i][j][k].y;
							inputForMD5+=cube[i][j][k].z;
							if (pprogress!=NULL) {
								progress->copy_label("Converting to cube...");
								progress->value(2048+stripCount/2.0);

							}
							stripCount++;
						}
					}
				}
			}
		    break;
		default:
			if (pprogress!=NULL) {
				progress->copy_label("Not a Recognized Image LUT Format!");
				progress->value(0);
				return -1;
			}
		    break;
		}
				

        
#ifdef WIN32
        strcpy(filename,pfilename);
        //strcpy(filename,getNameNoPath());

#else
        strcpy(filename,pfilename);
#endif
        size=16;

        //inputForMD5+=GetFilenameNoPath(pfilename);
        //printf("Input for MD5=%s\n",inputForMD5.c_str());

        md5Hash=GetMD5Hash(inputForMD5);

        //printf("MD5HASH: %s\n",md5Hash.c_str());

        if (pprogress!=NULL) {
            progress->copy_label("Done loading image cube");
            progress->value(4096);
        }

        if (pprogress!=NULL) {
            progress->copy_label("Creating HW LUT");
            progress->value(4096);
        }
        create3DTexture();

        if (pprogress!=NULL) {
            char tmp[256];
            sprintf(tmp,"Done Creating HW LUT: id%i",texture3D);
            progress->copy_label(tmp);
            progress->value(4096);
        }
        
        return 0;
    }
    break;

    case JEFECHECK1D: {
        using namespace std;
        //printf("*********\nREAD 1D LUT\n*********\n");
        //printf("Loading 1D LUT %s...",GetFilenameNoPath(pfilename).c_str());
        ifstream fs(pfilename);
        if (!fs) {

            if (pprogress!=NULL) {
                progress->copy_label("Error opening file!");
                progress->value(0);
                return -1;
            }
        } else {
            if (pprogress!=NULL) {
                progress->copy_label("File Opened");
                progress->value(0);
            }
        }



        //printf("done\n");
        //printf("Reading 1DLUT file\n");
#define MAXCHARS 40

        char line[40];
        //get header
        fs.getline(line,40);
        //printf("Header: %s\n",line);
        if (strstr(line,"#JefeCheck LUT Header v1.0")==0) {
            printf("File is not a JefeCheck LUT Header\n");
            if (pprogress!=NULL) {
                progress->copy_label("File is not a JefeCheck LUT Header!");
                progress->value(0);
                return -1;
            }
            return -1;
        }
        fs.getline(line,MAXCHARS);
        size=atoi(line);
        inputForMD5+=size;
        //printf("LUT File has %i entries\n",size);
        char tmpProgressLabel[250];
        if (pprogress!=NULL) {
            sprintf(tmpProgressLabel,"LUT File has %i entries\n",size);
            progress->copy_label("tmpProgressLabel");
            progress->value(0);

        }
        //printf("Allocating memory (%i Bytes)...",sizeof(LUT1DPair)*size);
        //lut1D = new float[size];
        //printf("done\n");
        fs.getline(line,MAXCHARS);
        fromBits=atoi(line);
        inputForMD5+=fromBits;
        //printf("Input bit depth: %i\n",fromBits);

        fs.getline(line,MAXCHARS);
        toBits=atoi(line);
        inputForMD5+=toBits;
        //printf("Output bit depth: %i\n",toBits);
        //printf("Reading %i entries...",size);
        if (pprogress!=NULL) {
            sprintf(tmpProgressLabel,"Reading %i entries\n",size);
            progress->copy_label(tmpProgressLabel);
            progress->maximum(1);
            progress->value(0);
        }
        int startOfNumbers=0;
        maximum1DValue=0;
        for (int i=0;i<size;i++) {
            fs.getline(line,MAXCHARS);
            startOfNumbers=strcspn(line,"0123456789");
            lut1D[i]=atof(strtok(&line[startOfNumbers]," "));
            inputForMD5+=lut1D[i];
            if (lut1D[i]>maximum1DValue)
                maximum1DValue=lut1D[i];
            //lut1D[i]=ceil(lut1D[i])-lut1D[i]<lut1D[i]-floor(lut1D[i])?ceil(lut1D[i]):floor(lut1D[i]);
            #ifndef __APPLE__
            if (pprogress!=NULL && i%10==0) {
                progress->value(i/(float)size);
                app().processEvents();
            }
            #endif;
            //lut[i].input=i;
            //printf("%f * ",lut1D[i]);
        }

        md5Hash=GetMD5Hash(inputForMD5);
        //printf("MD5HASH: %s\n",md5Hash.c_str());
        strcpy(filename,pfilename);
        //printf("ok\n");

        create1DTexture();
        if (pprogress!=NULL) {
            sprintf(tmpProgressLabel,"Done. Loaded 1D LUT (%i entries)",size);
            progress->copy_label(tmpProgressLabel);

            progress->value(1);
        }
        
        return 0;
        //printf("*********\n");
#undef MAXCHARS

    }
    break;

    case BASELIGHT3DCUBE:
        if (pprogress!=NULL) {
            progress->copy_label("Opening file");
            progress->value(0);
            app().processEvents();
        }
        printf("Opening Cube %s...",pfilename);
        file.open(pfilename);
        if (!file.is_open()) {
            if (pprogress!=NULL) {
                progress->copy_label("Error opening file!");
                progress->value(0);
                return -1;
            }
        } else {
            if (pprogress!=NULL) {
                progress->copy_label("File Opened");
                progress->value(0);
            }
        }
        //printf("done\n");
        char line[150]="";
        Vec3D *values;
        //find the lut size;
        //find the header
        if (!file.eof()) {

            file.getline(line,150);
			removeCRLF(line);
            printf("line: %s\n",line);
            if (std::string(line)!=std::string("# Truelight Cube v2.0")) {
                printf("File is not a Truelight Cube v2.0...cube not loaded\n");
                if (pprogress!=NULL) {
                    progress->copy_label("File is not a Truelight Cube v2.0...cube not loaded");
                    progress->value(0);
                    app().processEvents();
                }
                return 1;
            } else {
                //printf("Found Truelight Cube v2.0 header, finding cube dimensions\n");
            }

        }

        bool foundDims=false;
        while (!file.eof()) {

            file.getline(line,150);
            if (strstr(line,"# width")!=NULL) {
                //printf("line: %s\n",line);
                foundDims=true;

                break;
            }
        }

        if (!foundDims) {
            printf("Could not find cube dimensions...cube not loaded\n");
            if (pprogress!=NULL) {
                progress->copy_label("Could not find cube dimensions...cube not loaded");
                progress->value(0);
                app().processEvents();
            }
            return 2;
        } else {
            //we found cube dimensions, find them and store them
            //printf("line: %s\n",line);
            char *startOfNum=strpbrk(line,"1234567890");
            char *num=strtok(startOfNum," ");
            int dim=atoi(num);

            //printf("Dim found: dim=%i\n",dim);
            cubeSize=dim;
            //printf("Found cube dimensions, finding cube start...\n");
        }

        bool foundCubeStart=false;
        while (!file.eof()) {
            file.getline(line,150);
            if (strstr(line,"# Cube")!=NULL) {
                foundCubeStart=true;
                break;
            }
        }

        if (!foundCubeStart) {

            printf("Could not find cube start...cube not loaded\n");
            if (pprogress!=NULL) {
                progress->copy_label("Could not find cube start...cube not loaded");
                progress->value(0);
                app().processEvents();
            }
            return 3;


        } else {
            printf("Found cube start, reading %i entries...\n",cubeSize*cubeSize*cubeSize);

            values= new Vec3D[cubeSize*cubeSize*cubeSize];
            int valueTriadCount=0;
            if (pprogress!=NULL) {
                progress->maximum(cubeSize*cubeSize*cubeSize);
                //progress->copy_label("Loading triads...");
                progress->value(0);

                app().processEvents();
            }
            while (!file.eof() && valueTriadCount<cubeSize*cubeSize*cubeSize) {

                file.getline(line,150);
                char *startOfNum=strpbrk(line,"-1234567890");
                char *num=strtok(startOfNum," ");
                //printf("Found value: %s\n",num);
                values[valueTriadCount].x=atof(num);

                num=strtok(NULL," ");
                //printf("Found value: %s\n",num);
                values[valueTriadCount].y=atof(num);

                num=strtok(NULL," ");
                //printf("Found value: %s\n",num);
                values[valueTriadCount].z=atof(num);

                //printf("triad %i: %f %f %f\n",valueTriadCount,values[valueTriadCount].x,values[valueTriadCount].y,values[valueTriadCount].z);

/*
                if (pprogress!=NULL && valueTriadCount%100==0 ) {
#ifndef _APPLE_
                    progress->value(valueTriadCount/2);

    
                        app().processEvents();
#else
 
#endif
                }*/

                valueTriadCount++;
            }
	    
            //printf("Finished reading %i triads (%i values), closing file...",cubeSize*cubeSize*cubeSize,cubeSize*cubeSize*cubeSize*3);
            file.close();
            //printf("done\n");
        }

        // printf("Normalizing to %f and storing values to cube\n",normal);
        char tmpProgressLabel[150];
        sprintf(tmpProgressLabel,"Normalizing to %f and storing...",normal);

     /*   if (pprogress!=NULL) {
            progress->copy_label(tmpProgressLabel);
            progress->value(cubeSize*cubeSize*cubeSize/2);
            progress->maximum(cubeSize*cubeSize*cubeSize);
#ifndef _APPLE_
            app().processEvents();
#endif
        }*/

        int index1D=0;
		
		allocateCube(cubeSize);
        if (!invert) { 
            for (int i=0;i<cubeSize;i++) {
				
                for (int j=0;j<cubeSize;j++) {
				
                    for (int k=0; k<cubeSize; k++) {
				
                        //printf("Accesing values index %i\n",index1D);
                        cube[k][j][i].x=values[index1D].x/normal;
                        cube[k][j][i].y=values[index1D].y/normal;
                        cube[k][j][i].z=values[index1D].z/normal;
                        inputForMD5+=cube[k][j][i].x;
                        inputForMD5+=cube[k][j][i].y;
                        inputForMD5+=cube[k][j][i].z;
                       /* if (pprogress!=NULL && i%10==0) {
                            progress->value((index1D+cubeSize*cubeSize*cubeSize)/2);
#ifndef _APPLE_
						app().processEvents();
#endif
      
                        }*/
                        index1D++;
                    }
                }
            }
        } else {
			allocateCube(cubeSize);
            for (int i=cubeSize-1;i>=0;i--) {
				
                for (int j=cubeSize-1;j>=0;j--) {
					
                    for (int k=cubeSize-1; k>=0; k--) {
					
                        //printf("Accesing values index %i\n",index1D);
                        cube[k][j][i].x=values[index1D].x/normal;
                        cube[k][j][i].y=values[index1D].y/normal;
                        cube[k][j][i].z=values[index1D].z/normal;

                        inputForMD5+=cube[k][j][i].x;
                        inputForMD5+=cube[k][j][i].y;
                        inputForMD5+=cube[k][j][i].z;
                        /*if (pprogress!=NULL && i%10==0) {
                            progress->value((index1D+cubeSize*cubeSize*cubeSize)/2);
                            app().processEvents();
                        }*/
                        index1D++;
                    }
                }
            }
        }

        md5Hash=GetMD5Hash(inputForMD5);
        //printf("MD5HASH: %s\n",md5Hash.c_str());

        if (pprogress!=NULL) {
            sprintf(tmpProgressLabel,"Done (loaded %ix%ix%i cube)",cubeSize,cubeSize,cubeSize);
            progress->copy_label(tmpProgressLabel);
            progress->value(progress->maximum());
            app().processEvents();
        }
        strcpy(filename,pfilename);
        size=cubeSize;
        create3DTexture();
        return 0;
        break;
    }
	return 1;
}

float CubeLUT::findMaximum(const char *pfilename, Fl_Progress *pprogress) {
    float maximum=0;
    std::ifstream file;
    int cubeSize=size;

    progress=pprogress;

    if (pprogress!=NULL) {
        progress->copy_label("Opening file");
        progress->value(0);
        app().processEvents();
    }
    //printf("Opening file %s...",pfilename);
    file.open(pfilename);
    if (!file.is_open()) {
        if (pprogress!=NULL) {
            progress->copy_label("Error opening file!");
            progress->value(0);
            return -1;
        }
    } else {
        if (pprogress!=NULL) {
            progress->copy_label("File Opened");
            progress->value(0);
        }
    }
    //printf("done\n");
    char line[150]="";
    //Vec3D *values;
	std::vector< Vec3D > values;
    //find the lut size;
    //find the header
    if (!file.eof()) {

        file.getline(line,150);
        //printf("line: %s\n",line);
        if (strcmp(line,"# Truelight Cube v2.0")!=0) {
            //printf("File is not a Truelight Cube v2.0...cube not loaded\n");
            if (pprogress!=NULL) {
                progress->copy_label("File is not a Truelight Cube v2.0...cube not loaded");
                progress->value(0);
                app().processEvents();
            }
            return 1;
        } else {
            //printf("Found Truelight Cube v2.0 header, finding cube dimensions\n");
        }

    }

    bool foundDims=false;
    while (!file.eof()) {

        file.getline(line,150);
        if (strstr(line,"# width")!=NULL) {
            //printf("line: %s\n",line);
            foundDims=true;

            break;
        }
    }

    if (!foundDims) {
        //printf("Could not find cube dimensions...cube not loaded\n");
        if (pprogress!=NULL) {
            //progress->copy_label("Could not find cube dimensions...cube not loaded");
            progress->value(0);
            app().processEvents();
        }
        return 2;
    } else {
        //we found cube dimensions, find them and store them
        //printf("line: %s\n",line);
        char *startOfNum=strpbrk(line,"1234567890");
        char *num=strtok(startOfNum," ");
        int dim=atoi(num);

        //printf("Dim found: dim=%i\n",dim);
        cubeSize=dim;
        //printf("Found cube dimensions, finding cube start...\n");
    }

    bool foundCubeStart=false;
    while (!file.eof()) {
        file.getline(line,150);
        if (strstr(line,"# Cube")!=NULL) {
            foundCubeStart=true;
            break;
        }
    }

    if (!foundCubeStart) {

        //printf("Could not find cube start...cube not loaded\n");
        if (pprogress!=NULL) {
            progress->copy_label("Could not find cube start...cube not loaded");
            progress->value(0);
            app().processEvents();
        }
        return 3;


    } else {
        //printf("Found cube start, reading %i entries...\n",cubeSize*cubeSize*cubeSize);

        //values=new Vec3D[cubeSize*cubeSize*cubeSize];
		values.resize(cubeSize*cubeSize*cubeSize);
        int valueTriadCount=0;
        if (pprogress!=NULL) {
            progress->maximum(cubeSize*cubeSize*cubeSize);
            progress->copy_label("Reading triads...");
            progress->value(0);
            app().processEvents();
        }

        while (!file.eof() && valueTriadCount<cubeSize*cubeSize*cubeSize) {

            file.getline(line,150);
            char *startOfNum=strpbrk(line,"-1234567890");
            char *num=strtok(startOfNum," ");
            //printf("Found value: %s\n",num);
            if (atof(num)>maximum)
                maximum=atof(num);

            num=strtok(NULL," ");
            //printf("Found value: %s\n",num);
            if (atof(num)>maximum)
                maximum=atof(num);

            num=strtok(NULL," ");
            //printf("Found value: %s\n",num);
            if (atof(num)>maximum)
                maximum=atof(num);

            //printf("triad %i: %f %f %f\n",valueTriadCount,values[valueTriadCount].x,values[valueTriadCount].y,values[valueTriadCount].z);

            if (pprogress!=NULL && valueTriadCount%10==0) {
                progress->value(valueTriadCount);
                app().processEvents();
            }

            valueTriadCount++;
        }

        // printf("Finished reading %i triads (%i values), closing file...",cubeSize*cubeSize*cubeSize,cubeSize*cubeSize*cubeSize*3);
        if (pprogress!=NULL) {
            char tmpProgressLabel[250];
            sprintf(tmpProgressLabel,"Done. Found Maximum: %f",maximum);
            progress->copy_label(tmpProgressLabel);
            progress->value(progress->maximum());
            app().processEvents();
        }
        file.close();
        //printf("done\n");
    }
    return maximum;
}

void CubeLUT::set
(int x, int y, int z, Vec3D value) {

    if (x<size && y<size && z<size && x>=0 && y>=0 && z>=0) {
        cube[x][y][z]=value;
    }

}

CubeLUT mergeLUTs(std::vector<int> lutList, int resolution) {//Takes a list of lut intexi and merges the corresponding luts into a single all transforming lut.

     CubeLUT stdCube;
     CubeLUT returnCube;
//     Vec3D interpColor;
//     int lutListSize=lutList.size();
//     for (int k=/*lut10logTo8Lin?1:*/0;k<lutListSize;k++) {
//         printf("Applying lutlist[%i]=%i: %c\n",k,lutList[k],lutArray[lutList[k]].filename);
//         for (int i=0;i<resolution;i++) {
//             for (int j=0;j<resolution;j++) {
//                 for (int w=0; w<resolution;w++) {
//                     // printf("pixel[%i,%i]\n",tmpInt,j);
// 
// 
//                     interpColor=returnCube.cube[i][j][w];
//                     //interpColor=stdCube.valueAtT(i/((float)resolution),j/((float)resolution),w/((float)resolution));
//                     //stack all luts one after another , k grows since we want the luts in order
//                     //printf("Applying lutlist[%i]=%i\n",k,lutList[k]);
//                     //std::cout.flush();
//                     //interpColor=lutArray[lutList[k]].valueAt(interpColor.x,interpColor.y,interpColor.z);
//                     printf("sample[%i,%i,%i]:%.5f %.5f %.5f...",i,j,w,interpColor.x,interpColor.y,interpColor.z);
// 
//                     interpColor=lutArray[lutList[k]].valueAt(interpColor.x,interpColor.y,interpColor.z);
// 
//                     printf("%.5f %.5f %.5f\n",interpColor.x,interpColor.y,interpColor.z);
//                     printf("Original %i %i %i: %.5f %.5f %.5f\n\n",i,j,w,lutArray[lutList[k]].cube[i][j][w].x,lutArray[lutList[k]].cube[i][j][w].y,lutArray[lutList[k]].cube[i][j][w].z);
// 
//                     returnCube.set(i,j,w,interpColor);
//                     //                 printf("%f %f %f\n",color.getRed()/255.0,color.getGreen()/255.0,color.getBlue()/255.0);
//                 }
//             }
//         }
//     }
//     printf("Done Merging Cube***************************\n");
    return returnCube;
}

void CubeLUT::freeResources() {
    glDeleteTextures(1,&texture1D);
    glDeleteTextures(1,&texture3D);
}


