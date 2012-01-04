/*
    This file is a part of the RepSnapper project.
    Copyright (C) 2011  martin.dieringer@gmx.de

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#include "infill.h"
#include "poly.h"

// #include <vmmlib/vmmlib.h> 

using namespace std; 
using namespace vmml;

vector<struct Infill::pattern> Infill::savedPatterns;

// Infill::Infill(){
//   infill.clear();
// }
Infill::Infill(CuttingPlane *plane) 
{
  this->plane = plane;
}

void Infill::clearPatterns() {
  savedPatterns.clear();
}


// fill polys with type etc.
void Infill::calcInfill(const vector<Poly> polys, InfillType type, 
			double infillDistance, double offsetDistance, double rotation)
{
  ClipperLib::Polygons patterncpolys = 
    makeInfillPattern(type,infillDistance, offsetDistance, rotation);
  calcInfill(polys, patterncpolys, offsetDistance);
}

// fill polys with fillpolys
void Infill::calcInfill(const vector<Poly> polys, const vector<Poly> fillpolys,
			double offsetDistance)
{
  calcInfill(polys, plane->getClipperPolygons(fillpolys), offsetDistance);
}

// clip infill pattern polys against polys (after NOT offsetting polys)
void Infill::calcInfill(const vector<Poly> polys, 
			const ClipperLib::Polygons patterncpolys,
			double offsetDistance)
{
  bool reverse=true;
  // // problem with offsetting is orientation so that a few polys won't get filled
  // ClipperLib::Polygons offset;
  // while (offset.size()==0){ // try to reverse poly vertices if no result
  ClipperLib::Polygons cpolys = plane->getClipperPolygons(polys,reverse);
  //   // offset by half offsetDistance
  //   ClipperLib::OffsetPolygons(cpolys, offset, -1000.*offsetDistance/2.,
  // 			     ClipperLib::jtMiter,2);
  //   reverse=!reverse;
  //   if (reverse) break;
  // }
  ClipperLib::Clipper clpr;
  clpr.AddPolygons(patterncpolys,ClipperLib::ptSubject);
  clpr.AddPolygons(cpolys,ClipperLib::ptClip);
  ClipperLib::Polygons result;
  ClipperLib::PolyFillType filltype = ClipperLib::pftNonZero; //?
  clpr.Execute(ClipperLib::ctIntersection, result, filltype, ClipperLib::pftNonZero);
  for (uint i=0;i<result.size();i++)
    {
      Poly p = Poly(plane, result[i]);
      addInfillPoly(p);
    }
}

// generate infill pattern as a vector of polygons
ClipperLib::Polygons Infill::makeInfillPattern(InfillType type, 
					       double infillDistance,
					       double offsetDistance,
					       double rotation) 
{
  this->type = type;
  //cerr << "have " << savedPatterns.size()<<" saved patterns " << endl;
  while (rotation>2*M_PI) rotation -= 2*M_PI;
  while (rotation<0) rotation += 2*M_PI;
  this->angle= rotation;
  for (uint i = 0; i < savedPatterns.size(); i++) {
    if (savedPatterns[i].type == type &&
	ABS(savedPatterns[i].distance-infillDistance) < 0.01 &&
	ABS(savedPatterns[i].angle-rotation) < 0.01 )
      {
	//cerr << "found saved pattern no " << i << endl;
	return savedPatterns[i].cpolys;
      }
  }
  Vector2d Min = plane->Min;
  Vector2d Max = plane->Max;
  Vector2d center = (Min+Max)/2.;

  // Expand the Min/Max bounding rect to account for the rotation of
  // the infill....
  // FIXME: We should just correctly cover the areas.
  Min=center+(plane->Min-center)*2;
  Max=center+(plane->Max-center)*2;

  // none found - make new:
  ClipperLib::Polygons cpolys;
  vector<Poly> polys;
  switch (type)
    {
    case SupportInfill: // stripes, but leave them as polygons
    case ParallelInfill: // stripes, make them to lines later
      {
	Poly poly(this->plane);
	for (double x=Min.x; x <Max.x; x+= 2*infillDistance) {
	  poly.addVertex(Vector2d(x,Min.y));
	  poly.addVertex(Vector2d(x+infillDistance,Min.y));
	  poly.addVertex(Vector2d(x+infillDistance,Max.y));
	  poly.addVertex(Vector2d(x+2*infillDistance,Max.y));
	}
	poly.addVertex(Vector2d(Max.x,Min.y-infillDistance));
	poly.addVertex(Vector2d(Min.x,Min.y-infillDistance));
	poly.rotate(center,rotation);
	polys.push_back(poly);
	cpolys = plane->getClipperPolygons(polys);
      }
      break;
    case LinesInfill: // lines only -- not possible
      {
	Poly poly(this->plane);
	for (double x=Min.x; x <Max.x; x+= infillDistance) {
	  poly.addVertex(Vector2d(x,Min.y));
	  poly.addVertex(Vector2d(x,Max.y));
	  poly.rotate(center,rotation);
	  polys.push_back(poly);
	}
	cpolys = plane->getClipperPolygons(polys);
	cerr << cpolys.size() << endl; 
      }
      break;
    default:
      cerr << "infill type " << type << " unknown "<< endl;
    }
  // save 
  struct pattern newPattern;
  newPattern.type=type;
  newPattern.angle=rotation;
  newPattern.distance=infillDistance;
  newPattern.cpolys=cpolys;
  savedPatterns.push_back(newPattern);
  return newPattern.cpolys;
}

void Infill::addInfillPoly(const Poly p)
{
  if (type == ParallelInfill) 
    { // make lines instead of closed polygons
      Vector2d l,rotl;
      double sina = sin(-angle);
      double cosa = cos(-angle);
      // use the lines that have the angle of this Infill
      Poly * newpoly;
      for (uint i=0; i < p.size() ; i+=1 )
  	{
  	  l = (p.getVertexCircular(i+1) - p.getVertexCircular(i));     
	  // rotate with neg. infill angle and see whether it's 90° as infill lines
	  rotl = Vector2d(l.x*cosa-l.y*sina, 
			  l.y*cosa+l.x*sina);
	  if (ABS(rotl.x) < 0.1 && ABS(rotl.y) > 0.1)
  	    {
	      newpoly = new Poly(p.getPlane());
  	      newpoly->vertices.push_back(p.getVertexCircular(i));
  	      newpoly->vertices.push_back(p.getVertexCircular(i+1));
  	      infillpolys.push_back(*newpoly);
  	    }
  	}
    }
  else
    {
      infillpolys.push_back(p);
    }
}

// not used
void Infill::getLines(vector<Vector3d> &lines) const
{
  cerr << "infill getlines" << endl;
  for (uint i=0; i<infillpolys.size(); i++)
    {
      infillpolys[i].getLines(lines);
    }
}

void Infill::printinfo() const
{ 
  cout << "Infill with " << infillpolys.size() << " polygons" <<endl;
}

