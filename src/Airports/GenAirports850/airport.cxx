#include "beznode.hxx"
#include "runway.hxx"
#include "helipad.hxx"
#include "airport.hxx"

#include <list>
#include <map>
#include <string>
#include <ctime>

#include <simgear/compiler.h>
#include <simgear/structure/exception.hxx>
#include <simgear/debug/logstream.hxx>
#include <simgear/bucket/newbucket.hxx>
#include <simgear/math/sg_geodesy.hxx>
#include <simgear/math/SGGeometry.hxx>
#include <simgear/io/sg_binobj.hxx>
#include <simgear/misc/texcoord.hxx>

#include <Polygon/polygon.hxx>
#include <Polygon/texparams.hxx>
#include <Polygon/superpoly.hxx>
#include <Polygon/texparams.hxx>
#include <Polygon/chop.hxx>

#include <Geometry/poly_support.hxx>
#include <Geometry/poly_extra.hxx>

#include <Output/output.hxx>

#include "global.hxx"
#include "elevations.hxx"


#include <stdio.h>

string SGLOG_GREEN  = "\033[0;32m";
string SGLOG_NORMAL = "\033[0m";

Airport::Airport( int c, char* def)
{
    int   numParams;
    char* tok;
    int   ct = 0;

    code = c;

    // we need to tokenize airports, since we can't scanf two strings next to each other...
    numParams = 0;
    bool done = false;
    
    while (!done)
    {
        // trim leading whitespace
        while(isspace(*def)) def++;

        tok = strtok(def, " \t\r\n");

        if (tok)
        {
            def += strlen(tok)+1;

            switch( numParams )
            {
                case 0:
                    altitude = atoi(tok);
                    break;

                case 1:
                    ct = atoi(tok);
                    break;

                case 2:
                    // deprecated - ignore
                    break;

                case 3:
                    icao = tok;
                    description = def;
                    done = true;
                    break;
            }
        }
        numParams++;
    }

    altitude *= SG_FEET_TO_METER;

    dbg_rwy_poly  = 0;
    dbg_pvmt_poly = 0;
    dbg_feat_poly = 0;
    dbg_base_poly = 0;
    dbg_taxi_poly = 0;


    SG_LOG( SG_GENERAL, SG_DEBUG, "Read airport with icao " << icao << ", control tower " << ct << ", and description " << description );
}

Airport::~Airport()
{
    for (unsigned int i=0; i<features.size(); i++)
    {
        delete features[i];
    }

    for (unsigned int i=0; i<helipads.size(); i++)
    {
        delete helipads[i];
    }

    for (unsigned int i=0; i<runways.size(); i++)
    {
        delete runways[i];
    }

    for (unsigned int i=0; i<waterrunways.size(); i++)
    {
        delete waterrunways[i];
    }

    for (unsigned int i=0; i<pavements.size(); i++)
    {
        delete pavements[i];
    }

    for (unsigned int i=0; i<taxiways.size(); i++)
    {
        delete taxiways[i];
    }

    for (unsigned int i=0; i<lightobjects.size(); i++)
    {
        delete lightobjects[i];
    }

    for (unsigned int i=0; i<windsocks.size(); i++)
    {
        delete windsocks[i];
    }

    for (unsigned int i=0; i<beacons.size(); i++)
    {
        delete beacons[i];
    }

    for (unsigned int i=0; i<signs.size(); i++)
    {
        delete signs[i];
    }

    for (unsigned int i=0; i<boundary.size(); i++)
    {
        delete boundary[i];
    }
}

void Airport::SetDebugPolys( int rwy, int taxi, int pvmt, int feat, int base )
{
    dbg_rwy_poly  = rwy;
    dbg_taxi_poly = taxi;
    dbg_pvmt_poly = pvmt;
    dbg_feat_poly = feat;
    dbg_base_poly = base;
}


// TODO: Add to runway object
// calculate texture coordinates for runway section using the provided
// texturing parameters.  Returns a mirror polygon to the runway,
// except each point is the texture coordinate of the corresponding
// point in the original polygon.
static TGPolygon rwy_section_tex_coords( const TGPolygon& in_poly, const TGTexParams& tp, const bool clip_result )
{
    int i, j;
    TGPolygon result;
    result.erase();

    SGGeod ref = tp.get_ref();
    double width = tp.get_width();
    double length = tp.get_length();
    double heading = tp.get_heading();
    double minu = tp.get_minu();
    double maxu = tp.get_maxu();
    double minv = tp.get_minv();
    double maxv = tp.get_maxv();
    SG_LOG( SG_GENERAL, SG_DEBUG, "section ref = " << ref );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  width = " << width );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  length = " << length );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  heading = " << heading );
    Point3D p, t;
    double x, y, tx, ty;

    for ( i = 0; i < in_poly.contours(); ++i ) {
    	for ( j = 0; j < in_poly.contour_size( i ); ++j ) {
    	    p = in_poly.get_pt( i, j );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "point = " << p);

    	    //
    	    // 1. Calculate distance and bearing from the center of
    	    // the runway
    	    //

    	    // given alt, lat1, lon1, lat2, lon2, calculate starting
    	    // and ending az1, az2 and distance (s).  Lat, lon, and
    	    // azimuth are in degrees.  distance in meters
    	    double az1, az2, dist;
    	    geo_inverse_wgs_84( 0, ref.getLatitudeDeg(), ref.getLongitudeDeg(), p.y(), p.x(),
    				&az1, &az2, &dist );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "basic course = " << az2);

    	    //
    	    // 2. Rotate this back into a coordinate system where Y
    	    // runs the length of the runway and X runs crossways.
    	    //

    	    double course = az2 - heading;
    	    while ( course < -360 ) { course += 360; }
    	    while ( course > 360 ) { course -= 360; }
    	    SG_LOG( SG_GENERAL, SG_DEBUG,
                    "  course = " << course << "  dist = " << dist );

    	    //
    	    // 3. Convert from polar to cartesian coordinates
    	    //
    
    	    x = sin( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    y = cos( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  x = " << x << " y = " << y);

    	    //
    	    // 4. Map x, y point into texture coordinates
    	    //
    	    double tmp;

            tmp = x / width;
            tx = tmp * (maxu - minu) + minu;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ")");

            if ( clip_result) {
                if ( tx < 0.0 ) { tx = 0.0; }
                if ( tx > 1.0 ) { tx = 1.0; }
            }

    	    ty = y / length;
            tmp = y / length;
            ty = tmp * (maxv - minv) + minv;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << ty << ")");

            if ( clip_result ) {
                if ( ty < 0.0 ) { ty = 0.0; }
                if ( ty > 1.0 ) { ty = 1.0; }
            }

    	    t = Point3D( tx, ty, 0 );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ", " << ty << ")");

    	    result.add_node( i, t );
    	}
    }

    return result;
}

static TGPolygon shoulder_tex_coords( const TGPolygon& in_poly, const TGTexParams& tp )
{
    int i, j;
    TGPolygon result;
    result.erase();

    SGGeod ref = tp.get_ref();
    double width = tp.get_width();
    double length = tp.get_length();
    double heading = tp.get_heading();
    double minu = tp.get_minu();
    double maxu = tp.get_maxu();
    double minv = tp.get_minv();
    double maxv = tp.get_maxv();
    SG_LOG( SG_GENERAL, SG_DEBUG, "section ref = " << ref );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  width   = " << width );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  length  = " << length );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  heading = " << heading );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  minv    = " << minv );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  maxv    = " << maxv );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  heading = " << heading );

    Point3D p, t;
    double x, y, tx, ty;

    for ( i = 0; i < in_poly.contours(); ++i ) 
    {
    	for ( j = 0; j < in_poly.contour_size( i ); ++j ) 
        {
    	    p = in_poly.get_pt( i, j );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "tex coords for contour " << i << "point " << j << ": " << p );

    	    //
    	    // 1. Calculate distance and bearing from the center of
    	    // the feature
    	    //

    	    // given alt, lat1, lon1, lat2, lon2, calculate starting
    	    // and ending az1, az2 and distance (s).  Lat, lon, and
    	    // azimuth are in degrees.  distance in meters
    	    double az1, az2, dist;
    	    geo_inverse_wgs_84( 0, ref.getLatitudeDeg(), ref.getLongitudeDeg(), p.y(), p.x(),
    				&az1, &az2, &dist );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "basic course from ref = " << az2);

    	    //
    	    // 2. Rotate this back into a coordinate system where Y
    	    // runs the length of the runway and X runs crossways.
    	    //

    	    double course = az2 - heading;
    	    while ( course < -360 ) { course += 360; }
    	    while ( course > 360 ) { course -= 360; }
    	    SG_LOG( SG_GENERAL, SG_DEBUG,
                        "  course = " << course << "  dist = " << dist );

    	    //
    	    // 3. Convert from polar to cartesian coordinates
    	    //

    	    x = sin( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    y = cos( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  x = " << x << " y = " << y);

    	    //
    	    // 4. Map x, y point into texture coordinates
    	    //
    	    double tmp;

            tmp = x / width;
            tx = tmp * (maxu - minu) + minu;

            if ( tx < 0.00 ) { tx = 0.0; }
            if ( tx > 1.00 ) { tx = 1.0; }

    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ")");

            ty = (y/length) + minv;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << ty << ")");

    	    t = Point3D( tx, ty, 0 );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ", " << ty << ")");

    	    result.add_node( i, t );
    	}
    }

    return result;
}

