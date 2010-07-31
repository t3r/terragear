// genobj.hxx -- Generate the flight gear "obj" file format from the
//               triangle output
//
// Written by Curtis Olson, started March 1999.
//
// Copyright (C) 1999  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Id: genobj.hxx,v 1.9 2004-11-19 22:25:49 curt Exp $


#ifndef _GENOBJ_HXX
#define _GENOBJ_HXX


#ifndef __cplusplus                                                          
# error This library requires C++
#endif                                   


#include <simgear/compiler.h>

#include <simgear/bucket/newbucket.hxx>
#include <Geometry/point3d.hxx>
#include <simgear/math/sg_geodesy.hxx>
#include <simgear/math/sg_types.hxx>

#include <Optimize/genfans.hxx>
#include <Main/construct.hxx>
#include <Triangulate/triangle.hxx>

typedef std::vector < int_list > tex_list;
typedef tex_list::iterator tex_list_iterator;
typedef tex_list::const_iterator const_tex_list_iterator;

class TGGenOutput {

private:

    // node list in geodetic coordinates
    point_list geod_nodes;

    // triangles (by index into point list)
    triele_list tri_elements;

    // texture coordinates
    TGTriNodes tex_coords;

    // fan list
    opt_list fans[TG_MAX_AREA_TYPES];

    // textures pointer list
    tex_list textures[TG_MAX_AREA_TYPES];

    // global bounding sphere
    SGVec3d gbs_center;
    double gbs_radius;

    // calculate the global bounding sphere.  Center is the average of
    // the points.
    void calc_gbs( TGConstruct& c );

    // caclulate the bounding sphere for a list of triangle faces
    void calc_group_bounding_sphere( TGConstruct& c, const opt_list& fans, 
				     Point3D *center, double *radius );

    // caclulate the bounding sphere for the specified triangle face
    void calc_bounding_sphere( TGConstruct& c, const TGTriEle& t, 
			       Point3D *center, double *radius );

    // traverse the specified fan and attempt to calculate "none
    // stretching" texture coordinates
    // int_list calc_tex_coords( TGConstruct& c, point_list geod_nodes, int_list fan );

public:

    // Constructor && Destructor
    inline TGGenOutput() { }
    inline ~TGGenOutput() { }

    // build the necessary output structures based on the
    // triangulation data
    int build( TGConstruct& c );

    // write out the fgfs scenery file
    int write( TGConstruct &c );
};


#endif // _GENOBJ_HXX


