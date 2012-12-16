#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>

#include <simgear/debug/logstream.hxx>
#include <simgear/structure/exception.hxx>
#include <simgear/misc/sg_path.hxx>
#include <simgear/io/lowlevel.hxx>


#include "tg_chopper.hxx"

void tgChopper::Clip( const tgPolygon& subject,
                      const std::string& type,
                      SGBucket& b )
{
    // p;

    SGGeod min, max;
    SGGeod c    = b.get_center();
    double span = b.get_width();
    tgPolygon base, result;

    // calculate bucket dimensions
    if ( (c.getLatitudeDeg() >= -89.0) && (c.getLatitudeDeg() < 89.0) ) {
        min = SGGeod::fromDeg( c.getLongitudeDeg() - span/2.0, c.getLatitudeDeg() - SG_HALF_BUCKET_SPAN );
        max = SGGeod::fromDeg( c.getLongitudeDeg() + span/2.0, c.getLatitudeDeg() + SG_HALF_BUCKET_SPAN );
    } else if ( c.getLatitudeDeg() < -89.0) {
        min = SGGeod::fromDeg( -90.0, -180.0 );
        max = SGGeod::fromDeg( -89.0,  180.0 );
    } else if ( c.getLatitudeDeg() >= 89.0) {
        min = SGGeod::fromDeg(  89.0, -180.0 );
        max = SGGeod::fromDeg(  90.0,  180.0 );
    } else {
        SG_LOG( SG_GENERAL, SG_ALERT,  "Out of range latitude in clip_and_write_poly() = " << c.getLatitudeDeg() );
    }

    SG_LOG( SG_GENERAL, SG_DEBUG, "  (" << min << ") (" << max << ")" );

    // set up clipping tile
    base.AddNode( 0, SGGeod::fromDeg( min.getLongitudeDeg(), min.getLatitudeDeg()) );
    base.AddNode( 0, SGGeod::fromDeg( max.getLongitudeDeg(), min.getLatitudeDeg()) );
    base.AddNode( 0, SGGeod::fromDeg( max.getLongitudeDeg(), max.getLatitudeDeg()) );
    base.AddNode( 0, SGGeod::fromDeg( min.getLongitudeDeg(), max.getLatitudeDeg()) );

    SG_LOG(SG_GENERAL, SG_DEBUG, "shape contours = " << subject.Contours() );
    for ( unsigned int ii = 0; ii < subject.Contours(); ii++ ) {
        SG_LOG(SG_GENERAL, SG_DEBUG, "   hole = " << subject.GetContour(ii).GetHole() );
    }

    result = tgPolygon::Intersect( subject, base );

    SG_LOG(SG_GENERAL, SG_DEBUG, "result contours = " << result.Contours() );
    for ( unsigned int ii = 0; ii < result.Contours(); ii++ ) {
        SG_LOG(SG_GENERAL, SG_DEBUG, "  hole = " << result.GetContour(ii).GetHole() );
    }

    if ( subject.GetPreserve3D() ) {
        result.InheritElevations( subject );
    }

    if ( result.Contours() > 0 ) {
        result.SetPreserve3D( subject.GetPreserve3D() );
        result.SetTexParams( subject.GetTexParams() );
        if ( subject.GetTexMethod() == TG_TEX_BY_GEODE ) {
            // need to set center latitude for geodetic texturing
            result.SetTexMethod( TG_TEX_BY_GEODE, b.get_center_lat() );
        }
        result.SetFlag(type);

        lock.lock();
        bp_map[b.gen_index()].push_back( result );
        lock.unlock();
    }
}