// TODO: add to linear feature class 
static TGPolygon linear_feature_tex_coords( const TGPolygon& in_poly, const TGTexParams& tp )
{
    int i, j;
    TGPolygon result;
    result.erase();

    SGGeod ref = tp.get_ref();
    double width = tp.get_width();
    double length = tp.get_length();
    double heading = tp.get_heading();
    double minu = tp.get_minu();
    double maxu = tp.get_maxu();
    double minv = tp.get_minv();
    double maxv = tp.get_maxv();
    SG_LOG( SG_GENERAL, SG_DEBUG, "section ref = " << ref );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  width   = " << width );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  length  = " << length );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  heading = " << heading );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  minv    = " << minv );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  maxv    = " << maxv );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  heading = " << heading );

    Point3D p, t;
    double x, y, tx, ty;

    for ( i = 0; i < in_poly.contours(); ++i ) 
    {
    	for ( j = 0; j < in_poly.contour_size( i ); ++j ) 
        {
    	    p = in_poly.get_pt( i, j );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "tex coords for contour " << i << "point " << j << ": " << p );

    	    //
    	    // 1. Calculate distance and bearing from the center of
    	    // the feature
    	    //

    	    // given alt, lat1, lon1, lat2, lon2, calculate starting
    	    // and ending az1, az2 and distance (s).  Lat, lon, and
    	    // azimuth are in degrees.  distance in meters
    	    double az1, az2, dist;
    	    geo_inverse_wgs_84( 0, ref.getLatitudeDeg(), ref.getLongitudeDeg(), p.y(), p.x(),
    				&az1, &az2, &dist );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "basic course from ref = " << az2);

    	    //
    	    // 2. Rotate this back into a coordinate system where Y
    	    // runs the length of the runway and X runs crossways.
    	    //

    	    double course = az2 - heading;
    	    while ( course < -360 ) { course += 360; }
    	    while ( course > 360 ) { course -= 360; }
    	    SG_LOG( SG_GENERAL, SG_DEBUG,
                        "  course = " << course << "  dist = " << dist );

    	    //
    	    // 3. Convert from polar to cartesian coordinates
    	    //

    	    x = sin( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    y = cos( course * SGD_DEGREES_TO_RADIANS ) * dist;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  x = " << x << " y = " << y);

    	    //
    	    // 4. Map x, y point into texture coordinates
    	    //
    	    double tmp;

            tmp = x / width;
            tx = tmp * (maxu - minu) + minu;

            if ( tx < -1.0 )  { tx = -1.0; }
            if ( tx > 1.0 ) { tx = 1.0; }

    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ")");

            ty = (y/length) + minv;
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << ty << ")");

    	    t = Point3D( tx, ty, 0 );
    	    SG_LOG(SG_GENERAL, SG_DEBUG, "  (" << tx << ", " << ty << ")");

    	    result.add_node( i, t );
    	}
    }

    return result;
}

// TODO : Add somewhere
// Determine node elevations of a point_list based on the provided
// TGAptSurface.  Offset is added to the final elevation
static point_list calc_elevations( TGAptSurface &surf,
                                   const point_list& geod_nodes,
                                   double offset )
{
    point_list result = geod_nodes;
    for ( unsigned int i = 0; i < result.size(); ++i ) {
        double elev = surf.query( result[i].lon(), result[i].lat() );
        result[i].setelev( elev + offset );
    }

    return result;
}

// Determine node elevations of each node of a TGPolygon based on the
// provided TGAptSurface.  Offset is added to the final elevation
static TGPolygon calc_elevations( TGAptSurface &surf,
                                  const TGPolygon& poly,
                                  double offset )
{
    TGPolygon result;
    for ( int i = 0; i < poly.contours(); ++i ) {
        point_list contour = poly.get_contour( i );
        point_list elevated = calc_elevations( surf, contour, offset );

        result.add_contour( elevated, poly.get_hole_flag(i) );
    }

    return result;
}


void Airport::merge_slivers( superpoly_list& polys, poly_list& slivers_list ) {
    TGPolygon poly, result, slivers, sliver;
    point_list contour;
    int original_contours, result_contours;
    bool done;
    int  i, j, k;

    for ( i = 0; i < (int)slivers_list.size(); i++ ) {
        slivers = slivers_list[i];
        for ( j = 0; j < slivers.contours(); ++j ) {
            SG_LOG(SG_GENERAL, SG_DEBUG, "Merging sliver = " << i << ", " << j);

            // make the sliver polygon
            contour = slivers.get_contour( j );
            sliver.erase();
            sliver.add_contour( contour, 0 );
            done = false;

            // try to merge the slivers with the list of clipped polys
            for ( k = 0; k < (int)polys.size() && !done; ++k ) {
                poly = polys[k].get_poly();
                original_contours = poly.contours();
                result = tgPolygonUnionClipper( poly, sliver );
                result_contours = result.contours();

                if ( original_contours == result_contours ) {
                    SG_LOG(SG_GENERAL, SG_DEBUG, "    FOUND a poly to merge the sliver with");
                    polys[k].set_poly( result );
                    done = true;
                }
	        }

            if ( !done ) {
                SG_LOG(SG_GENERAL, SG_DEBUG, "couldn't merge sliver " << i << ", " << j);
	        }
        }
    }
}

