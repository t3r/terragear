#include <simgear/debug/logstream.hxx>

#include "tg_polygon_set.hxx"

#include <simgear/misc/sg_path.hxx> // for file i/o

// we are loading polygonal data from untrusted sources
// high probability this will crash CGAL if we just load 
// the points.  previous terragear would attempt to clean
// the input using duplicate point detection, degenerate
// edge detection, etc.
// 
// Instead, we'll generate an arrangement from each ring
// the first ring is considered the boundary, and all of 
// the rest are considered holes.
// this should never fail.                   _
//           _______________________________|_|
//          /                            ___|
//         /    _______                 / 
//        /    |       |               /
//        |    |     __|_             /
//        |    |____|__| |     ______/____
//        |         |____|     |____/     |
//        |                   /|          |
//        \                  / |__________|
//         \________________/
//
// NOTE: using this method, the above shapefile will result in a
// single polygon with holes.
// 1) the outer boundary is not simple - it self intersects in the 
//    top right corner.  We generate the outer boundary as the 
//    union of all faces generated by the first ring
// 2) the three remaining rings are unioned together as holes
//    a boolean difference is performed to make them holes.
//
// NOTE: the first two self intersecting holes become a single hole.
//       the third ring decreases the boundary of the polygon
//
// the final result is two polygons_with_holes.
// the frst is a poly with a single hole.
// the second is the degenerate piece in the top right.
tgPolygonSet::tgPolygonSet( OGRFeature* poFeature, OGRPolygon* poGeometry, const std::string& material ) : id(tgPolygonSet::cur_id++)
{
    // generate texture info from feature
    getFeatureFields( poFeature );

    // overwrite material, as it was given
    ti.material = material; 

    // create PolygonSet from the outer ring
    OGRLinearRing const *ring = poGeometry->getExteriorRing();
    ps = ogrRingToPolygonSet( ring );    

    // then a PolygonSet from each interior ring
    for ( int i = 0 ; i < poGeometry->getNumInteriorRings(); i++ ) {
        ring = poGeometry->getInteriorRing( i );
	cgalPoly_PolygonSet hole = ogrRingToPolygonSet( ring );

        ps.difference( hole );
    }
}

tgPolygonSet::tgPolygonSet( OGRFeature* poFeature, OGRPolygon* poGeometry ) : id(tgPolygonSet::cur_id++)
{
    // generate texture info from feature
    getFeatureFields( poFeature );

    // create PolygonSet from the outer ring
    OGRLinearRing const *ring = poGeometry->getExteriorRing();
    ps = ogrRingToPolygonSet( ring );    

    // then a PolygonSet from each interior ring
    for ( int i = 0 ; i < poGeometry->getNumInteriorRings(); i++ ) {
        ring = poGeometry->getInteriorRing( i );
	cgalPoly_PolygonSet hole = ogrRingToPolygonSet( ring );

        ps.difference( hole );
    }
}