void tgChopper::Add( const tgPolygon& subject, const std::string& type )
{
    tgRectangle bb;
    SGGeod p;

    // bail out immediately if polygon is empty
    if ( subject.Contours() == 0 )
        return;

    bb = subject.GetBoundingBox();

    SG_LOG( SG_GENERAL, SG_DEBUG, "  min = " << bb.getMin() << " max = " << bb.getMax() );

    // find buckets for min, and max points of convex hull.
    // note to self: self, you should think about checking for
    // polygons that span the date line
    SGBucket b_min( bb.getMin() );
    SGBucket b_max( bb.getMax() );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  Bucket min = " << b_min );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  Bucket max = " << b_max );

    if ( b_min == b_max ) {
        // shape entirely contained in a single bucket, write and bail
        Clip( subject, type, b_min );
        return;
    }

    SGBucket b_cur;
    int dx, dy;

    sgBucketDiff(b_min, b_max, &dx, &dy);
    SG_LOG( SG_GENERAL, SG_DEBUG, "  polygon spans tile boundaries" );
    SG_LOG( SG_GENERAL, SG_DEBUG, "  dx = " << dx << "  dy = " << dy );

    if ( (dx > 2880) || (dy > 1440) )
        throw sg_exception("something is really wrong in split_polygon()!!!!");

    if ( dy <= 1 ) {
        // we are down to at most two rows, write each column and then bail
        double min_center_lat = b_min.get_center_lat();
        double min_center_lon = b_min.get_center_lon();
        for ( int j = 0; j <= dy; ++j ) {
            for ( int i = 0; i <= dx; ++i ) {
                b_cur = sgBucketOffset(min_center_lon, min_center_lat, i, j);
                Clip( subject, type, b_cur );
            }
        }
        return;
    }

    // we have two or more rows left, split in half (along a
    // horizontal dividing line) and recurse with each half

    // find mid point (integer math)
    int mid = (dy + 1) / 2 - 1;

    // determine horizontal clip line
    SGBucket b_clip = sgBucketOffset( bb.getMin().getLongitudeDeg(), bb.getMin().getLatitudeDeg(), 0, mid);
    double clip_line = b_clip.get_center_lat();
    if ( (clip_line >= -90.0 + SG_HALF_BUCKET_SPAN)
         && (clip_line < 90.0 - SG_HALF_BUCKET_SPAN) )
        clip_line += SG_HALF_BUCKET_SPAN;
    else if ( clip_line < -89.0 )
        clip_line = -89.0;
    else if ( clip_line >= 89.0 )
        clip_line = 90.0;
    else {
        SG_LOG( SG_GENERAL, SG_ALERT, "Out of range latitude in clip_and_write_poly() = " << clip_line );
    }

    {
        //
        // Crop bottom area (hopefully by putting this in it's own
        // scope we can shorten the life of some really large data
        // structures to reduce memory use)
        //

        SG_LOG( SG_GENERAL, SG_DEBUG, "Generating bottom half (" << bb.getMin().getLatitudeDeg() << "-" << clip_line << ")" );

        tgPolygon bottom, bottom_clip;

        bottom.AddNode( 0, SGGeod::fromDeg(-180.0, bb.getMin().getLatitudeDeg()) );
        bottom.AddNode( 0, SGGeod::fromDeg( 180.0, bb.getMin().getLatitudeDeg()) );
        bottom.AddNode( 0, SGGeod::fromDeg( 180.0, clip_line) );
        bottom.AddNode( 0, SGGeod::fromDeg(-180.0, clip_line) );

        bottom_clip = tgPolygon::Intersect( subject, bottom );

        // the texparam should be constant over each clipped poly.
        // when they are reassembled, we want the texture map to
        // be seamless
        Add( bottom_clip, type );
    }

    {
        //
        // Crop top area (hopefully by putting this in it's own scope
        // we can shorten the life of some really large data
        // structures to reduce memory use)
        //

        SG_LOG( SG_GENERAL, SG_DEBUG, "Generating top half (" << clip_line << "-" << bb.getMax().getLatitudeDeg() << ")" );

        tgPolygon top, top_clip;

        top.AddNode( 0, SGGeod::fromDeg(-180.0, clip_line) );
        top.AddNode( 0, SGGeod::fromDeg( 180.0, clip_line) );
        top.AddNode( 0, SGGeod::fromDeg( 180.0, bb.getMax().getLatitudeDeg()) );
        top.AddNode( 0, SGGeod::fromDeg(-180.0, bb.getMax().getLatitudeDeg()) );

        top_clip = tgPolygon::Intersect( subject, top );

        if ( top_clip.TotalNodes() == subject.TotalNodes() ) {
            SG_LOG( SG_GENERAL, SG_DEBUG, "Generating top half - total nodes is the same after clip" << subject.TotalNodes() );
            exit(0);
        }

        Add( top_clip, type );
    }
}

long int tgChopper::GenerateIndex( std::string path )
{
    std::string index_file = path + "/chop.idx";
    long int index = 0;

    //Open or create the named mutex
    boost::interprocess::named_mutex mutex(boost::interprocess::open_or_create, "tgChopper_index2");
    {
//        SG_LOG(SG_GENERAL, SG_ALERT, "getting lock");
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);
//        SG_LOG(SG_GENERAL, SG_ALERT, " - got it");

        /* first try to read the file */
        FILE *fp = fopen( index_file.c_str(), "r+" );
        if ( fp == NULL ) {
            /* doesn't exist - create it */
            fp = fopen( index_file.c_str(), "w" );
            if ( fp == NULL ) {
                SG_LOG(SG_GENERAL, SG_ALERT, "Error cannot open Index file " << index_file << " for writing");
                boost::interprocess::named_mutex::remove("tgChopper_index2");
                exit( 0 );
            }
        } else {
            fread( (void*)&index, sizeof(long int), 1, fp );
//            SG_LOG(SG_GENERAL, SG_ALERT, " SUCCESS READING INDEX FILE - READ INDEX " << index );
        }

        index++;

        rewind( fp );
        fwrite( (void*)&index, sizeof(long int), 1, fp );
        fclose( fp );
    }

    boost::interprocess::named_mutex::remove("tgChopper_index2");

    return index;
}

void tgChopper::Save( void )
{
    // traverse the bucket list
    bucket_polys_map_interator it;
    char tile_name[16];
    char poly_ext[16];

    for (it=bp_map.begin(); it != bp_map.end(); it++) {
        SGBucket b( (*it).first );
        tgpolygon_list const& polys = (*it).second;

        std::string path = root_path + "/" + b.gen_base_path();
        sprintf( tile_name, "%ld", b.gen_index() );

        std::string polyfile = path + "/" + tile_name;

        SGPath sgp( polyfile );
        sgp.create_dir( 0755 );

        long int poly_index = GenerateIndex( path );

        sprintf( poly_ext, "%ld", poly_index );
        polyfile = polyfile + "." + poly_ext;

        gzFile fp;
        if ( (fp = gzopen( polyfile.c_str(), "wb9" )) == NULL ) {
            SG_LOG( SG_GENERAL, SG_INFO, "ERROR: opening " << polyfile.c_str() << " for writing!" );
            return;
        }

        /* Write polys to the file */
        sgWriteUInt( fp, polys.size() );
        for ( unsigned int i=0; i<polys.size(); i++ ) {
            polys[i].SaveToGzFile( fp );
        }

        gzclose( fp );
    }
}