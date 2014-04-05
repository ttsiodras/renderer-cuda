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

#ifndef __algebra_h__
#define __algebra_h__

#include "Types.h"

struct Matrix3 {
    Vector3 _row1, _row2, _row3;

    Matrix3(const Vector3& row1, const Vector3& row2, const Vector3& row3)
	:
	_row1(row1), _row2(row2), _row3(row3) {}

    Matrix3()
	:
	_row1(Vector3()), _row2(Vector3()), _row3(Vector3()) {}

    Vector3 multiplyRightWith(const Vector3& r) const;
};

Vector3 cross(const Vector3&, const Vector3&);
coord dot(const Vector3&, const Vector3&);

#endif
