/*
    This file is a part of the RepSnapper project.
    Copyright (C) 2010  Kulitorum
    Copyright (C) 2012  martin.dieringer@gmx.de

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

#include "layer.h"
#include "poly.h"
#include "../shape.h"
#include "../render.h"

// polygons will be simplified to thickness/CLEANFACTOR
#define CLEANFACTOR 7

Layer::Layer(Layer * prevlayer, int layerno, double thick, uint skins)
  : LayerNo(layerno), thickness(thick), previous(prevlayer), skins(skins)
{
//  normalInfill = NULL;
//  fullInfill = NULL;
//  skirtInfill = NULL;
//  supportInfill = NULL;
//  decorInfill = NULL;
//  thinInfill = NULL;
  Min = Vector2d(G_MAXDOUBLE, G_MAXDOUBLE);
  Max = Vector2d(G_MINDOUBLE, G_MINDOUBLE);
}

Layer::~Layer()
{
  Clear();
}


void Layer::Clear()
{
    normalInfill.clear();
    supportInfill.clear();
    skinInfill.clear();
    decorInfill.clear();
//    bridgeInfill.clear();
  clearpolys(polygons);
  clearpolys(shellPolygons);
  clearpolys(fillPolygons);
  clearpolys(thinPolygons);
  clearpolys(fullFillPolygons);
  clearpolys(bridgePolygons);
  clearpolys(bridgePillars);
  bridge_angles.clear();
  clearpolys(decorPolygons);
  clearpolys(supportPolygons);
  clearpolys(toSupportPolygons);
  clearpolys(skinPolygons);
  clearpolys(skinFullFillPolygons);
  hullPolygon.clear();
  clearpolys(skirtPolygons);
  Z=-999999;
}

// void Layer::setBBox(Vector2d min, Vector2d max)
// {
//   Min.x() = MIN(Min.x(),min.x());
//   Min.y = MIN(Min.y,min.y);
//   Max.x() = MAX(Max.x(),max.x());
//   Max.y = MAX(Max.y,max.y);
// }
// void Layer::setBBox(vector<Vector2d> minmax)
// {
//   setBBox(minmax[0],minmax[1]);
// }
// void Layer::setBBox(Vector3d min, Vector3d max)
// {
//   Min.x() = MIN(Min.x(),min.x());
//   Min.y = MIN(Min.y,min.y);
//   Max.x() = MAX(Max.x(),max.x());
//   Max.y = MAX(Max.y,max.y);
// }


void Layer::SetPolygons(vector<Poly> &polys) {
  this->polygons = polys;
  for(uint i=0;i<polygons.size();i++){
    polygons[i].setZ(Z);
  }
}

// //not used
// void Layer::SetPolygons(const Matrix4d &T, const Shape &shape,
// 			double z) {
//   cerr << "Layer::SetPolygons" << endl;
//   double offsetZ = Z;
//   bool polys_ok=false;
//   while (!polys_ok) {
//     double max_grad;
//     polys_ok = shape.getPolygonsAtZ(T, offsetZ, polygons, max_grad);
//     offsetZ+=thickness/10.;
//   }
//   for(uint i=0;i<polygons.size();i++){
//     polygons[i].setZ(Z);
//   }
// }

Vector2d Layer::getRandomPolygonPoint() const
{
  if (polygons.size() == 0) return Vector2d(0.,0.);
  Poly p = polygons[rand()%polygons.size()];
  return p.vertices[rand()%p.size()];
}
Vector2d Layer::getFarthestPolygonPoint(const Vector2d &from) const
{
  if (polygons.size() == 0) return from;
  uint pindex = 0, pvindex = 0;
  double maxdist = 0.;
  for (uint i = 0; i<polygons.size(); i++) {
    uint fi = polygons[i].getFarthestIndex(from);
    double pdist = from.squared_distance(polygons[i][fi]);
    if (pdist > maxdist) {
      maxdist = pdist;
      pindex = i;
      pvindex = fi;
    }
  }
  return polygons[pindex][pvindex];
}

bool Layer::pointInPolygons(const Vector2d &p) const
{
  for (uint i = 0; i<polygons.size(); i++)
    if (!polygons[i].isHole() && polygons[i].vertexInside(p)) return true;
  return false;
}


int Layer::addShape(const Matrix4d &T, const Shape &shape,
            double &max_gradient, double max_supportangle)
{
  double hackedZ = Z;
  bool polys_ok = false;
  vector<Poly> polys;
  int num_polys=-1;
  // try to slice until polygons can be made, otherwise hack z
  while (!polys_ok && hackedZ < Z + thickness) {
    polys.clear();
    polys_ok = shape.getPolygonsAtZ(T, hackedZ,  // slice shape at hackedZ
                                    polys, max_gradient,
                                    toSupportPolygons, max_supportangle,
                                    thickness);
    if (polys_ok) {
      num_polys += polys.size();
      addPolygons(polys);
    } else {
      num_polys=-1;
      hackedZ += thickness/10;
      cerr << "hacked Z " << Z << " -> " << hackedZ << endl;
    }
  }
  cleanupPolygons();
  return num_polys;
}

void Layer::cleanupPolygons()
{
  double clean = thickness/CLEANFACTOR;
  for(uint i=0; i < polygons.size(); i++){
    polygons[i].cleanup(clean);
  }
}

void Layer::addPolygons(vector<Poly> &polys)
{
  for(uint i=0;i<polys.size();i++){
    polys[i].setZ(Z);
  }
  polygons.insert(polygons.end(),polys.begin(),polys.end());
}


void Layer::calcBridgeAngles(const Layer *layerbelow) {
  bridge_angles.resize(bridgePolygons.size());
  Clipping clipp;
  vector<Poly> polysbelow = layerbelow->GetInnerShell();//clipp.getOffset(polygons,3*thickness);
  bridgePillars.resize(bridgePolygons.size());
  for (uint i=0; i<bridgePolygons.size(); i++)
    {
      // intersect bridge poly with polygons below (=pillars of bridge)
      clipp.clear();
      clipp.addPolys(polysbelow,subject);
      clipp.addPolys(clipp.getOffset(bridgePolygons[i].outer,thickness),clip);
      bridgePillars[i] = clipp.intersect();

      // TODO detect circular bridges -> rotating infill?

      // get average direction of the mutual connections of all the intersections
      Vector2d dir(0,0);
      for (uint p=0; p<bridgePillars[i].size(); p++){
    for (uint q=p+1; q<bridgePillars[i].size(); q++){
      // Vector2d p1,p2;
      // bridgePillars[i][q].shortestConnectionSq(bridgePillars[i][p], p1,p2);
      // dir += (p2-p1);
      dir += bridgePillars[i][q].center - bridgePillars[i][p].center;
    }
      }
      //cerr << pillars.size() << " - " << dir << endl;
      bridge_angles[i] = atan2(dir.y(),dir.x());
      if (bridge_angles[i]<0) bridge_angles[i]+=M_PI;
    }
}

// these are used for the bridge polys of the layer above
// NOT USED, NOT TESTED, using calcBridgeAngles
vector <double> Layer::getBridgeRotations(const vector<Poly> &polys) const{
  cerr << "Layer::getBridgeRotations" << endl;
  vector<double> angles; angles.resize(polys.size());
  Clipping clipp;
  vector<Poly> offset = polygons;//clipp.getOffset(polygons,3*thickness);
  for (uint i=0; i<polys.size(); i++)
    {
      // intersect bridge poly with polygons below (=pillars of bridge)
      clipp.clear();
      clipp.addPolys(offset,subject);
      clipp.addPolys(clipp.getOffset(polys[i],2*thickness),clip);
      vector<Poly> pillars = clipp.intersect();
      // get average direction of the connection lines of all the intersections
      Vector2d dir(0,0);
      for (uint p=0; p<pillars.size(); p++){
    for (uint q=p+1; q<pillars.size(); q++){
      dir+=pillars[q].center-pillars[p].center;
    }
      }
      //cerr << pillars.size() << " - " << dir << endl;
      angles[i] = atan2(dir.y(),dir.x());
    }
  return angles;
}


void Layer::CalcInfill (Settings &settings, const InfillSet &infills,
                        bool fullInfill, bool altInfill, bool firstLayer)
{
    double fullInfillDistance = settings.GetInfillDistance(thickness, 100, 0);

    normalInfill.clear();

    if (infills.skirtInfill) {   // skirt infill first
        vector<Poly> skirtFill;
        Clipping clipp;
        clipp.addPolys(skirtPolygons, subject);
        clipp.addPolys(GetOuterShell(), clip);
        clipp.addPolys(supportPolygons, clip);
        skirtFill = clipp.subtract();
        skirtFill = Clipping::getOffset(skirtFill, -fullInfillDistance);
        infills.skirtInfill->apply(Z, skirtFill, LayerNo, normalInfill);
    }

    if (fullInfill && infills.fullInfill)
        infills.fullInfill->apply(Z, fillPolygons, LayerNo, normalInfill);
    else if (firstLayer && infills.firstInfill)
        infills.firstInfill->apply(Z, fillPolygons, LayerNo, normalInfill);
    else if (altInfill && infills.altInfill)
        infills.altInfill->apply(Z, fillPolygons, LayerNo, normalInfill);
    else if (infills.normalInfill)
        infills.normalInfill->apply(Z, fillPolygons, LayerNo, normalInfill);

  infills.fullInfill->apply(Z, fullFillPolygons, LayerNo, normalInfill);

  decorInfill.clear();
  if (infills.decorInfill)
      infills.decorInfill->apply(Z, decorPolygons, LayerNo, decorInfill);


  assert(bridge_angles.size() >= bridgePolygons.size());
  Infill * bInfill;
//  bridgeInfill.clear();
  for (uint b=0; b < bridgePolygons.size(); b++){
    bInfill = new Infill(ParallelInfill,
                         settings.get_double("Slicing/BridgeExtrusion"),
                         fullInfillDistance, bridge_angles[b]+M_PI/2, 0);
    bInfill->apply(Z, bridgePolygons[b],LayerNo, normalInfill);
    delete bInfill;
  }


  if (skins > 1) {
    // relative extrusion for skins:
    for (uint s = 0; s < skins; s++) {
      double sz = Z-thickness + (s+1)*thickness/skins;
      infills.skinInfills[skins-1]->apply(sz, skinFullFillPolygons, LayerNo+s,
                                          skinInfill);
    }
  }

  supportInfill.clear();
  if (infills.supportInfill)
      infills.supportInfill->apply(Z, supportPolygons, LayerNo, supportInfill);

  if (infills.thinInfill)
      infills.thinInfill->apply(Z, thinPolygons, LayerNo, normalInfill);
}


// call before full fill areas are multiplied
void Layer::makeSkinPolygons()
{
  if (skins<2) return;
  skinFullFillPolygons = fullFillPolygons;
  fullFillPolygons.clear();
}

// add bridge polys and subtract them from normal and full fill polys
// each given ExPoly is a single bridge with its holes
void Layer::addBridgePolygons(const vector<ExPoly> &newexpolys)
{
  // clip against normal fill and make these areas into bridges:
  Clipping clipp;
  uint num_bridges = newexpolys.size();
  if (num_bridges==0) return;
  clipp.clear();
  bridgePolygons.clear();
  for (uint i=0; i < num_bridges; i++){
    vector<Poly> newpolys = Clipping::getPolys(newexpolys[i]);
    clipp.clear();
    clipp.addPolys(fillPolygons,subject);
    clipp.addPolys(newpolys, clip);
    vector<ExPoly> exbridges = clipp.ext_intersect();
    bridgePolygons.insert(bridgePolygons.end(),exbridges.begin(),exbridges.end());
  }
  // subtract from normal fill
  clipp.clear();
  clipp.addPolys(fillPolygons,subject);
  clipp.addPolys(newexpolys, clip);
  setNormalFillPolygons(clipp.subtract());
}

void Layer::addFullPolygons(const vector<ExPoly> &newpolys, bool decor)
{
  addFullPolygons(Clipping::getPolys(newpolys),decor);
}

// add full fill and subtract them from normal fill polys
void Layer::addFullPolygons(const vector<Poly> &newpolys, bool decor)
{
  if (newpolys.size()==0) return;
  Clipping clipp;
  clipp.clear();
  // full fill only where already normal fill
  clipp.addPolys(fillPolygons,subject);
  if (decor) clipp.addPolys(fullFillPolygons,subject);
  clipp.addPolys(newpolys,clip);
  clipp.setZ(Z);
  vector<Poly> inter = clipp.intersect();
  vector<Poly> normals = clipp.subtractMerged(thickness/2.);
  if (decor) {//  && LayerNo != 0) // no decor on base layers
    decorPolygons.insert(decorPolygons.end(), inter.begin(), inter.end());
    Clipping clipp;
    clipp.addPolys(fullFillPolygons,subject);
    clipp.addPolys(inter,clip);
    clipp.setZ(Z);
    setFullFillPolygons(clipp.subtract());
  }
  else {
    fullFillPolygons.insert(fullFillPolygons.end(),inter.begin(),inter.end());
  }

  setNormalFillPolygons(normals);
  //  mergeFullPolygons(false); // done separately
}

void cleanup(vector<Poly> &polys, double error)
{
  for (uint i = 0; i<polys.size(); i++)
    polys[i].cleanup(error);
}

void Layer::mergeFullPolygons(bool bridge)
{
  // if (bridge) {
  //   // setBridgePolygons(Clipping::getMerged(bridgePolygons, thickness));
  //   // clipp.addPolys(bridgePolygons,clip);
  // } else {
  //subtract decor from full
  Clipping clipp;
  // clipp.addPolys(fullFillPolygons,subject);
  // clipp.addPolys(decorPolygons,clip);
  // setFullFillPolygons(clipp.subtract());

  setFullFillPolygons(Clipping::getMerged(fullFillPolygons, thickness));
  cleanup(fullFillPolygons, thickness/CLEANFACTOR);
  //subtract from normal fills
  clipp.clear();
  cleanup(fillPolygons, thickness/CLEANFACTOR);
  clipp.addPolys(fillPolygons,subject);
  clipp.addPolys(fullFillPolygons,clip);
  clipp.addPolys(decorPolygons,clip);
  vector<Poly> normals = clipp.subtractMerged();
  setNormalFillPolygons(normals);
  // }
}
void Layer::mergeSupportPolygons()
{
    setSupportPolygons(Clipping::getMerged(GetSupportPolygons()));
}

const vector<Poly> &Layer::GetInnerShell() const
{
  // if (fillPolygons.size()>0) return fillPolygons;
  // // no offset
  if (shellPolygons.size()>0) return (shellPolygons.back());
  // no shells:
  if (skinPolygons.size()>0) return skinPolygons;
  // no skins
  return polygons;
}
const vector<Poly> &Layer::GetOuterShell() const
{
  if (skinPolygons.size()>0) return skinPolygons;
  // no skins
  if (shellPolygons.size()>0) return (shellPolygons.front());
  // no shells:
  if (fillPolygons.size()>0) return fillPolygons;
  // no offset
  return polygons;
}

vector<ExPoly> Layer::GetExPolygons() const
{
  return Clipping::getExPolys(polygons,0,0);
}

// circular numbering
vector<Poly> Layer::GetShellPolygonsCirc(int number) const
{
  number = (shellPolygons.size() +  number) % shellPolygons.size();
  return shellPolygons[number];
}

void Layer::setNormalFillPolygons(const vector<Poly> &polys)
{
  clearpolys(fillPolygons);
  fillPolygons = polys;
  for (uint i=0; i<fillPolygons.size();i++)
    fillPolygons[i].setZ(Z);
}

void Layer::setFullFillPolygons(const vector<Poly> &polys)
{
  clearpolys(fullFillPolygons);
  fullFillPolygons = polys;
  for (uint i=0; i<fullFillPolygons.size();i++)
    fullFillPolygons[i].setZ(Z);
}
void Layer::setBridgePolygons(const vector<ExPoly> &expolys)
{
  uint count = expolys.size();
  // vector<Poly> polygroups;
  // vector<bool> done; done.resize(count);
  // for (uint i=0; i<count;i++) done[i] = false;
  // for (uint i=0; i<count;i++) {
  //   if (!done[i]){
  //     vector<Poly> group;
  //     group.push_back(polys[i]);
  //     done[i] = true;
  //   }
  //   for (uint j=i+1; j<polys.size();j++){
  //     if (!done[j]){
  // 	if (polys[.polyInside(polys[j]){
  // 	    done[j] = true;
  // 	  g
  //   }
  // }
  clearpolys(bridgePolygons);
  bridgePolygons = expolys;
  for (uint i=0; i<count; i++) {
    bridgePolygons[i].outer.setZ(Z);
    for (uint h = 0; h < bridgePolygons[i].holes.size(); h++)
      bridgePolygons[i].holes[h].setZ(Z);
  }
}
void Layer::setBridgeAngles(const vector<double> &angles)
{
  bridge_angles=angles; // .insert(bridge_angles.begin(),angles.begin(),angles.end());
}

void Layer::setSupportPolygons(const vector<Poly> &polys)
{
  clearpolys(supportPolygons);
  supportPolygons = polys;
  const double minarea = 10*thickness*thickness;
  for (int i = supportPolygons.size()-1; i >= 0; i--) {
    supportPolygons[i].cleanup(thickness/CLEANFACTOR);
    if (abs(Clipping::Area(supportPolygons[i])) < minarea) {
      supportPolygons.erase(supportPolygons.begin() + i);
      continue;
    }
    vector<Vector2d> minmax = supportPolygons[i].getMinMax();
    Min.x() = min(minmax[0].x(),Min.x());
    Min.y() = min(minmax[0].y(),Min.y());
    Max.x() = max(minmax[1].x(),Max.x());
    Max.y() = max(minmax[1].y(),Max.y());
  }
}

void Layer::setSkirtPolygons(const vector<Poly> &poly)
{
  clearpolys(skirtPolygons);
  skirtPolygons = poly;
  for (uint i=0; i<skirtPolygons.size(); i++) {
    skirtPolygons[i].cleanup(thickness);
    skirtPolygons[i].setZ(Z);
  }
}


void Layer::FindThinpolys(const vector<Poly> &polys, double extrwidth,
              vector<Poly> &thickpolys, vector<Poly> &thinpolys)
{
#define THINPOLYS 1
#if THINPOLYS
  // go in
  thickpolys = Clipping::getOffset(polys, -0.5*extrwidth);
  // go out again, now thin polys are gone
  thickpolys = Clipping::getOffset(thickpolys, 0.55*extrwidth);
  // (need overlap to really clip)

  // use bigger (longer) polys for clip to avoid overlap of thin and thick extrusion lines
  vector <Poly> bigthick = Clipping::getOffset(thickpolys, extrwidth);
  // difference to original are thin polys
  Clipping clipp;
  clipp.addPolys(polys, subject);
  clipp.addPolys(bigthick, clip);
  thinpolys = clipp.subtract();
  // remove overlap
  thickpolys = Clipping::getOffset(thickpolys, -0.05*extrwidth);
#else
  thickpolys = polys;
#endif
}

void Layer::MakeShells(Settings &settings, int extruder)
{
  double extrudedWidth        = settings.GetExtrudedMaterialWidth(thickness, extruder);
  double roundline_extrfactor =
    settings.RoundedLinewidthCorrection(extrudedWidth,thickness);
  double distance       = 0.5 * extrudedWidth;
  double cleandist      = min(distance/CLEANFACTOR, thickness/CLEANFACTOR);
  double shelloffset    = settings.get_double("Slicing/ShellOffset");
  uint   shellcount     = settings.get_integer("Slicing/ShellCount");
  double infilloverlap  = settings.get_double("Slicing/InfillOverlap");

  // first shrink with global offset
  vector<Poly> shrinked = Clipping::getOffset(polygons, -2.0/M_PI*extrudedWidth-shelloffset);

  vector<Poly> thickPolygons;
  FindThinpolys(shrinked, extrudedWidth, thickPolygons, thinPolygons);
  shrinked = thickPolygons;

  for (uint i = 0; i<thinPolygons.size(); i++)
    thinPolygons[i].cleanup(cleandist);

  // // expand shrinked to get to the outer shell again
  // shrinked = Clipping::getOffset(shrinked, 2*distance);
  for (uint i = 0; i<shrinked.size(); i++)
    shrinked[i].cleanup(cleandist);

  //vector<Poly> shrinked = Clipping::getShrinkedCapped(polygons,distance);
  // outmost shells
  if (shellcount > 0) {
    if (skins>1) { // either skins
      for (uint i = 0; i<shrinked.size(); i++)  {
    shrinked[i].setExtrusionFactor(1./skins*roundline_extrfactor);
      }
      skinPolygons = shrinked;
    } else {  // or normal shell
      clearpolys(shellPolygons);
      for (uint i = 0; i<shrinked.size(); i++)  {
    shrinked[i].setExtrusionFactor(roundline_extrfactor);
      }
      shellPolygons.push_back(shrinked);
    }
    // inner shells
    for (uint i = 1; i<shellcount; i++) // shrink from shell to shell
      {
    shrinked = Clipping::getOffset(shrinked,-extrudedWidth);
    vector<Poly> thinpolys;
    FindThinpolys(shrinked, extrudedWidth, thickPolygons, thinpolys);
    shrinked = thickPolygons;
    thinPolygons.insert(thinPolygons.end(), thinpolys.begin(),thinpolys.end());
    for (uint j = 0; j<shrinked.size(); j++)
      shrinked[j].cleanup(cleandist);
    //shrinked = Clipping::getShrinkedCapped(shrinked,extrudedWidth);
    shellPolygons.push_back(shrinked);
      }
  }
  // the filling polygon
  if (settings.get_boolean("Slicing/DoInfill")) {
    fillPolygons = Clipping::getOffset(shrinked,-(1.-infilloverlap)*extrudedWidth);
    for (uint i = 0; i<fillPolygons.size(); i++)
      fillPolygons[i].cleanup(cleandist);
    //fillPolygons = Clipping::getShrinkedCapped(shrinked,extrudedWidth);
    //cerr << LayerNo << " > " << fillPolygons.size()<< endl;
  }

  calcConvexHull();

  //cerr << " .. made " << fillPolygons.size() << " offsetpolys "  << endl;
  // for (uint i =0; i<shellPolygons.size(); i++) {
  //   cout << "shell " << i << endl;
  //   for (uint j =1; j<shellPolygons[i].size(); j++) {
  //     shellPolygons[i][j].printinfo();
  //   }
  // }
}

void Layer::MakeSkirt(double distance, bool single)
{
  clearpolys(skirtPolygons);
  vector<Poly> all;
  if (single) { // single skirt for all combined
    all.push_back(hullPolygon);
    all.insert(all.end(),supportPolygons.begin(),supportPolygons.end());
    Poly hull = convexHull2D(all);
    vector<Poly> skp = Clipping::getOffset(hull, distance, jround);
    if (skp.size()>0){
      skirtPolygons.push_back(skp.front());
      skirtPolygons[0].setZ(Z);
      skirtPolygons[0].cleanup(thickness);
    }
  } else { // skirt for each shape
    skirtPolygons = Clipping::getOffset(GetOuterShell(), distance, jround);
  }
}


// calc convex hull and Min and Max of layer
void Layer::calcConvexHull()
{
  hullPolygon = convexHull2D(polygons); // see geometry.cpp
  hullPolygon.setZ(Z);
  setMinMax(hullPolygon);
}

void Layer::setMinMax(const vector<Poly> &polys)
{
    Min.set( INFTY, INFTY);
    Max.set(-INFTY, -INFTY);
    for (Poly p : polys){
        p.accumulateMinMax(Min, Max);
    }
}

bool Layer::setMinMax(const Poly &poly)
{
  vector<Vector2d> minmax = poly.getMinMax();
  if (minmax[0]==Min && minmax[1]==Max) return false;
  Min = minmax[0];
  Max = minmax[1];
  return true;
}


// Convert to GCode
void Layer::MakeGCode (Vector3d &start,
                       GCodeState &gc_state,
                       double offsetZ,
                       Settings *settings) const
{
  vector<PLine<3>*> plines;
  Vector2d start2(start.x(), start.y());
  Printlines *printlines = MakePrintlines(start, plines, offsetZ, *settings);
  makePrintLines3(start2, printlines, plines, settings);
  delete printlines;
  Printlines::makeAntioozeRetract(plines, settings);
  Printlines::toCommands(plines, settings, gc_state);
}

// Convert to 2D Printlines
Printlines *Layer::MakePrintlines(Vector3d &lastPos, //GCodeState &state,
                                  vector<PLine<3> *> &lines3,
                                  double offsetZ,
                                  Settings &settings) const
{
  uint currentExtruder = 0;

  const double linewidth      = settings.GetExtrudedMaterialWidth(thickness, currentExtruder);
  const double cornerradius   = linewidth*settings.get_double("Slicing/CornerRadius");

  const bool clipnearest      = settings.get_boolean("Slicing/MoveNearest");

  const uint supportExtruder  = settings.GetSupportExtruder();
  const double minshelltime   = settings.get_double("Slicing/MinShelltime");

  const double maxshellspeed  = settings.get_double(
              Settings::numbered("Extruder",currentExtruder)+"/MaxShellSpeed") * 60;
  const bool ZliftAlways      = settings.get_boolean(
              Settings::numbered("Extruder",currentExtruder)+"/ZliftAlways");

  const double extr_per_mm = settings.GetExtrusionPerMM(thickness, currentExtruder);

  Printlines *printlines = new Printlines(this, &settings, offsetZ);

  vector<Poly> polys; // intermediate collection

  // polys to keep line movements inside
  //const vector<Poly> * clippolys = &polygons;
  const vector<Poly> clippolys = GetOuterShell();

  double maxlinespeed = settings.get_double(
              Settings::numbered("Extruder",currentExtruder)+"/MaxLineSpeed") * 60; // default

  // 1. Skins, all but last, because they are the lowest lines, below layer Z
  if (skins > 1) {
      for(uint s = 0; s < skins; s++) {
          // z offset from bottom to top:
          double skin_z = Z - thickness + (s+1)*thickness/skins;
          if ( skin_z < 0 ){
              cerr << "Skin Z<0! " << s << " -- " << Z << " -- "<<skin_z <<" -- " << thickness <<  endl;
              continue;
          }

          // skin infill polys:
          polys.insert(polys.end(),
                       skinInfill.begin(),
                       skinInfill.end());
          // add skin infill to lines
          printlines->addPolys(INFILL, polys, false, maxlinespeed);

          polys.clear();

          // make polygons at skin_z:
          for(size_t p = 0; p < skinPolygons.size(); p++) {
              polys.push_back(Poly(skinPolygons[p], skin_z));
          }
          // add skin to lines
          printlines->addPolys(SKIN, polys, (s==0), // displace at first skin
                              maxshellspeed,
                              minshelltime);
          if (s < skins-1) { // not on the last layer, this handle with all other lines
              // have to get all these separately because z changes
              Vector2d startPoint(lastPos.x(),lastPos.y());
              vector<PLine<2>*> skinlines;
              printlines->makeLines(startPoint, skinlines);
              if (!ZliftAlways)
                  printlines->clipMovements(clippolys, skinlines, clipnearest, linewidth);
              printlines->optimize(minshelltime,  cornerradius, skinlines);
              printlines->getLines(skinlines, lines3, extr_per_mm);
              printlines->clear();
              skinlines.clear();
          }
          polys.clear();
      }
  } // last skin layer now still in lines

  // 2. Skirt, only shell
  printlines->addPolys(SKIRT, skirtPolygons, false,
                       maxshellspeed, minshelltime);

  // 3. Support
  if (supportInfill.size()>0) {
      const double maxsupportspeed = settings.get_double(
                  Settings::numbered("Extruder", supportExtruder)+"/MaxShellSpeed") * 60;
      printlines->addPolys(SUPPORT, supportInfill, false, maxsupportspeed);
  }
  // 4. all other polygons:

  //  Shells
  for(long p=shellPolygons.size()-1; p>=0; p--) { // inner to outer
    //cerr << "displace " << p << endl;
    printlines->addPolys(SHELL, shellPolygons[p],
            (p==long(shellPolygons.size())-1),
            maxshellspeed * 60,
            minshelltime);
  }

  //  Infill
  printlines->addPolys(INFILL, normalInfill, false, maxlinespeed);
  printlines->addPolys(INFILL, decorInfill, false, maxlinespeed);

  return printlines;
}


void Layer::makePrintLines3(Vector2d &startPos, Printlines *printlines,
                            vector<PLine<3> *> &lines3,
                            Settings *settings) const {
    // FINISH
    uint currentExtruder = 0;

    const double linewidth      = settings->GetExtrudedMaterialWidth(thickness, currentExtruder);
    const double cornerradius   = linewidth*settings->get_double("Slicing/CornerRadius");
    const bool clipnearest      = settings->get_boolean("Slicing/MoveNearest");

    const double extr_per_mm = settings->GetExtrusionPerMM(thickness, currentExtruder);
    const bool ZliftAlways      = settings->get_boolean(
                Settings::numbered("Extruder",currentExtruder)+"/ZliftAlways");
    const vector<Poly> clippolys = GetOuterShell();

    Command lchange(LAYERCHANGE, LayerNo);
    lchange.where = Vector3d(0.,0.,Z);
    lchange.comment += info();
    lines3.push_back(new PLine3(lchange));

    vector<PLine<2>*> lines;

    double polyspeedfactor = printlines->makeLines(startPos, lines);

    if (!ZliftAlways)
        Printlines::clipMovements(clippolys, lines, clipnearest, linewidth);
    printlines->optimize(settings->get_double("Slicing/MinLayertime"),
                        cornerradius, lines);

    if (LayerNo < settings->get_integer("Slicing/FirstLayersNum"))
        printlines->setSpeedFactor(settings->get_double("Slicing/FirstLayersSpeed"), lines);
    double slowdownfactor = printlines->getSlowdownFactor() * polyspeedfactor;

    if (settings->get_boolean("Slicing/FanControl")) {
        const int minFan = settings->get_integer("Slicing/MinFanSpeed");
        const int maxFan = settings->get_integer("Slicing/MaxFanSpeed");
        int fanspeed = minFan;
        if (slowdownfactor < 1 && slowdownfactor > 0) {
            double fanfactor = 1-slowdownfactor;
            fanspeed +=
                    int(fanfactor * (maxFan - minFan));
            fanspeed = CLAMP(fanspeed, minFan, maxFan);
            //cerr << slowdownfactor << " - " << fanfactor << " - " << fanspeed << " - " << endl;
        }
        Command fancommand(FANON, fanspeed);
        lines3.push_back(new PLine3(fancommand));
    }
    printlines->getLines(lines, lines3, extr_per_mm);
}

double Layer::area() const
{
  return Clipping::Area(polygons);
}

string Layer::info() const
{
  ostringstream ostr;
  ostr <<"Layer at Z=" << Z << " No=" << LayerNo
       <<", thickn=" << thickness
       <<", "<<skins <<" skins"
       <<", "<<polygons.size() <<" polys"
       <<", "<<shellPolygons.size() <<" shells"
       <<", "<<fullFillPolygons.size() <<" fullfill polys"
       <<", "<<bridgePolygons.size() <<" bridge polys"
       <<", "<<skinFullFillPolygons.size() <<" skin fullfill polys"
       <<", "<<supportPolygons.size() <<" support polys";
//  ostr <<", infill: ";
//  if (normalInfill)
//    ostr <<" normal "<<normalInfill->size() ;
//  if (fullInfill)
//    ostr <<", full "<<fullInfill->size()  ;
//  if (bridgeInfills.size()>0)
//    ostr <<", bridges "<<bridgeInfills.size() ;
//  if (supportInfill)
//    ostr<<", support "<<supportInfill->size() ;
//  ostr <<", skinfills "<<skinFullInfills.size() ;

  if (previous != NULL)
    ostr << " prev.No=" << previous->LayerNo;

  return ostr.str();
}


vector<Poly> Layer::getOverhangs() const
{
  vector<Poly> overhangs;
  if (previous!=NULL) {
    Clipping clipp;
    clipp.addPolys(polygons, subject);
    vector<Poly> prevoffset = Clipping::getOffset(previous->polygons, thickness/2);
    clipp.addPolys(prevoffset, clip);
    clipp.setZ(Z);
    overhangs = clipp.subtract();//CL::pftNonZero,CL::pftNonZero);
  }
  return overhangs;
}

string Layer::SVGpath(const Vector2d &trans) const
{
  ostringstream ostr;
  ostr << "\t<g id=\"Layer_"<< LayerNo
       << "_z:" << getZ() << "\">" << endl;
  for (uint i = 0; i<polygons.size(); i++) {
    ostr << polygons[i].SVGpath(trans) << endl;
  }
  ostr << "\t</g>";
  return ostr.str();
}

void Layer::addToInfill(Infill *infill, const vector<Poly> &polys)
{
    infill->apply(Z, polys, LayerNo, normalInfill);
}


void Layer::Draw(Settings &settings, Render *render)
{

#if 0
  // test single area expolys
  vector<ExPoly> expolys = Clipping::getExPolys(polygons);
  draw_polys(expolys, GL_LINE_LOOP, 1, 3, RED, 1);
  cerr << expolys.size() << endl;

  Infill exinf(this, 1.);
  exinf.setName("infill");
  double infilldistance = settings.GetInfillDistance(thickness,
                             settings.Slicing.InfillPercent);

  exinf.addPolys(Z, expolys, HexInfill,
         infilldistance, infilldistance, 0.4);
  draw_polys(exinf.infillpolys, GL_LINE_LOOP, 1, 3,
         (exinf.cached?BLUEGREEN:GREEN), 1);
  return;
#endif

  bool randomized = settings.get_boolean("Display/RandomizedLines");
  bool filledpolygons = settings.get_boolean("Display/DisplayFilledAreas");
  // glEnable(GL_LINE_SMOOTH);
  // glHint(GL_LINE_SMOOTH_HINT,  GL_NICEST);
  draw_polys(polygons, GL_LINE_LOOP, 1, 3, RED, 1, randomized);
  draw_polys(polygons, GL_POINTS,    1, 3, RED, 1, randomized);

  if(render != nullptr && settings.get_boolean("Display/DrawCPOutlineNumbers"))
    for(size_t p=0; p<polygons.size();p++)
      {
    ostringstream oss;
    oss << p;
    Vector2d center = polygons[p].getCenter();
    render->draw_string(Vector3d(center.x(), center.y(), Z), oss.str());
      }

  draw_poly(hullPolygon,    GL_LINE_LOOP, 3, 3, ORANGE,  0.5, randomized);
  draw_polys(skirtPolygons, GL_LINE_LOOP, 3, 3, YELLOW,  1, randomized);
  draw_polys(shellPolygons, GL_LINE_LOOP, 1, 3, YELLOW2, 1, randomized);
  draw_polys(thinPolygons,  GL_LINE_LOOP, 2, 3, YELLOW,  1, randomized);

  glColor4f(0.5,0.9,1,1);
  glLineWidth(1);
  double zs = Z;
  for(size_t s=0;s<skins;s++) {
    for(size_t p=0; p < skinPolygons.size();p++) {
      //cerr << s << ": " << p << " _ " << zs << endl;
      skinPolygons[p].draw(GL_LINE_LOOP, zs, randomized);
    }
    zs-=thickness/skins;
  }
  draw_polys(fillPolygons,         GL_LINE_LOOP, 1, 3, WHITE, 0.6, randomized);
  if (supportPolygons.size()>0) {
    if (filledpolygons)
      draw_polys_surface(supportPolygons,  Min, Max, Z, thickness/2., BLUE2, 0.4);
    draw_polys(supportPolygons,      GL_LINE_LOOP, 3, 3, BLUE2, 1,   randomized);
    if(settings.get_boolean("Display/DrawVertexNumbers"))
      for(size_t p=0; p<supportPolygons.size();p++)
    supportPolygons[p].drawVertexNumbers(render);
  } // else
    // draw_polys(toSupportPolygons,    GL_LINE_LOOP, 1, 1, BLUE2, 1,   randomized);
  draw_polys(bridgePolygons,       GL_LINE_LOOP, 3, 3, RED2,  0.7, randomized);
  draw_polys(fullFillPolygons,     GL_LINE_LOOP, 1, 1, GREY,  0.6, randomized);
  draw_polys(decorPolygons,        GL_LINE_LOOP, 1, 3, WHITE, 1,   randomized);
  draw_polys(skinFullFillPolygons, GL_LINE_LOOP, 1, 3, GREY,  0.6, randomized);
  if (filledpolygons) {
    draw_polys_surface(fullFillPolygons,  Min, Max, Z, thickness/2., GREEN, 0.5);
    draw_polys_surface(decorPolygons,  Min, Max, Z, thickness/2., GREY, 0.2);
  }
  if(settings.get_boolean("Display/DisplayInfill"))
    {
      if (filledpolygons)
          draw_polys_surface(fillPolygons,  Min, Max, Z, thickness/2., GREEN2, 0.25);
      bool DebugInfill = settings.get_boolean("Display/DisplayDebugInfill");
      draw_polys(normalInfill, GL_LINE_LOOP, 1, 3,
                 (false?BLUEGREEN:GREEN), 1, randomized);
//      if (thinInfill)
//          draw_polys(thinInfill->infillpolys, GL_LINE_LOOP, 1, 3,
//                     GREEN, 1, randomized);
//      if (fullInfill)
//          draw_polys(fullInfill->infillpolys, GL_LINE_LOOP, 1, 3,
//                     (fullInfill->cached?BLUEGREEN:GREEN), 0.8, randomized);
//      if (skirtInfill)
//          draw_polys(skirtInfill->infillpolys, GL_LINE_LOOP, 1, 3,
//                     YELLOW, 0.6, randomized);
//      if(DebugInfill && fullInfill->cached)
//          draw_polys(fullInfill->getCachedPattern(Z), GL_LINE_LOOP, 1, 3,
//                     ORANGE, 0.5, randomized);
      draw_polys(decorInfill, GL_LINE_LOOP, 1, 3,
                 (false?BLUEGREEN:GREEN), 0.8, randomized);
//      draw_polys(bridgeInfill, GL_LINE_LOOP, 2, 3,
//                 RED3, 0.9, randomized);
      draw_polys(supportInfill, GL_LINE_LOOP, 1, 3,
                 (false?BLUEGREEN:GREEN), 0.8, randomized);
      draw_polys(skinInfill, GL_LINE_LOOP, 1, 3,
                 (false?BLUEGREEN:GREEN), 0.6, randomized);
  }
  //draw_polys(GetInnerShell(), GL_LINE_LOOP, 2, 3, WHITE,  1);
  glLineWidth(1);
  if(settings.get_boolean("Display/DrawCPVertexNumbers")) // poly vertex numbers
    for(size_t p=0; p<polygons.size();p++)
      polygons[p].drawVertexNumbers(render);
      //polygons[p].drawVertexAngles();

  if(settings.get_boolean("Display/DrawCPLineNumbers"))  // poly line numbers
    for(size_t p=0; p<polygons.size();p++)
      polygons[p].drawLineNumbers(render);

  if(settings.get_boolean("Display/DrawVertexNumbers")) { // infill vertex numbers
    for(size_t p=0; p<fillPolygons.size();p++)
      fillPolygons[p].drawVertexNumbers(render);
    for(size_t p=0; p<fullFillPolygons.size();p++)
      fullFillPolygons[p].drawVertexNumbers(render);
    for(size_t p=0; p<decorPolygons.size();p++)
      decorPolygons[p].drawVertexNumbers(render);
    for(size_t p=0; p<shellPolygons.size();p++)
      for(size_t q=0; q<shellPolygons[p].size();q++)
    shellPolygons[p][q].drawVertexNumbers(render);
  }


  if (settings.get_boolean("Display/ShowLayerOverhang")) {
    draw_polys(bridgePillars,        GL_LINE_LOOP, 3, 3, YELLOW,0.7, randomized);
    if (previous!=NULL) {
      vector<Poly> overhangs = getOverhangs();
      draw_polys(overhangs, GL_LINE_LOOP, 1, 3, VIOLET, 0.8, randomized);
#ifdef USECAIRO
      //draw_polys_surface(overhangs, Min, Max, Z, thickness/5, VIOLET , 0.5);
      Cairo::RefPtr<Cairo::ImageSurface> surface;
      Cairo::RefPtr<Cairo::Context>      context;
      if (rasterpolys(overhangs, Min, Max, thickness/5, surface, context))
    if (surface) {
      glColor4f(RED[0],RED[1],RED[2], 0.5);
      glDrawCairoSurface(surface, Min, Max, Z);
      glColor4f(RED[0],RED[1],RED[2], 0.6);
      glPointSize(3);
      glBegin(GL_POINTS);
      for (double x = Min.x(); x<Max.x(); x+=thickness)
        for (double y = Min.y(); y<Max.y(); y+=thickness)
          if (getCairoSurfaceDatapoint(surface, Min, Max, Vector2d(x,y))!=0)
        glVertex3d(x,y,Z);
      glEnd();
    }
#endif
    }
  }

#if 0
  // test point-in-polygons
  const vector<Poly> *polys = GetOuterShell();
  glColor4f(RED[0],RED[1],RED[2], 0.6);
  glPointSize(3);
  glBegin(GL_POINTS);
  for (double x = Min.x(); x<Max.x(); x+=thickness/1)
    for (double y = Min.y(); y<Max.y(); y+=thickness/1) {
      bool inpoly = false;
      for (uint i=0; i<polys->size(); i++) {
    if ((*polys)[i].vertexInside(Vector2d(x,y))){
      inpoly=true; break;
    }
      }
      if (inpoly)
    glVertex3d(x,y,Z);
      // else
      //   glColor4f(0.3,0.3,0.3, 0.6);
    }
  glEnd();
#endif

}

void Layer::DrawRulers(const Vector2d &point, Render *render)
{
  if (polygons.size() == 0) return;
  Vector2d x0(Min.x()-10, point.y());
  Vector2d x1(Max.x()+10, point.y());
  Vector2d y0(point.x(), Min.y()-10);
  Vector2d y1(point.x(), Max.y()+10);

  // cut axes with layer polygons
  vector<Intersection> xint, yint;
  Intersection hit;
  hit.p = Vector2d(Min.x(),point.y()); hit.d = 10;
  xint.push_back(hit);
  hit.p = Vector2d(Max.x(),point.y()); hit.d = Max.x()-Min.x()+10;
  xint.push_back(hit);
  hit.p = Vector2d(point.x(),Min.y()); hit.d = 10;
  yint.push_back(hit);
  hit.p = Vector2d(point.x(),Max.y()); hit.d = Max.y()-Min.y()+10;
  yint.push_back(hit);

  for(size_t p=0; p<polygons.size();p++) {
    vector<Intersection> lint = polygons[p].lineIntersections(x0,x1,0.1);
    xint.insert(xint.end(),lint.begin(),lint.end());
    lint = polygons[p].lineIntersections(y0,y1,0.1);
    yint.insert(yint.end(),lint.begin(),lint.end());
  }
  //cerr << xint.size() << " - "<< xint.size() << endl;
  std::sort(xint.begin(),xint.end());
  std::sort(yint.begin(),yint.end());

  glColor4f(1.,1.,1.,1.);
  glLineWidth(1);
  glBegin(GL_LINES);
  // draw lines
  glVertex3d(Min.x(), x0.y(), Z);
  glVertex3d(Max.x(), x1.y(), Z);
  glVertex3d(y0.x(), Min.y(), Z);
  glVertex3d(y1.x(), Max.y(), Z);
  // draw ticks
  double ticksize=2;
  for(guint i = 0; i<xint.size(); i++) {
    glVertex3d(xint[i].p.x(), xint[i].p.y()-ticksize, Z);
    glVertex3d(xint[i].p.x(), xint[i].p.y()+ticksize, Z);
  }
  for(guint i = 0; i<yint.size(); i++) {
    glVertex3d(yint[i].p.x()-ticksize, yint[i].p.y(), Z);
    glVertex3d(yint[i].p.x()+ticksize, yint[i].p.y(), Z);
  }
  glEnd();
  // draw BBox
  glBegin(GL_LINE_LOOP);
  glVertex3d(Min.x(), Min.y(), Z);
  glVertex3d(Max.x(), Min.y(), Z);
  glVertex3d(Max.x(), Max.y(), Z);
  glVertex3d(Min.x(), Max.y(), Z);
  glEnd();
  // draw numbers
  ostringstream val;
  val.precision(1);
  for(guint i = 1; i<xint.size(); i++) {
    val.str("");
    double v = xint[i].p.x()-xint[i-1].p.x();
    val << fixed << v;
    render->draw_string(Vector3d((xint[i].p.x()+xint[i-1].p.x())/2.,
                 xint[i].p.y()+1,Z),
            val.str());
  }
  for(guint i = 1; i<yint.size(); i++) {
    val.str("");
    double v = yint[i].p.y()-yint[i-1].p.y();
    val << fixed << v;
    render->draw_string(Vector3d(yint[i].p.x()+1,(yint[i].p.y()+yint[i-1].p.y())/2.,Z),
            val.str());
  }

}
