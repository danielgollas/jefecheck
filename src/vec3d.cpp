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
#include "vec3d.h"
#include <math.h>






inline float vectorMag(const Vec3D &a){return sqrt(a.x*a.x+a.y*a.y+a.z+a.z);}
inline Vec3D crossProduct(const Vec3D &a, const Vec3D &b){return Vec3D(a.y*b.z-a.z*b.y , a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);}

// inline Vec3D operator *(float k, const Vec3D &v){return Vec3D(k*v.x,k*v.y,k*v.z);}
