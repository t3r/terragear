// rwy_prec.hxx -- Build a precision runway
//
// Written by Curtis Olson, started February 2002.
//
// Copyright (C) 2002  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Id$
//


#ifndef _RWY_PREC_HXX
#define _RWY_PREC_HXX


#include <Polygon/polygon.hxx>
#include <Polygon/superpoly.hxx>

#include "runway.hxx"
#include "texparams.hxx"


// generate a precision approach runway.  The routine modifies
// rwy_polys, texparams, and accum.  For specific details and
// dimensions of precision runway markings, please refer to FAA
// document AC 150/5340-1H

void gen_precision_rwy( const TGRunway& rwy_info,
                        double alt_m,
			const string& material,
			superpoly_list *rwy_polys,
			texparams_list *texparams,
			TGPolygon *accum );


#endif // _RWY_PREC_HXX
