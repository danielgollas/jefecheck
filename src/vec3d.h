/***************************************************************************
 *   Copyright (C) 2006 by Daniel Gollas Gilman   *
 *   dgollas@ollin.com.mx   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef VEC3D_H
#define VEC3D_H

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

class Vec3D;

class Vec3D{

public:

double x;
double y;
double z;

Vec3D(): x(0),y(0),z(0){};
Vec3D(const Vec3D &v): x(v.x), y(v.y), z(v.z){};
Vec3D(double px, double py, double pz) : x(px),y(py),z(pz){};

Vec3D operator +(const Vec3D &a) const {return Vec3D(x+a.x,y+a.y,z+a.z);};
Vec3D operator -(const Vec3D &a) const {return Vec3D(x-a.x,y-a.y,z-a.z);};

Vec3D operator *(const float a) const {return Vec3D(x*a, y*a, z*a);};
Vec3D operator /(const float a)const {return Vec3D(x/a, y/a, z/a);};

Vec3D operator -() const {return Vec3D(-x,-y,-z);};


};

inline float vectorMag(const Vec3D &a);
inline Vec3D crossProduct(const Vec3D &a, const Vec3D &b);
inline Vec3D operator *(float k, const Vec3D &v){return Vec3D(k*v.x,k*v.y,k*v.z);}

#endif
