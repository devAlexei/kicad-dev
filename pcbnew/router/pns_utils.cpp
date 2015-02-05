/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2014 CERN
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pns_utils.h"
#include "pns_line.h"
#include "pns_via.h"
#include "pns_router.h"

#include <geometry/shape_segment.h>

const SHAPE_LINE_CHAIN OctagonalHull( const VECTOR2I& aP0, const VECTOR2I& aSize,
                                      int aClearance, int aChamfer )
{
    SHAPE_LINE_CHAIN s;

    s.SetClosed( true );

    s.Append( aP0.x - aClearance, aP0.y - aClearance + aChamfer );
    s.Append( aP0.x - aClearance + aChamfer, aP0.y - aClearance );
    s.Append( aP0.x + aSize.x + aClearance - aChamfer, aP0.y - aClearance );
    s.Append( aP0.x + aSize.x + aClearance, aP0.y - aClearance + aChamfer );
    s.Append( aP0.x + aSize.x + aClearance, aP0.y + aSize.y + aClearance - aChamfer );
    s.Append( aP0.x + aSize.x + aClearance - aChamfer, aP0.y + aSize.y + aClearance );
    s.Append( aP0.x - aClearance + aChamfer, aP0.y + aSize.y + aClearance );
    s.Append( aP0.x - aClearance, aP0.y + aSize.y + aClearance - aChamfer );

    return s;
}


const SHAPE_LINE_CHAIN SegmentHull ( const SHAPE_SEGMENT& aSeg, int aClearance,
                                     int aWalkaroundThickness )
{
    int d = aSeg.GetWidth() / 2 + aClearance + aWalkaroundThickness / 2 + HULL_MARGIN;
    int x = (int)( 2.0 / ( 1.0 + M_SQRT2 ) * d );

    const VECTOR2I a = aSeg.GetSeg().A;
    const VECTOR2I b = aSeg.GetSeg().B;

    VECTOR2I dir = b - a;
    VECTOR2I p0 = dir.Perpendicular().Resize( d );
    VECTOR2I ds = dir.Perpendicular().Resize( x / 2 );
    VECTOR2I pd = dir.Resize( x / 2 );
    VECTOR2I dp = dir.Resize( d );

    SHAPE_LINE_CHAIN s;

    s.SetClosed( true );

    s.Append( b + p0 + pd );
    s.Append( b + dp + ds );
    s.Append( b + dp - ds );
    s.Append( b - p0 + pd );
    s.Append( a - p0 - pd );
    s.Append( a - dp - ds );
    s.Append( a - dp + ds );
    s.Append( a + p0 - pd );

    // make sure the hull outline is always clockwise
    if( s.CSegment( 0 ).Side( a ) < 0 )
        return s.Reverse();
    else
        return s;
}


SHAPE_RECT ApproximateSegmentAsRect( const SHAPE_SEGMENT& aSeg )
{
    SHAPE_RECT r;

    VECTOR2I delta( aSeg.GetWidth() / 2, aSeg.GetWidth() / 2 );
    VECTOR2I p0( aSeg.GetSeg().A - delta );
    VECTOR2I p1( aSeg.GetSeg().B + delta );

    return SHAPE_RECT( std::min( p0.x, p1.x ), std::min( p0.y, p1.y ),
                       std::abs( p1.x - p0.x ), std::abs( p1.y - p0.y ) );
}

void DrawDebugPoint ( VECTOR2I p, int color )
{
    SHAPE_LINE_CHAIN l;

    l.Append ( p - VECTOR2I(-50000, -50000) );
    l.Append ( p + VECTOR2I(-50000, -50000) );
    
    //printf("router @ %p\n", PNS_ROUTER::GetInstance());
    PNS_ROUTER::GetInstance()->DisplayDebugLine ( l, color, 10000 );

    l.Clear();
    l.Append ( p - VECTOR2I(50000, -50000) );
    l.Append ( p + VECTOR2I(50000, -50000) );

    PNS_ROUTER::GetInstance()->DisplayDebugLine ( l, color, 10000 );
}

void DrawDebugBox ( BOX2I b, int color )
{
    SHAPE_LINE_CHAIN l;

    VECTOR2I o = b.GetOrigin();
    VECTOR2I s = b.GetSize();

    l.Append ( o );
    l.Append ( o.x + s.x, o.y );
    l.Append ( o.x + s.x, o.y + s.y );
    l.Append ( o.x, o.y + s.y );
    l.Append ( o );
    
    //printf("router @ %p\n", PNS_ROUTER::GetInstance());
    PNS_ROUTER::GetInstance()->DisplayDebugLine ( l, color, 10000 );
}

void DrawDebugSeg ( SEG s, int color )
{
    SHAPE_LINE_CHAIN l;

    l.Append ( s.A );
    l.Append ( s.B );
    
    PNS_ROUTER::GetInstance()->DisplayDebugLine ( l, color, 10000 );
}



OPT_BOX2I ChangedArea ( const PNS_ITEM *aItemA, const PNS_ITEM *aItemB )
{
    if ( aItemA->OfKind ( PNS_ITEM::VIA ) && aItemB->OfKind ( PNS_ITEM::VIA ) )
    {
        const PNS_VIA *va = static_cast <const PNS_VIA *> (aItemA);
        const PNS_VIA *vb = static_cast <const PNS_VIA *> (aItemB);
 
        return va->ChangedArea (vb);
    } else if ( aItemA->OfKind ( PNS_ITEM::LINE ) && aItemB->OfKind ( PNS_ITEM::LINE ) )
    {
        const PNS_LINE *la = static_cast <const PNS_LINE *> (aItemA);
        const PNS_LINE *lb = static_cast <const PNS_LINE *> (aItemB);

        return la->ChangedArea (lb);
    }
    return OPT_BOX2I();
}