/***************************************************************************
 *   Copyright (C) 2006 by Daniel Gollas Gilman   *
 *   dgollas@ollin.com.mx   *
 *   Description: Source file for xxxxxx.h
 *                                                                                    
 ***************************************************************************/
#include "mtpoly.h"
#include "UIConstants.h"
#include "glew.h"
#include <FL/gl.h>
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#  include <GL/glu.h>
#endif
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>


#define MIN(x,y) (x < y ? x : y)
#define MAX(x,y) (x > y ? x : y)

bool isPointInPoly(Vec3D p, MtPoly poly)
{
  int counter = 0;
  int i;
  double xinters;
  Vec3D p1,p2;

  p1 = poly.points[0];
  for (i=1;i<=poly.points.size();i++)
  {
    p2 = poly.points[i % poly.points.size()];
    if (p.y > MIN(p1.y,p2.y))
    {
      if (p.y <= MAX(p1.y,p2.y))
      {
        if (p.x <= MAX(p1.x,p2.x))
        {
          if (p1.y != p2.y)
          {
            xinters = (p.y-p1.y)*(p2.x-p1.x)/(p2.y-p1.y)+p1.x;
            if (p1.x == p2.x || p.x <= xinters)
              counter++;
          }
        }
      }
    }
    p1 = p2;
  }

  if (counter % 2 == 0)
    return(false);
  else
    return(true);
}


MtPoly::MtPoly()
{
  selectedPoint=-1;
}


MtPoly::~MtPoly()
{}

void MtPoly::addPoint(Vec3D pos, int index)
{}

void MtPoly::insertPoint(){
	vector<Vec3D>::iterator p=points.begin() + selectedPoint +1 ;
	points.insert(p,points[selectedPoint]+(points[(selectedPoint+1)%points.size()]-points[selectedPoint])*0.5);
}

void MtPoly::addPoint(float x, float y, float z, int index)
{
  points.push_back(Vec3D(x,y,z));
}

int MtPoly::deletePoint(int index)
{

  return 1;
}

int MtPoly::movePoint(int index,Vec3D pos)
{

  return 1;
}


int MtPoly::movePoint(int index,float x, float y, float z)
{

  return 1;
}


void MtPoly::deleteSelected()
{

  vector<Vec3D>::iterator it=points.begin();


  glColor3f(1,1,0);
  glPointSize(8);
  int i=0;
  for(it=points.begin();it<points.end();it++)
  {

    if(i==selectedPoint)
      points.erase(it);
    i++;

  }




}

int MtPoly::moveSelected(float x, float y, float z)
{
  points[selectedPoint].x=x;
  points[selectedPoint].y=y;
  points[selectedPoint].z=z;
  polyAltered=true;
  return 0;
}

void MtPoly::draw(const char *name)
{
  //printf("drawing\n");
  int lineMode=GL_LINE_LOOP;
  
  vector<Vec3D>::iterator it=points.begin();

  glBegin(lineMode);
  for(it;it<points.end();it++)
  {
    glVertex3f(it->x,-it->y,it->z);
  }
  glEnd();

  if(true)
  {
    glColor3f(1,1,0);
    glPointSize(8);
    int i=0;
    glBegin(GL_POINTS);
    for(it=points.begin();it<points.end();it++)
    {
      if(i==selectedPoint)
        glColor3f(1,0,0);
      else
        glColor3f(1,1,0);
      glVertex3f(it->x,-it->y,it->z);
      ++i;
    }
  }
  glEnd();
  
  if(points.size()>0){
  glColor3f(0.2,0.2,1);
   gl_font(FL_COURIER,12);
   gl_draw("Area Of Interest", (float)points[0].x+5, (float)-points[0].y+5);
  }
}

void MtPoly::drawForPointSelect()
{

  vector<Vec3D>::iterator it=points.begin();


  glColor3f(1,1,0);
  glPointSize(8);
  int i=0;
  for(it=points.begin();it<points.end();it++)
  {
    glPushName(i);
    glBegin(GL_POINTS);
    glVertex3f(it->x,-it->y,it->z);
    glEnd();
    i++;
    glPopName();
  }




}

void MtPoly::drawForLineSelect()
{
  //printf("drawing\n");
  int lineMode=GL_LINE_LOOP;

  glLineWidth(2);
  glColor3f(0.8,0.8,0.8);

  vector<Vec3D>::iterator it=points.begin();

  glBegin(lineMode);
  for(it;it<points.end();it++)
  {
    glVertex3f(it->x,-it->y,it->z);
  }
  glEnd();


}

