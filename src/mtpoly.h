/***************************************************************************
 *   Copyright (C) 2006 by Daniel Gollas Gilman   *
 *   dgollas@ollin.com.mx   *
 *   Description: Header file for xxxxxx.cpp. 
 *                                                                                    
 ***************************************************************************/
#ifndef MTPOLY_H
#define MTPOLY_H

#include "vec3d.h"
#include <vector>



/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class MtPoly
{
public:
  MtPoly();
  ~MtPoly();

  std::vector<Vec3D> points;
  void addPoint(Vec3D, int index=-1);
  void addPoint(float x, float y, float z, int index=-1);
  void insertPoint();
  int deletePoint(int index);
  void deleteSelected();
  int movePoint(int index, Vec3D pos);
  int movePoint(int index, float x, float y, float z);
  int moveSelected(float x, float y, float z);
  void draw(const char *name="");
  void drawForPointSelect();
  void drawForLineSelect();
  bool polyAltered;
 int selectedPoint;
};

bool isPointInPoly(Vec3D p, MtPoly poly);

#endif