void Airport::BuildBtg(const string& root, const string_list& elev_src )
{
    ClipPolyType accum;
    poly_list    slivers;

    // try to keep line accumulator in clipper format for speed...
    ClipPolyType lines;
    poly_list    line_slivers;

    TGPolygon apt_base;
    TGPolygon apt_clearing;

    // runways
    superpoly_list rwy_polys;
    texparams_list rwy_tps;

    // pavements
    superpoly_list pvmt_polys;
    texparams_list pvmt_tps;

    // linear features
    superpoly_list line_polys;
    texparams_list line_tps;

    // int i, j, k;
    Point3D p;

    bool verbose_triangulation = false;
    bool make_shapefiles = false;

    // parse main airport information
    double apt_lon = 0.0, apt_lat = 0.0;

    SGTimeStamp build_start;
    SGTimeStamp build_end;
    SGTimeStamp cleanup_start;
    SGTimeStamp cleanup_end;
    SGTimeStamp triangulation_start;
    SGTimeStamp triangulation_end;
    time_t      log_time;

    // Find the average of all the runway and heliport long / lats
    int num_samples = 0;
    for (unsigned int i=0; i<runways.size(); i++)
    {
        apt_lon += runways[i]->GetMidpoint().getLongitudeDeg();
        apt_lat += runways[i]->GetMidpoint().getLatitudeDeg();
        num_samples++;
    }
    for (unsigned int i=0; i<helipads.size(); i++)
    {
        apt_lon += helipads[i]->GetLoc().getLongitudeDeg();
        apt_lat += helipads[i]->GetLoc().getLatitudeDeg();
        num_samples++;
    }
    apt_lon = apt_lon / (double)num_samples;
    apt_lat = apt_lat / (double)num_samples;

    SGBucket b( apt_lon, apt_lat );
    SG_LOG(SG_GENERAL, SG_DEBUG, b.gen_base_path() << "/" << b.gen_index_str());

    superpoly_list rwy_lights; 
    rwy_lights.clear();

    // If we are cutting in the linear features, add them first
    if (pavements.size())
    {
        for ( unsigned int i=0; i<pavements.size(); i++ )
        {
            AddFeatures( pavements[i]->GetFeatures() );
        }
    }

    SG_LOG(SG_GENERAL, SG_INFO, "Parse Complete - Runways: " << runways.size() << " Pavements: " << pavements.size() << " Features: " << features.size() << " Taxiways: " << taxiways.size() );

    // Starting to clip the polys (for now - only UNIX builds)
    build_start.stamp();

    // Add the linear features
    if (features.size())
    {
        for ( unsigned int i=0; i<features.size(); i++ )
        {
            SG_LOG(SG_GENERAL, SG_INFO, "Build Feature Poly " << i + 1 << " of " << features.size() << " : " << features[i]->GetDescription() );

            if ( (dbg_feat_poly > 0) && (i == (unsigned int)dbg_feat_poly-1) ) {
                SG_LOG(SG_GENERAL, SG_INFO, "Problem feat poly (" << i << ")");

                make_shapefiles = true;
            } else {
                make_shapefiles = false;
            }

            features[i]->BuildBtg( &line_polys, &line_tps, &lines, &rwy_lights, make_shapefiles );
        }
    }
    else
    {
        SG_LOG(SG_GENERAL, SG_DEBUG, "no markings");
    }

    log_time = time(0);
    SG_LOG( SG_GENERAL, SG_ALERT, "Finished building Linear Features for " << icao << " at " << ctime(&log_time) );

    // Build runways next
    for ( unsigned int i=0; i<runways.size(); i++ )
    {
        SG_LOG(SG_GENERAL, SG_INFO, "Build Runway " << i + 1 << " of " << runways.size());
        slivers.clear();

        if ( (dbg_rwy_poly > 0) && (i == (unsigned int)dbg_rwy_poly-1) ) {
            SG_LOG(SG_GENERAL, SG_INFO, "Problem runway poly (" << i << ")");

            make_shapefiles = true;
        } else {
            make_shapefiles = false;
        }

        if (boundary.size())
        {
            runways[i]->BuildBtg( &rwy_polys, &rwy_tps, &rwy_lights, &accum, slivers, NULL, NULL, make_shapefiles );
        }
        else
        {
            runways[i]->BuildBtg( &rwy_polys, &rwy_tps, &rwy_lights, &accum, slivers, &apt_base, &apt_clearing, make_shapefiles );
        }

        // Now try to merge any slivers we found
        merge_slivers( rwy_polys, slivers );
    }

    log_time = time(0);
    SG_LOG( SG_GENERAL, SG_ALERT, "Finished building runways for " << icao << " at " << ctime(&log_time) );

    if (lightobjects.size())
    {
        for ( unsigned int i=0; i<lightobjects.size(); i++ )
        {
            SG_LOG(SG_GENERAL, SG_INFO, "Build runway light " << i + 1 << " of " << lightobjects.size());
            lightobjects[i]->BuildBtg( &rwy_lights );
        }
    }

    // Build helipads (use runway poly- and texture list for this)
    if (helipads.size())
    {
        for ( unsigned int i=0; i<helipads.size(); i++ )
        {
            SG_LOG(SG_GENERAL, SG_INFO, "Build helipad " << i + 1 << " of " << helipads.size());
            slivers.clear();

            if (boundary.size())
            {
                helipads[i]->BuildBtg( &rwy_polys, &rwy_tps, &rwy_lights, &accum, slivers, NULL, NULL );
            }
            else
            {
                helipads[i]->BuildBtg( &rwy_polys, &rwy_tps, &rwy_lights, &accum, slivers, &apt_base, &apt_clearing );
            }

            // Now try to merge any slivers we found
            merge_slivers( rwy_polys, slivers );
        }
    }

    // Build the pavements
    if (pavements.size())
    {
        for ( unsigned int i=0; i<pavements.size(); i++ )
        {
            SG_LOG(SG_GENERAL, SG_INFO, "Build Pavement " << i + 1 << " of " << pavements.size() << " : " << pavements[i]->GetDescription());
            slivers.clear();

            if ( (dbg_pvmt_poly > 0) && (i == (unsigned int)dbg_pvmt_poly-1) ) {
                SG_LOG(SG_GENERAL, SG_INFO, "Problem pvmt poly (" << i << ")");

                make_shapefiles = true;
            } else {
                make_shapefiles = false;
            }

            if (boundary.size())
            {
                pavements[i]->BuildBtg( &pvmt_polys, &pvmt_tps, &accum, slivers, NULL, NULL, make_shapefiles );
            }
            else
            {
                pavements[i]->BuildBtg( &pvmt_polys, &pvmt_tps, &accum, slivers, &apt_base, &apt_clearing, make_shapefiles );
            }

            // Now try to merge any slivers we found
            merge_slivers( rwy_polys, slivers );
            merge_slivers( pvmt_polys, slivers );
        }
    }
    else
    {
        SG_LOG(SG_GENERAL, SG_DEBUG, "no pavements");
    }

    log_time = time(0);
    SG_LOG( SG_GENERAL, SG_ALERT, "Finished building Pavements for " << icao << " at " << ctime(&log_time) );

    // Build the legacy taxiways
    if (taxiways.size())
    {
        for ( unsigned int i=0; i<taxiways.size(); i++ )
        {
            SG_LOG(SG_GENERAL, SG_INFO, "Build Taxiway " << i + 1 << " of " << taxiways.size());
            slivers.clear();

            if ( (dbg_taxi_poly > 0) && (i == (unsigned int)dbg_taxi_poly-1) ) {
                SG_LOG(SG_GENERAL, SG_INFO, "Problem taxi poly (" << i << ")");

                make_shapefiles = true;
            } else {
                make_shapefiles = false;
            }

            if (boundary.size())
            {
                taxiways[i]->BuildBtg( &pvmt_polys, &pvmt_tps, &rwy_lights, &accum, slivers, NULL, NULL, make_shapefiles );
            }
            else
            {
                taxiways[i]->BuildBtg( &pvmt_polys, &pvmt_tps, &rwy_lights, &accum, slivers, &apt_base, &apt_clearing, make_shapefiles );
            }

            // Now try to merge any slivers we found
            merge_slivers( rwy_polys, slivers );
            merge_slivers( pvmt_polys, slivers );
        }
    }
    else
    {
        SG_LOG(SG_GENERAL, SG_DEBUG, "no taxiways");
    }


    // Build runway shoulders here
    for ( unsigned int i=0; i<runways.size(); i++ )
    {
        SG_LOG(SG_GENERAL, SG_INFO, "Build Runway shoulder " << i + 1 << " of " << runways.size());

        if ( runways[i]->GetsShoulder() )
        {
            slivers.clear();

            if (boundary.size())
            {
                runways[i]->BuildShoulder( &rwy_polys, &rwy_tps, &accum, slivers, NULL, NULL );
            }
            else
            {
                runways[i]->BuildShoulder( &rwy_polys, &rwy_tps, &accum, slivers, &apt_base, &apt_clearing );
            }

            // Now try to merge any slivers we found
            merge_slivers( rwy_polys, slivers );
            merge_slivers( pvmt_polys, slivers );
        }
    }

    // Build helipad shoulders here
    for ( unsigned int i=0; i<helipads.size(); i++ )
    {
        SG_LOG(SG_GENERAL, SG_INFO, "Build Helipad shoulder " << i + 1 << " of " << helipads.size());

        if ( helipads[i]->GetsShoulder() )
        {
            slivers.clear();

            if (boundary.size())
            {
                helipads[i]->BuildShoulder( &rwy_polys, &rwy_tps, &accum, slivers, NULL, NULL );
            }
            else
            {
                helipads[i]->BuildShoulder( &rwy_polys, &rwy_tps, &accum, slivers, &apt_base, &apt_clearing );
            }

            // Now try to merge any slivers we found
            merge_slivers( rwy_polys, slivers );
            merge_slivers( pvmt_polys, slivers );
        }
    }

    // build the base and clearing if there's a boundary
    if (boundary.size())
    {
        for ( unsigned int i=0; i<boundary.size(); i++ )
        {
            SG_LOG(SG_GENERAL, SG_INFO, "Build Userdefined boundary " << i + 1 << " of " << boundary.size());
            boundary[i]->BuildBtg( &apt_base, &apt_clearing, false );
        }
    }

    if ( apt_base.total_size() == 0 )
    {
        SG_LOG(SG_GENERAL, SG_ALERT, "no airport points generated");
    	return;
    }


    TGPolygon filled_base  = tgPolygonStripHoles( apt_base );
    TGPolygon divided_base = tgPolygonSplitLongEdges( filled_base, 200.0 );
    TGPolygon base_poly    = tgPolygonDiffClipper( divided_base, accum );

    build_end.stamp();
    build_time = build_end - build_start;

    cleanup_start.stamp();

    // add segments to polygons to remove any possible "T"
    // intersections
    TGTriNodes tmp_pvmt_nodes;
    TGTriNodes tmp_feat_nodes;

    // build temporary node list from runways...
    SG_LOG(SG_GENERAL, SG_INFO, "Build Node List " );

    for ( unsigned int k = 0; k < rwy_polys.size(); ++k ) 
    {
    	TGPolygon poly = rwy_polys[k].get_poly();
    	for ( int i = 0; i < poly.contours(); ++i ) 
        {
    	    for ( int j = 0; j < poly.contour_size( i ); ++j ) 
            {
                tmp_pvmt_nodes.unique_add( poly.get_pt(i, j) );
    	    }
    	}
    }

    // and pavements
    for ( unsigned int k = 0; k < pvmt_polys.size(); ++k ) 
    {
    	TGPolygon poly = pvmt_polys[k].get_poly();
    	for ( int i = 0; i < poly.contours(); ++i ) 
        {
    	    for ( int j = 0; j < poly.contour_size( i ); ++j ) 
            {
        		tmp_pvmt_nodes.unique_add( poly.get_pt(i, j) );
    	    }
    	}
    }

    // and linear features ( keep Linear feature nodes seperate)
    for ( unsigned int k = 0; k < line_polys.size(); ++k ) 
    {
    	TGPolygon poly = line_polys[k].get_poly();
    	for ( int i = 0; i < poly.contours(); ++i ) 
        {
    	    for ( int j = 0; j < poly.contour_size( i ); ++j ) 
            {
        		tmp_feat_nodes.unique_add( poly.get_pt(i, j) );
    	    }
    	}
    }

    // and the base
    for ( int i = 0; i < base_poly.contours(); ++i ) 
    {
    	for ( int j = 0; j < base_poly.contour_size( i ); ++j ) 
        {
    	    tmp_pvmt_nodes.unique_add( base_poly.get_pt(i, j) );
    	}
    }

    // the divided base could contain points not found in base_poly,
    // so we should add them because the skirt needs them.
    for ( int i = 0; i < divided_base.contours(); ++i ) 
    {
	    for ( int j = 0; j < divided_base.contour_size( i ); ++j ) 
        {
    	    tmp_pvmt_nodes.unique_add( divided_base.get_pt(i, j) );
    	}
    }

    log_time = time(0);
    SG_LOG( SG_GENERAL, SG_ALERT, "Finished collecting nodes for " << icao << " at " << ctime(&log_time) );

    // second pass : runways
    for ( unsigned int k = 0; k < rwy_polys.size(); ++k ) 
    {
    	TGPolygon poly = rwy_polys[k].get_poly();
    	poly = add_nodes_to_poly( poly, tmp_pvmt_nodes );
    	SG_LOG(SG_GENERAL, SG_DEBUG, "total size after add nodes = " << poly.total_size());
    	rwy_polys[k].set_poly( poly );
    }

    // second pass : and pavements
    for ( unsigned int k = 0; k < pvmt_polys.size(); ++k ) 
    {
    	TGPolygon poly = pvmt_polys[k].get_poly();
    	poly = add_nodes_to_poly( poly, tmp_pvmt_nodes );
    	SG_LOG(SG_GENERAL, SG_DEBUG, "total size after add nodes = " << poly.total_size());
    	pvmt_polys[k].set_poly( poly );
    }

    // second pass : and lines
    for ( unsigned int k = 0; k < line_polys.size(); ++k ) 
    {
    	TGPolygon poly = line_polys[k].get_poly();
    	poly = add_nodes_to_poly( poly, tmp_feat_nodes );
    	SG_LOG(SG_GENERAL, SG_DEBUG, "total size after add nodes = " << poly.total_size());
    	line_polys[k].set_poly( poly );
    }

    log_time = time(0);
    SG_LOG( SG_GENERAL, SG_ALERT, "Finished adding intermediate nodes for " << icao << " at " << ctime(&log_time) );

    for ( unsigned int k = 0; k < line_polys.size(); ++k ) 
    {
    	TGPolygon poly = line_polys[k].get_poly();

        poly = remove_cycles( poly );
        poly = remove_dups( poly );
        poly = remove_bad_contours( poly );
        poly = tgPolygonSimplify( poly );
        poly = remove_tiny_contours( poly );
        poly = remove_spikes( poly );
        poly = remove_dups( poly );
        poly = remove_bad_contours( poly );
        poly = remove_tiny_contours( poly );

    	line_polys[k].set_poly( poly );
    }

    log_time = time(0);
    SG_LOG( SG_GENERAL, SG_ALERT, "Finished cleaning polys for " << icao << " at " << ctime(&log_time) );

    SG_LOG(SG_GENERAL, SG_DEBUG, "add nodes base ");
    SG_LOG(SG_GENERAL, SG_DEBUG, " before: " << base_poly);
    SG_LOG(SG_GENERAL, SG_DEBUG, " tmp_pvmt_nodes size = " << tmp_pvmt_nodes.get_node_list().size());
    SG_LOG(SG_GENERAL, SG_DEBUG, " tmp_feat_nodes size = " << tmp_feat_nodes.get_node_list().size());

    base_poly = add_nodes_to_poly( base_poly, tmp_pvmt_nodes );
    base_poly = snap( base_poly, gSnap );
    SG_LOG(SG_GENERAL, SG_DEBUG, " after adding tmp_nodes: " << base_poly);

    // Finally find slivers in base
    tgPolygonFindSlivers( base_poly, slivers );
    merge_slivers( rwy_polys, slivers );
    merge_slivers( pvmt_polys, slivers );

    // Then snap rwy and pavement to grid (was done right after adding intermediate nodes...)
    for ( unsigned int k = 0; k < rwy_polys.size(); ++k ) 
    {
    	TGPolygon poly = rwy_polys[k].get_poly();
        poly = snap(poly, gSnap);
        poly = remove_dups( poly );
        poly = remove_bad_contours( poly );
    	rwy_polys[k].set_poly( poly );
    }
    for ( unsigned int k = 0; k < pvmt_polys.size(); ++k ) 
    {
    	TGPolygon poly = pvmt_polys[k].get_poly();
        poly = snap(poly, gSnap);
        poly = remove_dups( poly );
        poly = remove_bad_contours( poly );
    	pvmt_polys[k].set_poly( poly );
    }

    cleanup_end.stamp();
    cleanup_time = cleanup_end - cleanup_start;

    triangulation_start.stamp();

    // tesselate the polygons and prepair them for final output
    for ( unsigned int i = 0; i < rwy_polys.size(); ++i ) 
    {
        SG_LOG(SG_GENERAL, SG_INFO, "Tesselating runway poly = " << i + 1 << " of " << rwy_polys.size() << " : flag = " << rwy_polys[i].get_flag());

    	TGPolygon poly = rwy_polys[i].get_poly();

        if ( (dbg_rwy_poly > 0) && (i == (unsigned int)dbg_rwy_poly-1) ) {
            SG_LOG(SG_GENERAL, SG_INFO, "Problem rwy poly (" << i << ") : " << poly );

            tgChopNormalPolygon( "/home/pete", "rwy", poly, false );

            verbose_triangulation = true;
        } else {
            verbose_triangulation = false;
        }

	    SG_LOG(SG_GENERAL, SG_DEBUG, "contours before " << poly.contours() << " total points before = " << poly.total_size());
        TGPolygon tri = polygon_tesselate_alt_cgal( poly, verbose_triangulation );
	    SG_LOG(SG_GENERAL, SG_DEBUG, "total size after = " << tri.total_size());

        TGPolygon tc;
        if (rwy_polys[i].get_flag() == "shoulder")
        {
            tc = shoulder_tex_coords( tri, rwy_tps[i] );
        }
        else
        {
            tc = rwy_section_tex_coords( tri, rwy_tps[i], true );
        }

    	rwy_polys[i].set_tris( tri );
	    rwy_polys[i].set_texcoords( tc );
    }

    // tesselate the polygons and prepair them for final output
    for ( unsigned int i = 0; i < pvmt_polys.size(); ++i ) 
    {
        SG_LOG(SG_GENERAL, SG_INFO, "Tesselating pavement poly = " << i + 1 << " of " << pvmt_polys.size() << " : flag = " << pvmt_polys[i].get_flag());

    	TGPolygon poly = pvmt_polys[i].get_poly();

        if ( (dbg_pvmt_poly > 0) && (i == (unsigned int)dbg_pvmt_poly-1) ) {
            SG_LOG(SG_GENERAL, SG_INFO, "Problem pvmt poly (" << i << ") : " << poly );

            tgChopNormalPolygon( "/home/pete", "pvmt", poly, false );
            verbose_triangulation = true;
        } else {
            verbose_triangulation = false;
        }

	    SG_LOG(SG_GENERAL, SG_DEBUG, "contours before " << poly.contours() << " total points before = " << poly.total_size());
	    TGPolygon tri = polygon_tesselate_alt_cgal( poly, verbose_triangulation );
	    SG_LOG(SG_GENERAL, SG_DEBUG, "total size after = " << tri.total_size());

        TGPolygon tc;
        if (pvmt_polys[i].get_flag() == "lf")
        {
            tc = linear_feature_tex_coords( tri, pvmt_tps[i] );
        }
        else
        {
            tc = rwy_section_tex_coords( tri, pvmt_tps[i], false );
        }

    	pvmt_polys[i].set_tris( tri );
	    pvmt_polys[i].set_texcoords( tc );
    }

    // tesselate the polygons and prepair them for final output
    for ( unsigned int i = 0; i < line_polys.size(); ++i ) 
    {
        SG_LOG(SG_GENERAL, SG_INFO, "Tesselating line poly = " << i + 1 << " of " << line_polys.size() << " : flag = " << line_polys[i].get_flag());

    	TGPolygon poly = line_polys[i].get_poly();

        if ( (dbg_feat_poly > 0) && (i == (unsigned int)dbg_feat_poly-1) ) {
            SG_LOG(SG_GENERAL, SG_INFO, "Problem feat poly (" << i << ") : " << poly );

            tgChopNormalPolygon( "/home/pete/", "feat", poly, false );
            verbose_triangulation = true;
        } else {
            verbose_triangulation = false;
        }

  	    SG_LOG(SG_GENERAL, SG_DEBUG, "contours before " << poly.contours() << " total points before = " << poly.total_size());
        TGPolygon tri = polygon_tesselate_alt_cgal( poly, verbose_triangulation );
        SG_LOG(SG_GENERAL, SG_DEBUG, "total size after = " << tri.total_size());

        TGPolygon tc;
        tc = linear_feature_tex_coords( tri, line_tps[i] );

      	line_polys[i].set_tris( tri );
        line_polys[i].set_texcoords( tc );
    }

    if ( dbg_base_poly > 0 ) {
        SG_LOG(SG_GENERAL, SG_INFO, "Problem base poly: " << base_poly );

        tgChopNormalPolygon( "/home/pete/", "Base", base_poly, false );
        verbose_triangulation = true;
    }

    /* before tessellating the base, make sure there are no
       intersecting contours */
    base_poly = tgPolygonSimplify( base_poly );

    SG_LOG(SG_GENERAL, SG_INFO, "Tesselating base poly ");
    TGPolygon base_tris = polygon_tesselate_alt_cgal( base_poly, verbose_triangulation );
    verbose_triangulation = false;

    SG_LOG(SG_GENERAL, SG_INFO, "Tesselating base poly - done : contours = " << base_tris.contours());

    triangulation_end.stamp();
    triangulation_time = triangulation_end - triangulation_start;

    //
    // We should now have the runway polygons all generated with their
    // corresponding triangles and texture coordinates, and the
    // surrounding base area.
    //
    // Next we need to create the output lists ... vertices, normals,
    // texture coordinates, and tri-strips
    //

    // traverse the tri list and create ordered node and texture
    // coordinate lists

    TGTriNodes nodes, normals, texcoords;
    nodes.clear();
    normals.clear();
    texcoords.clear();

    group_list pts_v; pts_v.clear();
    group_list pts_n; pts_n.clear();
    string_list pt_materials; pt_materials.clear();

    group_list tris_v; tris_v.clear();
    group_list tris_n; tris_n.clear();
    group_list tris_tc; tris_tc.clear();
    string_list tri_materials; tri_materials.clear();

    group_list strips_v; strips_v.clear();
    group_list strips_n; strips_n.clear();
    group_list strips_tc; strips_tc.clear();
    string_list strip_materials; strip_materials.clear();

    int index;
    int_list pt_v, tri_v, strip_v;
    int_list pt_n, tri_n, strip_n;
    int_list tri_tc, strip_tc;

    // calculate "the" normal for this airport
    SGVec3d vnt = SGVec3d::fromGeod( base_tris.get_pt(0, 0).toSGGeod() );
    vnt = normalize(vnt);
    Point3D vn = Point3D::fromSGVec3(vnt);
    SG_LOG(SG_GENERAL, SG_DEBUG, "found normal for this airport = " << vn);

    SG_LOG(SG_GENERAL, SG_INFO, "Adding runway nodes and normals");
    for ( unsigned int k = 0; k < rwy_polys.size(); ++k ) 
    {
    	SG_LOG(SG_GENERAL, SG_DEBUG, "tri " << k);
    	// TGPolygon tri_poly = rwy_tris[k];
    	TGPolygon tri_poly = rwy_polys[k].get_tris();
    	TGPolygon tri_txs = rwy_polys[k].get_texcoords();
    	string material = rwy_polys[k].get_material();
    	SG_LOG(SG_GENERAL, SG_DEBUG, "material = " << material);
    	SG_LOG(SG_GENERAL, SG_DEBUG, "poly size = " << tri_poly.contours());
    	SG_LOG(SG_GENERAL, SG_DEBUG, "texs size = " << tri_txs.contours());
    	for ( int i = 0; i < tri_poly.contours(); ++i ) 
        {
    	    tri_v.clear();
    	    tri_n.clear();
    	    tri_tc.clear();
    	    for ( int j = 0; j < tri_poly.contour_size(i); ++j ) 
            {
        		p = tri_poly.get_pt( i, j );
        		index = nodes.unique_add( p );
                SG_LOG(SG_GENERAL, SG_DEBUG, "added rwy point " << p << " at " << index );
        		tri_v.push_back( index );

        		// use 'the' normal
        		index = normals.unique_add( vn );
        		tri_n.push_back( index );

        		Point3D tc = tri_txs.get_pt( i, j );
        		index = texcoords.unique_add( tc );
        		tri_tc.push_back( index );
    	    }
    	    tris_v.push_back( tri_v );
    	    tris_n.push_back( tri_n );
    	    tris_tc.push_back( tri_tc );
    	    tri_materials.push_back( material );
    	}
    }

    SG_LOG(SG_GENERAL, SG_INFO, "Adding pavement nodes and normals");
    for ( unsigned int k = 0; k < pvmt_polys.size(); ++k ) 
    {
    	SG_LOG(SG_GENERAL, SG_DEBUG, "tri " << k);
    	// TGPolygon tri_poly = rwy_tris[k];
    	TGPolygon tri_poly = pvmt_polys[k].get_tris();
    	TGPolygon tri_txs = pvmt_polys[k].get_texcoords();
    	string material = pvmt_polys[k].get_material();
    	SG_LOG(SG_GENERAL, SG_DEBUG, "material = " << material);
    	SG_LOG(SG_GENERAL, SG_DEBUG, "poly size = " << tri_poly.contours());
    	SG_LOG(SG_GENERAL, SG_DEBUG, "texs size = " << tri_txs.contours());
    	for ( int i = 0; i < tri_poly.contours(); ++i ) 
        {
    	    tri_v.clear();
    	    tri_n.clear();
    	    tri_tc.clear();
    	    for ( int j = 0; j < tri_poly.contour_size(i); ++j ) 
            {
        		p = tri_poly.get_pt( i, j );
        		index = nodes.unique_add( p );
                SG_LOG(SG_GENERAL, SG_DEBUG, "added pvmnt point " << p << " at " << index );
        		tri_v.push_back( index );

        		// use 'the' normal
        		index = normals.unique_add( vn );
        		tri_n.push_back( index );

        		Point3D tc = tri_txs.get_pt( i, j );
        		index = texcoords.unique_add( tc );
        		tri_tc.push_back( index );
    	    }
    	    tris_v.push_back( tri_v );
    	    tris_n.push_back( tri_n );
    	    tris_tc.push_back( tri_tc );
    	    tri_materials.push_back( material );
    	}
    }

    SG_LOG(SG_GENERAL, SG_INFO, "Adding line nodes and normals");
    for ( unsigned int k = 0; k < line_polys.size(); ++k ) 
    {
    	SG_LOG(SG_GENERAL, SG_DEBUG, "tri " << k);
    	// TGPolygon tri_poly = rwy_tris[k];
    	TGPolygon tri_poly = line_polys[k].get_tris();
    	TGPolygon tri_txs = line_polys[k].get_texcoords();
    	string material = line_polys[k].get_material();
    	SG_LOG(SG_GENERAL, SG_DEBUG, "material = " << material);
    	SG_LOG(SG_GENERAL, SG_DEBUG, "poly size = " << tri_poly.contours());
    	SG_LOG(SG_GENERAL, SG_DEBUG, "texs size = " << tri_txs.contours());
    	for ( int i = 0; i < tri_poly.contours(); ++i ) 
        {
    	    tri_v.clear();
    	    tri_n.clear();
    	    tri_tc.clear();
    	    for ( int j = 0; j < tri_poly.contour_size(i); ++j ) 
            {
        		p = tri_poly.get_pt( i, j );
        		index = nodes.unique_add( p );
                SG_LOG(SG_GENERAL, SG_DEBUG, "added line point " << p << " at " << index );
        		tri_v.push_back( index );

        		// use 'the' normal
        		index = normals.unique_add( vn );
        		tri_n.push_back( index );

        		Point3D tc = tri_txs.get_pt( i, j );
        		index = texcoords.unique_add( tc );
        		tri_tc.push_back( index );
    	    }
    	    tris_v.push_back( tri_v );
    	    tris_n.push_back( tri_n );
    	    tris_tc.push_back( tri_tc );
    	    tri_materials.push_back( material );
    	}
    }

    // add base points
    std::vector< SGVec2f > base_txs; 
    int_list base_tc;

    SG_LOG(SG_GENERAL, SG_INFO, "Adding base nodes and normals");
    for ( int i = 0; i < base_tris.contours(); ++i ) 
    {
    	tri_v.clear();
    	tri_n.clear();
    	tri_tc.clear();
    	for ( int j = 0; j < base_tris.contour_size(i); ++j ) 
        {
    	    p = base_tris.get_pt( i, j );
    	    index = nodes.unique_add( p );
            SG_LOG(SG_GENERAL, SG_DEBUG, "added base point " << p << " at " << index );
    	    tri_v.push_back( index );

    	    index = normals.unique_add( vn );
    	    tri_n.push_back( index);
    	}
    	tris_v.push_back( tri_v );
    	tris_n.push_back( tri_n );
    	tri_materials.push_back( "Grass" );

    	std::vector < SGGeod > geodNodes;
    	for ( unsigned int j = 0; j < nodes.get_node_list().size(); j++ ) 
        {
    	    Point3D node = nodes.get_node_list()[j];
    	    geodNodes.push_back( SGGeod::fromDegM( node.x(), node.y(), node.z() ) );
    	}
	    base_txs.clear();
    	base_txs = sgCalcTexCoords( b, geodNodes, tri_v );

    	base_tc.clear();
    	for ( unsigned int j = 0; j < base_txs.size(); ++j ) 
        {
    	    SGVec2f tc = base_txs[j];
    	    // SG_LOG(SG_GENERAL, SG_DEBUG, "base_tc = " << tc);
    	    index = texcoords.simple_add( Point3D( tc.x(), tc.y(), 0 ) );
    	    base_tc.push_back( index );
    	}
    	tris_tc.push_back( base_tc );
    }

    // on rare occasion, one or more of the divided base points can be
    // missed.  Make sure they are all in the node list so we can
    // build a proper skirt.
    for ( int i = 0; i < divided_base.contours(); ++i ) 
    {
    	for ( int j = 0; j < divided_base.contour_size( i ); ++j ) 
        {
    	    index = nodes.unique_add( divided_base.get_pt(i, j) );
            SG_LOG(SG_GENERAL, SG_DEBUG, "added base point " << divided_base.get_pt(i, j) << " at " << index );
    	}
    }

    // Now that we have assembled all the airport geometry nodes into
    // a list, calculate an "average" airport elevation based on all
    // the actual airport node points.  This is more useful than
    // calculating an average over the entire airport surface because
    // it avoids biases introduced from the surrounding area if the
    // airport is located in a bowl or on a hill.

    SG_LOG(SG_GENERAL, SG_DEBUG, " calc average elevation");
    {
        point_list dbg = nodes.get_node_list();

        // dump the node list
        SG_LOG(SG_GENERAL, SG_DEBUG, " node list size is " << dbg.size() );
        for (unsigned int w = 0; w<dbg.size(); w++)
        {
            SG_LOG(SG_GENERAL, SG_DEBUG, " node " << w << " is " << dbg[w] );            
        }
    }

    double average = tgAverageElevation( root, elev_src, nodes.get_node_list() );

    // cout << "average airport elevation = " << average << endl;

    // Now build the fitted airport surface ...

    // calculation min/max coordinates of airport area
    SG_LOG(SG_GENERAL, SG_DEBUG, " calculation min/max coordinates of airport area");

    Point3D min_deg(9999.0, 9999.0, 0), max_deg(-9999.0, -9999.0, 0);
    for ( unsigned int j = 0; j < nodes.get_node_list().size(); ++j ) 
    {
        Point3D p = nodes.get_node_list()[j];
        if ( p.lon() < min_deg.lon() ) 
        {
            SG_LOG(SG_GENERAL, SG_DEBUG, "new min lon from node " << j << " is " << p.lon() );
            min_deg.setlon( p.lon() );
        }
        if ( p.lon() > max_deg.lon() ) 
        {
            SG_LOG(SG_GENERAL, SG_DEBUG, "new max lon from node " << j << " is " << p.lon() );
            max_deg.setlon( p.lon() );
        }
        if ( p.lat() < min_deg.lat() ) 
        {
            SG_LOG(SG_GENERAL, SG_DEBUG, "new min lat from node " << j << " is " << p.lat() );
            min_deg.setlat( p.lat() );
        }
        if ( p.lat() > max_deg.lat() ) 
        {
            SG_LOG(SG_GENERAL, SG_DEBUG, "new max lat from node " << j << " is " << p.lat() );
            max_deg.setlat( p.lat() );
        }
    }

    SG_LOG(SG_GENERAL, SG_DEBUG, "Before extending for lights: min = " << min_deg << " max = " << max_deg );

    // extend the min/max coordinates of airport area to cover all
    // lights as well

    SG_LOG(SG_GENERAL, SG_DEBUG, " extend the min/max coordinates of airport area to cover all lights as well : num rwy lights is " << rwy_lights.size() );
    for ( unsigned int i = 0; i < rwy_lights.size(); ++i ) 
    {
        SG_LOG(SG_GENERAL, SG_DEBUG, " extend the min/max coordinates of airport area to cover all lights as well : rwy light " << i << "has " << rwy_lights[i].get_poly().get_contour(0).size() << " lights " );

        for ( unsigned int j = 0; j < rwy_lights[i].get_poly().get_contour(0).size(); ++j )
        {
            Point3D p = rwy_lights[i].get_poly().get_contour(0)[j];
            if ( p.lon() < min_deg.lon() ) 
            {
                min_deg.setlon( p.lon() );
            }
            if ( p.lon() > max_deg.lon() ) 
            {
                max_deg.setlon( p.lon() );
            }
            if ( p.lat() < min_deg.lat() ) 
            {
                min_deg.setlat( p.lat() );
            }
            if ( p.lat() > max_deg.lat() ) 
            {
                max_deg.setlat( p.lat() );
            }
        }
    }

    // Extend the area a bit so we don't have wierd things on the edges
    double dlon = max_deg.lon() - min_deg.lon();
    double dlat = max_deg.lat() - min_deg.lat();
    min_deg.setlon( min_deg.lon() - 0.01 * dlon );
    max_deg.setlon( max_deg.lon() + 0.01 * dlon );
    min_deg.setlat( min_deg.lat() - 0.01 * dlat );
    max_deg.setlat( max_deg.lat() + 0.01 * dlat );
    SG_LOG(SG_GENERAL, SG_INFO, "min = " << min_deg << " max = " << max_deg );

    TGAptSurface apt_surf( root, elev_src, min_deg, max_deg, average );
    SG_LOG(SG_GENERAL, SG_DEBUG, "Airport surface created");

    // calculate node elevations
    SG_LOG(SG_GENERAL, SG_DEBUG, "Computing airport node elevations");
    point_list geod_nodes = calc_elevations( apt_surf,
                                             nodes.get_node_list(),
                                             0.0 );
    divided_base = calc_elevations( apt_surf, divided_base, 0.0 );

    SG_LOG(SG_GENERAL, SG_DEBUG, "Done with base calc_elevations()");

#if 0 // TODO
    SG_LOG(SG_GENERAL, SG_INFO, "Computing tower node elevations");
    point_list tower_nodes = calc_elevations( apt_surf, towers, 0.0 );
#endif

    // calc windsock elevations:
    SG_LOG(SG_GENERAL, SG_DEBUG, "Computing windsock node elevations");
    point_list ws_nodes;
    ws_nodes.clear();
    for ( unsigned int i = 0; i < windsocks.size(); ++i ) 
    {
        p = windsocks[i]->GetLoc();
        ws_nodes.push_back( p );
    }
    point_list windsock_nodes = calc_elevations( apt_surf, ws_nodes, 0.0 );
    SG_LOG(SG_GENERAL, SG_DEBUG, "Done");

    SG_LOG(SG_GENERAL, SG_DEBUG, "Computing beacon node elevations");
    point_list b_nodes;
    b_nodes.clear();
    for ( unsigned int i = 0; i < beacons.size(); ++i ) 
    {
        p = beacons[i]->GetLoc();
        b_nodes.push_back( p );
    }
    point_list beacon_nodes = calc_elevations( apt_surf, b_nodes, 0.0 );
    SG_LOG(SG_GENERAL, SG_DEBUG, "Done");

    // calc taxiway sign elevations:
    SG_LOG(SG_GENERAL, SG_DEBUG, "Computing taxiway sign node elevations");
    point_list ts_nodes;
    ts_nodes.clear();
    for ( unsigned int i = 0; i < signs.size(); ++i ) 
    {
        p = signs[i]->GetLoc();
        ts_nodes.push_back( p );
    }
    point_list taxisigns_nodes = calc_elevations( apt_surf, ts_nodes, 0.0 );
    SG_LOG(SG_GENERAL, SG_DEBUG, "Done");

    // calc water runway buoys elevations:
    SG_LOG(SG_GENERAL, SG_DEBUG, "Computing water buoy elevations");
    point_list buoy_nodes;
    buoy_nodes.clear();
    for ( unsigned int i = 0; i < waterrunways.size(); ++i )
    {
        buoy_nodes = waterrunways[i]->GetNodes();
    }
    point_list water_buoys_nodes = calc_elevations( apt_surf, buoy_nodes, 0.0 );

    // add base skirt (to hide potential cracks)
    // this has to happen after we've calculated the node elevations
    // but before we convert to wgs84 coordinates

    int uindex, lindex;

    for ( int i = 0; i < divided_base.contours(); ++i )
    {
        strip_v.clear();
        strip_n.clear();
        strip_tc.clear();

        // prime the pump ...
        p = divided_base.get_pt( i, 0 );
        uindex = nodes.find( p );
        if ( uindex >= 0 )
        {
            Point3D lower = geod_nodes[uindex] - Point3D(0, 0, 20);
            SG_LOG(SG_GENERAL, SG_DEBUG, geod_nodes[uindex] << " <-> " << lower);
            lindex = nodes.simple_add( lower );
            geod_nodes.push_back( lower );
            strip_v.push_back( uindex );
            strip_v.push_back( lindex );

            // use 'the' normal.  We are pushing on two nodes so we
            // need to push on two normals.
            index = normals.unique_add( vn );
            strip_n.push_back( index );
            strip_n.push_back( index );
        }
        else
        {
            string message = "Ooops missing node when building skirt (in init)";
            SG_LOG( SG_GENERAL, SG_ALERT, message << " " << p );
            throw sg_exception( message );
        }

        // loop through the list
        for ( int j = 1; j < divided_base.contour_size(i); ++j )
        {
            p = divided_base.get_pt( i, j );
            uindex = nodes.find( p );
            if ( uindex >= 0 )
            {
                Point3D lower = geod_nodes[uindex] - Point3D(0, 0, 20);
                SG_LOG(SG_GENERAL, SG_DEBUG, geod_nodes[uindex] << " <-> " << lower);
                lindex = nodes.simple_add( lower );
                geod_nodes.push_back( lower );
                strip_v.push_back( uindex );
                strip_v.push_back( lindex );

                index = normals.unique_add( vn );
                strip_n.push_back( index );
                strip_n.push_back( index );
            }
            else
            {
                string message = "Ooops missing node when building skirt (in loop)";
                SG_LOG( SG_GENERAL, SG_ALERT, message << " " << p );
                throw sg_exception( message );
            }
        }

        // close off the loop
        p = divided_base.get_pt( i, 0 );
        uindex = nodes.find( p );
        if ( uindex >= 0 )
        {
            Point3D lower = geod_nodes[uindex] - Point3D(0, 0, 20);
            SG_LOG(SG_GENERAL, SG_DEBUG, geod_nodes[uindex] << " <-> " << lower);
            lindex = nodes.simple_add( lower );
            geod_nodes.push_back( lower );
            strip_v.push_back( uindex );
            strip_v.push_back( lindex );

            index = normals.unique_add( vn );
            strip_n.push_back( index );
            strip_n.push_back( index );
        }
        else
        {
            string message = "Ooops missing node when building skirt (at end)";
            SG_LOG( SG_GENERAL, SG_ALERT, message << " " << p );
            throw sg_exception( message );
        }

        strips_v.push_back( strip_v );
        strips_n.push_back( strip_n );
        strip_materials.push_back( "Grass" );

        std::vector < SGGeod > geodNodes;
        for ( unsigned int j = 0; j < nodes.get_node_list().size(); j++ )
        {
            Point3D node = nodes.get_node_list()[j];
            geodNodes.push_back( SGGeod::fromDegM( node.x(), node.y(), node.z() ) );
        }
        base_txs.clear();
        base_txs = sgCalcTexCoords( b, geodNodes, strip_v );

        base_tc.clear();
        for ( unsigned int j = 0; j < base_txs.size(); ++j )
        {
            SGVec2f tc = base_txs[j];
            // SG_LOG(SG_GENERAL, SG_DEBUG, "base_tc = " << tc);
            index = texcoords.simple_add( Point3D( tc.x(), tc.y(), 0 ) );
            base_tc.push_back( index );
        }
        strips_tc.push_back( base_tc );
    }

    // add light points
    superpoly_list tmp_light_list; 
    tmp_light_list.clear();
    typedef map < string, double, less<string> > elev_map_type;
    typedef elev_map_type::const_iterator const_elev_map_iterator;
    elev_map_type elevation_map;

    SG_LOG(SG_GENERAL, SG_DEBUG, "Computing runway/approach lighting elevations");

    // pass one, calculate raw elevations from Array
    for ( unsigned int i = 0; i < rwy_lights.size(); ++i ) 
    {
        TGTriNodes light_nodes;
        light_nodes.clear();
        point_list lights_v = rwy_lights[i].get_poly().get_contour(0);
        for ( unsigned int j = 0; j < lights_v.size(); ++j ) 
        {
            p = lights_v[j];
            index = light_nodes.simple_add( p );
        }

        // calculate light node elevations
        point_list geod_light_nodes = calc_elevations( apt_surf, light_nodes.get_node_list(), 0.0 );
        TGPolygon p;
        p.add_contour( geod_light_nodes, 0 );
        TGSuperPoly s;
        s.set_poly( p );
        tmp_light_list.push_back( s );

        string flag = rwy_lights[i].get_flag();
        if ( flag != (string)"" ) 
        {
            double max = -9999;
            const_elev_map_iterator it = elevation_map.find( flag );
            if ( it != elevation_map.end() ) 
            {
                max = elevation_map[flag];
            }
            for ( unsigned int j = 0; j < geod_light_nodes.size(); ++j ) 
            {
                if ( geod_light_nodes[j].z() > max ) 
                {
                    max = geod_light_nodes[j].z();
                }
            }
            elevation_map[flag] = max;
            SG_LOG( SG_GENERAL, SG_DEBUG, flag << " max = " << max );
        }
    }

    SG_LOG(SG_GENERAL, SG_DEBUG, "Done with lighting calc_elevations() num light polys is " << rwy_lights.size() );

    // pass two, for each light group check if we need to lift (based
    // on flag) and do so, then output next structures.
    for ( unsigned int i = 0; i < rwy_lights.size(); ++i ) 
    {
        // tmp_light_list is a parallel structure to rwy_lights
        point_list geod_light_nodes = tmp_light_list[i].get_poly().get_contour(0);
        
        // this is a little round about, but what we want to calculate the
        // light node elevations as ground + an offset so we do them
        // seperately, then we add them back into nodes to get the index
        // out, but also add them to geod_nodes to maintain consistancy
        // between these two lists.
        point_list light_normals = rwy_lights[i].get_normals().get_contour(0);
        pt_v.clear();
        pt_n.clear();
        for ( unsigned int j = 0; j < geod_light_nodes.size(); ++j ) 
        {
            p = geod_light_nodes[j];
            index = nodes.simple_add( p );
            pt_v.push_back( index );
            geod_nodes.push_back( p );

            index = normals.unique_add( light_normals[j] );
            pt_n.push_back( index );
        }
        pts_v.push_back( pt_v );
        pts_n.push_back( pt_n );
        pt_materials.push_back( rwy_lights[i].get_material() );
    }

    // calculate wgs84 mapping of nodes
    std::vector< SGVec3d > wgs84_nodes;
    for ( unsigned int i = 0; i < geod_nodes.size(); ++i ) 
    {
        SGGeod geod = SGGeod::fromDegM( geod_nodes[i].x(), geod_nodes[i].y(), geod_nodes[i].z() );
    	SG_LOG(SG_GENERAL, SG_DEBUG, "geod pt = " << geod_nodes[i] );
        SGVec3d cart = SGVec3d::fromGeod(geod);
        SG_LOG(SG_GENERAL, SG_DEBUG, "  cart pt = " << cart );
    	wgs84_nodes.push_back( cart );
    }
    SGSphered d;
    for ( unsigned int i = 0; i < wgs84_nodes.size(); ++i ) 
    {
        d.expandBy(wgs84_nodes[ i ]);
    }
    
    SGVec3d gbs_center = d.getCenter();
    double gbs_radius = d.getRadius();
    SG_LOG(SG_GENERAL, SG_DEBUG, "gbs center = " << gbs_center);
    SG_LOG(SG_GENERAL, SG_DEBUG, "Done with wgs84 node mapping");
    SG_LOG(SG_GENERAL, SG_DEBUG, "  center = " << gbs_center << " radius = " << gbs_radius );

    // null structures
    group_list fans_v; fans_v.clear();
    group_list fans_n; fans_n.clear();
    group_list fans_tc; fans_tc.clear();
    string_list fan_materials; fan_materials.clear();

    string objpath = root + "/AirportObj";
    string name = icao + ".btg";
    
    std::vector< SGVec3f > normals_3f;
    for ( unsigned int i = 0; i < normals.get_node_list().size(); i++ ) 
    {
        Point3D node = normals.get_node_list()[i];
        normals_3f.push_back( node.toSGVec3f() );
    }

    std::vector< SGVec2f > texcoords_2f;
    for ( unsigned int i = 0; i < texcoords.get_node_list().size(); i++ ) 
    {
        Point3D node = texcoords.get_node_list()[i];
        texcoords_2f.push_back( node.toSGVec2f() );
    }

    SGBinObject obj;

    obj.set_gbs_center( gbs_center );
    obj.set_gbs_radius( gbs_radius );
    obj.set_wgs84_nodes( wgs84_nodes );
    obj.set_normals( normals_3f );
    obj.set_texcoords( texcoords_2f );
    obj.set_pts_v( pts_v );
    obj.set_pts_n( pts_n );
    obj.set_pt_materials( pt_materials );
    obj.set_tris_v( tris_v );
    obj.set_tris_n( tris_n );
    obj.set_tris_tc( tris_tc ); 
    obj.set_tri_materials( tri_materials );
    obj.set_strips_v( strips_v );
    obj.set_strips_n( strips_n );
    obj.set_strips_tc( strips_tc ); 
    obj.set_strip_materials( strip_materials );
    obj.set_fans_v( fans_v );
    obj.set_fans_n( fans_n );
    obj.set_fans_tc( fans_tc );
    obj.set_fan_materials( fan_materials );

    bool result;
    result = obj.write_bin( objpath, name, b );
    if ( !result ) 
    {
        throw sg_exception("error writing file. :-(");
    }

    // write out airport object reference
    write_index( objpath, b, name );

#if 0 // TODO : along with taxiway signs
    // write out tower references
    for ( i = 0; i < (int)tower_nodes.size(); ++i ) 
    {
        write_index_shared( objpath, b, tower_nodes[i],
                            "Models/Airport/tower.xml",
                            0.0 );
    }
#endif

    // write out windsock references : TODO - save elevation data in the windsock object
    for ( unsigned int i = 0; i < windsock_nodes.size(); ++i ) 
    {
    	if ( windsocks[i]->IsLit() ) 
        {
            write_index_shared( objpath, b, windsock_nodes[i],
                                "Models/Airport/windsock_lit.xml",
                                0.0 );
        }
        else
        {
            write_index_shared( objpath, b, windsock_nodes[i],
                                "Models/Airport/windsock.xml",
                                0.0 );
    	} 
    }

    // write out beacon references
    for ( unsigned int i = 0; i < beacon_nodes.size(); ++i ) 
    {
        write_index_shared( objpath, b, beacon_nodes[i],
                            "Models/Airport/beacon.xml",
                            0.0 );
    }

    // write out taxiway signs references
    for ( unsigned int i = 0; i < taxisigns_nodes.size(); ++i ) 
    {
        write_object_sign( objpath, b, taxisigns_nodes[i],
                            signs[i]->GetDefinition(),
                            signs[i]->GetHeading(),
                            signs[i]->GetSize() );
    }

    // write out water buoys
    for ( unsigned int i = 0; i < water_buoys_nodes.size(); ++i )
    {
    	write_index_shared( objpath, b, water_buoys_nodes[i],
                            "Models/Airport/water_rw_buoy.xml",
                            0.0 );
    }

    string holepath = root + "/AirportArea";
    // long int poly_index = poly_index_next();
    // write_boundary( holepath, b, hull, poly_index );
    tgChopNormalPolygon( holepath, "Hole", divided_base, true );
    tgChopNormalPolygon( holepath, "Airport", apt_clearing, false );

    //SG_LOG( SG_GENERAL, SG_ALERT, SGLOG_GREEN << "\nSUCCESS generating " << icao << SGLOG_NORMAL << "\n" );
}