cgalPoly_PolygonSet tgPolygonSet::ogrRingToPolygonSet( OGRLinearRing const *ring )
{
    cgalPoly_Arrangement  		arr;
    std::vector<cgalPoly_Segment> 	segs;
    cgalPoly_PolygonSet                 faces;

    for (int i = 0; i < ring->getNumPoints(); i++) {
        cgalPoly_Point src(ring->getX(i), ring->getY(i));
	int trgIdx ;

        if ( i < ring->getNumPoints()-1 ) {
            // target is the next point
            trgIdx = i+1;
	} else {
            // target is the first point
            trgIdx = 0;
	}
	cgalPoly_Point trg = cgalPoly_Point(ring->getX(trgIdx), ring->getY(trgIdx));

        if ( src != trg ) {
            segs.push_back( cgalPoly_Segment( src, trg ) );
        } else {
            std::cout << " seg src == seg trg " << std::endl;
        }
    }

    insert( arr, segs.begin(), segs.end() );

    // DEBUG DEBUG DEBUG
    // dump the arrangement
    char layer_id[128];
    sprintf( layer_id, "%s_%ld", ti.material.c_str(), id );

    // Open datasource and layer
    GDALDataset* poDS = openDatasource( "./arr_dbg" );

    if ( poDS ) {
        OGRLayer* poLayer = openLayer( poDS, wkbLineString, layer_id );
        if ( poLayer ) {
            toShapefile( poLayer, arr );
        }

        // close datasource
        GDALClose( poDS );
    }

    // return the union of all bounded faces
    cgalPoly_FaceConstIterator fit;
    for( fit = arr.faces_begin(); fit != arr.faces_end(); fit++ ) {
        cgalPoly_Arrangement::Face face = (*fit);
        if( face.has_outer_ccb() ) {
            // generate Polygon from face, and join wuth polygon set
            cgalPoly_CcbHeConstCirculator ccb = face.outer_ccb();
            cgalPoly_CcbHeConstCirculator cur = ccb;
            cgalPoly_HeConstHandle        he;
            std::vector<cgalPoly_Point>   nodes;

            do
            {
                he = cur;

                // ignore inner antenna
                if ( he->face() != he->twin()->face() ) {                    
		    nodes.push_back( he->source()->point() );
                }
                
                ++cur;
            } while (cur != ccb);

	    // check the orientation - outer boundaries should be CCW
            cgalPoly_Polygon poly( nodes.begin(), nodes.end() );
            faces.join( poly );
        }
    }

    return faces;
}


GDALDataset* tgPolygonSet::openDatasource( const char* datasource_name ) const
{
    GDALDataset*    poDS = NULL;
    GDALDriver*     poDriver = NULL;
    const char*     format_name = "ESRI Shapefile";
    
    SG_LOG( SG_GENERAL, SG_INFO, "Open Datasource: " << datasource_name );
    
    GDALAllRegister();
    
    SGPath sgp( datasource_name );
    sgp.create_dir( 0755 );
    
    poDriver = GetGDALDriverManager()->GetDriverByName( format_name );
    if ( poDriver ) {    
        poDS = poDriver->Create( datasource_name, 0, 0, 0, GDT_Unknown, NULL );
    }
    
    return poDS;
}

OGRLayer* tgPolygonSet::openLayer( GDALDataset* poDS, OGRwkbGeometryType lt, const char* layer_name ) const
{
    OGRLayer*           poLayer = NULL;
    
    poLayer = poDS->GetLayerByName( layer_name );    
    if ( !poLayer ) {
        SG_LOG(SG_GENERAL, SG_ALERT, "tgPolygonSet::toShapefile: layer " << layer_name << " doesn't exist - create" );

        OGRSpatialReference srs;
        srs.SetWellKnownGeogCS("WGS84");
        
        poLayer = poDS->CreateLayer( layer_name, &srs, lt, NULL );

        OGRFieldDefn descriptionField( "tg_desc", OFTString );
        descriptionField.SetWidth( 128 );
        if( poLayer->CreateField( &descriptionField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_desc' failed" );
        }
        
        OGRFieldDefn idField( "tg_id", OFTInteger );
        if( poLayer->CreateField( &idField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_id' failed" );
        }
        
        OGRFieldDefn flagsField( "tg_flags", OFTInteger );
        if( poLayer->CreateField( &flagsField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'flags' failed" );
        }
        
        OGRFieldDefn materialField( "tg_mat", OFTString );
        materialField.SetWidth( 32 );
        if( poLayer->CreateField( &materialField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_material' failed" );
        }
        
        OGRFieldDefn texMethodField( "tg_texmeth", OFTInteger );
        if( poLayer->CreateField( &texMethodField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tex_method' failed" );
        }
        
        OGRFieldDefn texRefLonField( "tg_reflon", OFTReal );
        if( poLayer->CreateField( &texRefLonField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_ref_lon' failed" );
        }
        
        OGRFieldDefn texRefLatField( "tg_reflat", OFTReal );
        if( poLayer->CreateField( &texRefLatField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_ref_lat' failed" );
        }
        
        OGRFieldDefn texHeadingField( "tg_heading", OFTReal );
        if( poLayer->CreateField( &texHeadingField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_heading' failed" );
        }
        
        OGRFieldDefn texWidthField( "tg_width", OFTReal );
        if( poLayer->CreateField( &texWidthField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_width' failed" );
        }
        
        OGRFieldDefn texLengthField( "tg_length", OFTReal );
        if( poLayer->CreateField( &texLengthField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_length' failed" );
        }
        
        OGRFieldDefn texMinUField( "tg_minu", OFTReal );
        if( poLayer->CreateField( &texMinUField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_minu' failed" );
        }
        
        OGRFieldDefn texMinVField( "tg_minv", OFTReal );
        if( poLayer->CreateField( &texMinVField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_minv' failed" );
        }
        
        OGRFieldDefn texMaxUField( "tg_maxu", OFTReal );
        if( poLayer->CreateField( &texMaxUField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_maxu' failed" );
        }
        
        OGRFieldDefn texMaxVField( "tg_maxv", OFTReal );
        if( poLayer->CreateField( &texMaxVField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_maxv' failed" );
        }
        
        OGRFieldDefn texMinClipUField( "tg_mincu", OFTReal );
        if( poLayer->CreateField( &texMinClipUField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_min_clipu' failed" );
        }
        
        OGRFieldDefn texMinClipVField( "tg_mincv", OFTReal );
        if( poLayer->CreateField( &texMinClipVField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_min_clipv' failed" );
        }
        
        OGRFieldDefn texMaxClipUField( "tg_maxcu", OFTReal );
        if( poLayer->CreateField( &texMaxClipUField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_max_clipu' failed" );
        }
        
        OGRFieldDefn texMaxClipVField( "tg_maxcv", OFTReal );
        if( poLayer->CreateField( &texMaxClipVField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_max_clipv' failed" );
        }
        
        OGRFieldDefn texCenterLatField( "tg_clat", OFTReal );
        if( poLayer->CreateField( &texCenterLatField ) != OGRERR_NONE ) {
            SG_LOG( SG_GENERAL, SG_ALERT, "Creation of field 'tg_tp_center_lat' failed" );
        }
        
    } else {
        SG_LOG(SG_GENERAL, SG_ALERT, "tgPolygonSet::toShapefile: layer " << layer_name << " already exists - open" );        
    }
   
    return poLayer;
}

void tgPolygonSet::toShapefile( OGRLayer* layer, const char* description ) const
{
    
}

void tgPolygonSet::toShapefile( OGRLayer* poLayer, const cgalPoly_PolygonWithHoles& pwh ) const
{
    OGRPolygon    polygon;
    OGRPoint      point;
    OGRLinearRing ring;
    
    // in CGAL, the outer boundary is counter clockwise - in GDAL, it's expected to be clockwise
    cgalPoly_Polygon poly;
    cgalPoly_Polygon::Vertex_iterator it;

    poly = pwh.outer_boundary();
    //poly.reverse_orientation();
    for ( it = poly.vertices_begin(); it != poly.vertices_end(); it++ ) {
        point.setX( CGAL::to_double( (*it).x() ) );
        point.setY( CGAL::to_double( (*it).y() ) );
        point.setZ( 0.0 );
                
        ring.addPoint(&point);
    }
    ring.closeRings();
    polygon.addRing(&ring);

    // then write each hole
    cgalPoly_PolygonWithHoles::Hole_const_iterator hit;
    for (hit = pwh.holes_begin(); hit != pwh.holes_end(); ++hit) {
        OGRLinearRing hole;
        poly = (*hit);

        //poly.reverse_orientation();
        for ( it = poly.vertices_begin(); it != poly.vertices_end(); it++ ) {
            point.setX( CGAL::to_double( (*it).x() ) );
            point.setY( CGAL::to_double( (*it).y() ) );
            point.setZ( 0.0 );
                    
            hole.addPoint(&point);            
        }
        hole.closeRings();
        polygon.addRing(&hole);
    }

    OGRFeature* poFeature = OGRFeature::CreateFeature( poLayer->GetLayerDefn() );
    poFeature->SetGeometry(&polygon);    
    setFeatureFields( poFeature );
    
    if( poLayer->CreateFeature( poFeature ) != OGRERR_NONE )
    {
        SG_LOG(SG_GENERAL, SG_ALERT, "Failed to create feature in shapefile");
    }
    OGRFeature::DestroyFeature(poFeature);    
}

void tgPolygonSet::toShapefile( OGRLayer* poLayer, const cgalPoly_Arrangement& arr ) const
{    
    cgalPoly_EdgeConstIterator eit;
    
    for ( eit = arr.edges_begin(); eit != arr.edges_end(); ++eit ) {        
        cgalPoly_Segment seg = eit->curve();

        OGRLinearRing ring;
        OGRPoint      point;

	point.setX( CGAL::to_double( seg.source().x() ) );
	point.setY( CGAL::to_double( seg.source().y() ) );
	point.setZ( 0 );
        ring.addPoint(&point);

	point.setX( CGAL::to_double( seg.target().x() ) );
	point.setY( CGAL::to_double( seg.target().y() ) );
	point.setZ( 0 );
        ring.addPoint(&point);

        OGRFeature* poFeature = OGRFeature::CreateFeature( poLayer->GetLayerDefn() );
        poFeature->SetGeometry(&ring);    
    
        if( poLayer->CreateFeature( poFeature ) != OGRERR_NONE )
        {
            SG_LOG(SG_GENERAL, SG_ALERT, "Failed to create feature in shapefile");
        }
        OGRFeature::DestroyFeature(poFeature);    
    }    
}

int tgPolygonSet::getFieldAsInteger( OGRFeature* poFeature, const char* field, int defValue )
{
    int fieldIdx = poFeature->GetFieldIndex( field );
    int value = defValue;
    
    if ( fieldIdx >= 0 ) {
        value = poFeature->GetFieldAsInteger(fieldIdx);
    }
    
    return value;
}

double tgPolygonSet::getFieldAsDouble( OGRFeature* poFeature, const char* field, double defValue )
{
    int fieldIdx = poFeature->GetFieldIndex( field );
    double value = defValue;
    
    if ( fieldIdx >= 0 ) {
        value = poFeature->GetFieldAsDouble(fieldIdx);
    }
    
    return value;
}

const char* tgPolygonSet::getFieldAsString( OGRFeature* poFeature, const char* field, const char* defValue )
{
    int fieldIdx = poFeature->GetFieldIndex( field );
    const char* value = defValue;
    
    if ( fieldIdx >= 0 ) {
        value = poFeature->GetFieldAsString(fieldIdx);
    }
    
    return value;
}

void tgPolygonSet::getFeatureFields( OGRFeature* poFeature )
{
    id          = getFieldAsInteger( poFeature, "tg_id", id );
    flags       = getFieldAsInteger( poFeature, "tg_flags", 0 );

    ti.material = getFieldAsString( poFeature, "tg_mat", "default" );    
    ti.method   = (tgTexInfo::method_e)getFieldAsInteger( poFeature, "tg_texmeth", tgTexInfo::TEX_BY_GEODE );
    if ( ti.method == tgTexInfo::TEX_BY_GEODE ) {
        ti.center_lat = getFieldAsDouble( poFeature, "tg_clat", 0.0 );
    } else {
        ti.ref = cgalPoly_Point( getFieldAsDouble( poFeature, "tg_reflon", 0.0 ), 
                                 getFieldAsDouble( poFeature, "tg_reflat", 0.0 ) );
        
        ti.heading   = getFieldAsDouble( poFeature, "tg_heading", 0.0 );
        ti.width     = getFieldAsDouble( poFeature, "tg_width", 0.0 );
        ti.length    = getFieldAsDouble( poFeature, "tg_length", 0.0 );
        ti.minu      = getFieldAsDouble( poFeature, "tg_minu", 0.0 );
        ti.minv      = getFieldAsDouble( poFeature, "tg_minv", 0.0 );
        ti.maxu      = getFieldAsDouble( poFeature, "tg_maxu", 0.0 );
        ti.maxv      = getFieldAsDouble( poFeature, "tg_maxv", 0.0 );
        ti.min_clipu = getFieldAsDouble( poFeature, "tg_mincu", 0.0 );
        ti.min_clipv = getFieldAsDouble( poFeature, "tg_mincv", 0.0 );
        ti.max_clipu = getFieldAsDouble( poFeature, "tg_maxcu", 0.0 );
        ti.max_clipv = getFieldAsDouble( poFeature, "tg_maxcv", 0.0 );
    }    
}

void tgPolygonSet::setFeatureFields( OGRFeature* poFeature ) const
{
    poFeature->SetField("tg_id",        (int)id );
    poFeature->SetField("tg_flags",     (int)flags );
    
    poFeature->SetField("tg_mat",       ti.material.c_str() );
    poFeature->SetField("tg_texmeth",   (int)ti.method );
    
    poFeature->SetField("tg_reflon",    CGAL::to_double(ti.ref.x()) );
    poFeature->SetField("tg_reflat",    CGAL::to_double(ti.ref.y()) );
    poFeature->SetField("tg_heading",   ti.heading );
    poFeature->SetField("tg_width",     ti.width );
    poFeature->SetField("tg_length",    ti.length );
    poFeature->SetField("tg_minu",      ti.minu );
    poFeature->SetField("tg_minv",      ti.minv );
    poFeature->SetField("tg_maxu",      ti.maxu );
    poFeature->SetField("tg_maxv",      ti.maxv );
    poFeature->SetField("tg_mincu",     ti.min_clipu );
    poFeature->SetField("tg_mincv",     ti.min_clipv );
    poFeature->SetField("tg_maxcu",     ti.max_clipu );
    poFeature->SetField("tg_maxcv",     ti.max_clipv );
}

void tgPolygonSet::toShapefile( const char* datasource, const char* layer ) const
{
    // Open datasource and layer
    GDALDataset* poDS = openDatasource( datasource );

    if ( poDS ) {
        OGRLayer* poLayer = openLayer( poDS, wkbPolygon25D, layer );
        if ( poLayer ) {
            toShapefile( poLayer, ps );
        }
    }
    
    // close datasource
    GDALClose( poDS );
}

void tgPolygonSet::toShapefile( OGRLayer* poLayer, const cgalPoly_PolygonSet& polySet ) const
{
    std::list<cgalPoly_PolygonWithHoles>                 pwh_list;
    std::list<cgalPoly_PolygonWithHoles>::const_iterator it;

    polySet.polygons_with_holes( std::back_inserter(pwh_list) );
    SG_LOG(SG_GENERAL, SG_ALERT, "tgPolygonSet::toShapefile: got " << pwh_list.size() << " polys with holes ");
    
    // save each poly with holes to the layer
    for (it = pwh_list.begin(); it != pwh_list.end(); ++it) {
        cgalPoly_PolygonWithHoles pwh = (*it);

        toShapefile( poLayer, pwh );
    }
}

#if 0 // native from GDAL    
tgPolygonSet tgPolygonSet::fromGDAL( OGRPolygon* poGeometry )
{
    cgalPoly_Arrangement  arr;
    cgalPoly_PolygonSet   boundaries;
    cgalPoly_PolygonSet   holes;
    
    // for each boundary contour, we add it to its own arrangement
    // then read the resulting faces as a list of polygons with holes
    // note that a self intersecting face ( like a donut ) will generate
    // more than one face.  We need to determine for each face wether it is a 
    // hole or not.
    // ___________
    // | ______  |
    // | \    /  |
    // |  \  /   |
    // |___\/____|
    //
    // Example of a single self intersecting contour that should be represented by a polygon 
    for (unsigned int i=0; i<subject.Contours(); i++ ) {
        char layer[128];

        //sprintf( layer, "%04u_original_contour_%d", subject.GetId(), i );
        //tgShapefile::FromContour( subject.GetContour(i), false, true, "./clip_dbg", layer, "cont" );
    
        arr.Clear();
        arr.Add( subject.GetContour(i), layer );
    
        // retreive the new Contour(s) from traversing the outermost face first
        // any holes in this face are individual polygons
        // any holes in those faces are holes, etc...
        
        // dump the arrangement to see what we have.
        //sprintf( layer, "%04u_Arrangement_contour_%d", subject.GetId(), i );
        //arr.ToShapefiles( "./clip_dbg", layer );

        // Combine boundaries and holes into their sets
        Polygon_set face = arr.ToPolygonSet( i );
        //sprintf( layer, "%04u_face_contour_%d", subject.GetId(), i );
        //ToShapefile( face, layer );
        
        if ( subject.GetContour(i).GetHole() ) {
            //SG_LOG(SG_GENERAL, SG_ALERT, "ToCgalPolyWithHoles : Join with holes"  );
            
            //SG_LOG(SG_GENERAL, SG_ALERT, "ToCgalPolyWithHoles : before - face_valid " << face.is_valid() << " holes_valid " << holes.is_valid()  );
            holes.join( face );
            //SG_LOG(SG_GENERAL, SG_ALERT, "ToCgalPolyWithHoles : after - face_valid " << face.is_valid() << " holes_valid " << holes.is_valid()  );
        } else {
            //SG_LOG(SG_GENERAL, SG_ALERT, "ToCgalPolyWithHoles : Join with boundaries"  );
            
            //SG_LOG(SG_GENERAL, SG_ALERT, "ToCgalPolyWithHoles : before - face_valid " << face.is_valid() << " boundaries_valid " << boundaries.is_valid()  );
            boundaries.join( face );            
            //SG_LOG(SG_GENERAL, SG_ALERT, "ToCgalPolyWithHoles : after - face_valid " << face.is_valid() << " boundaries_valid " << boundaries.is_valid()  );
        }
        //SG_LOG(SG_GENERAL, SG_ALERT, "ToCgalPolyWithHoles : Join complete"  );
    }

    // now, generate the result
    boundaries.difference( holes );
    
    // dump to shapefile
    if ( boundaries.is_valid() ) {
        return tgPolygonSet( boundaries );
    } else {
        return tgPolygonSet();
    }
}
#endif


// this needs some more thought - static constructor?  do projection?
// texture info? flags?, identifier?

// We want feature and geometry - geometry should be projected already.
// constructor - return new object. - no longer a static function
    
typedef CGAL::Bbox_2    BBox;

#if 0
void tgAccumulator::Diff_cgal( tgPolygon& subject )
{   
    // static int savepoly = 0;
    // char filename[32];
    
    Polygon_set  cgalSubject;
    CGAL::Bbox_2 cgalBbox;
    
    Polygon_set diff = accum_cgal;

    if ( ToCgalPolyWithHoles( subject, cgalSubject, cgalBbox ) ) {
        if ( !accumEmpty ) {
            cgalSubject.difference( diff );
        }

        ToTgPolygon( cgalSubject, subject );
    }
}

void tgAccumulator::Add_cgal( const tgPolygon& subject )
{
    // just add the converted cgalPoly_PolygonWithHoles to the Polygon set
    Polygon_set  cgalSubject;
    CGAL::Bbox_2 cgalBbox;
    
    if ( ToCgalPolyWithHoles( subject, cgalSubject, cgalBbox ) ) {
        accum_cgal.join(cgalSubject);
        accumEmpty = false;
    }
}

// rewrite to use bounding boxes, and lists of polygons with holes 
// need a few functions:
// 1) generate a Polygon_set from the Polygons_with_holes in the list that intersect subject bounding box
// 2) Add to the Polygons_with_holes list with a Polygon set ( and the bounding boxes )
    
Polygon_set tgAccumulator::GetAccumPolygonSet( const CGAL::Bbox_2& bbox ) 
{
    std::list<tgAccumEntry>::const_iterator it;
    std::list<Polygon_with_holes> accum;
    Polygon_set ps;
    
    // traverse all of the Polygon_with_holes and accumulate their union
    for ( it=accum_cgal_list.begin(); it!=accum_cgal_list.end(); it++ ) {
        if ( CGAL::do_overlap( bbox, (*it).bbox ) ) {
            accum.push_back( (*it).pwh );
        }
    }
    
    ps.join( accum.begin(), accum.end() );
    
    return ps;
}

void tgAccumulator::AddAccumPolygonSet( const Polygon_set& ps )
{
    std::list<Polygon_with_holes> pwh_list;
    std::list<Polygon_with_holes>::const_iterator it;
    CGAL::Bbox_2 bbox;
    
    ps.polygons_with_holes( std::back_inserter(pwh_list) );
    for (it = pwh_list.begin(); it != pwh_list.end(); ++it) {
        tgAccumEntry entry;
        entry.pwh  = (*it);
        entry.bbox =  entry.pwh.outer_boundary().bbox();

        accum_cgal_list.push_back( entry );
    }    
}

#define DEBUG_DIFF_AND_ADD 1
void tgAccumulator::Diff_and_Add_cgal( tgPolygon& subject )
{
    Polygon_set     cgSubject;
    CGAL::Bbox_2    cgBoundingBox;

#if DEBUG_DIFF_AND_ADD    
    char            layer[128];
#endif
    
    if ( ToCgalPolyWithHoles( subject, cgSubject, cgBoundingBox ) ) {
        Polygon_set add  = cgSubject;
        Polygon_set diff = GetAccumPolygonSet( cgBoundingBox );

#if DEBUG_DIFF_AND_ADD    
        sprintf( layer, "clip_%03d_pre_subject", subject.GetId() );
        ToShapefile( add, layer );
        
        tgContour bb;
        bb.AddPoint( cgalPoly_Point( cgBoundingBox.xmin(), cgBoundingBox.ymin() ) );
        bb.AddPoint( cgalPoly_Point( cgBoundingBox.xmin(), cgBoundingBox.ymax() ) );
        bb.AddPoint( cgalPoly_Point( cgBoundingBox.xmax(), cgBoundingBox.ymax() ) );
        bb.AddPoint( cgalPoly_Point( cgBoundingBox.xmax(), cgBoundingBox.ymin() ) );
        
        sprintf( layer, "clip_%03d_bbox", subject.GetId() );
        tgShapefile::FromContour( bb, false, false, "./clip_dbg", layer, "bbox" );
        
        sprintf( layer, "clip_%03d_pre_accum", subject.GetId() );
        ToShapefile( diff, layer );
#endif

        if ( diff.number_of_polygons_with_holes() ) {
            cgSubject.difference( diff );
            
#if DEBUG_DIFF_AND_ADD    
            sprintf( layer, "clip_%03d_post_subject", subject.GetId() );
            ToShapefile( cgSubject, layer );            
#endif

        }

        // add the polygons_with_holes to the accumulator list
        AddAccumPolygonSet( add );
        
        // when we convert back to poly, insert face_location points for each face
        ToTgPolygon( cgSubject, subject );        
    } else {
        tgcontour_list contours;
        contours.clear();
        subject.SetContours( contours );
    }    
}
#endif