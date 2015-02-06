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

#include <boost/foreach.hpp>
#include <boost/optional.hpp>

#include <base_units.h> // God forgive me doing this...
#include <colors.h>

#include "trace.h"

#include "pns_node.h"
#include "pns_itemset.h"
#include "pns_topology.h"
#include "pns_meander_placer.h"
#include "pns_router.h"

using boost::optional;


PNS_MEANDER_PLACER::PNS_MEANDER_PLACER( PNS_ROUTER* aRouter ) :
    PNS_MEANDER_PLACER_BASE ( aRouter )
{
    m_world = NULL;
    m_currentNode = NULL;
    m_originLine = NULL;
}


PNS_MEANDER_PLACER::~PNS_MEANDER_PLACER()
{
 
}

const PNS_LINE PNS_MEANDER_PLACER::Trace() const
{
    return PNS_LINE( *m_originLine, m_finalShape );
}

PNS_NODE* PNS_MEANDER_PLACER::CurrentNode( bool aLoopsRemoved ) const
{
    if(!m_currentNode)
        return m_world;
    return m_currentNode;
}

bool PNS_MEANDER_PLACER::Start( const VECTOR2I& aP, PNS_ITEM* aStartItem )
{
    VECTOR2I p;
    if(!aStartItem || !aStartItem->OfKind ( PNS_ITEM::SEGMENT ))
    {
        Router()->SetFailureReason( _("Please select a track whose length you want to tune.") );
        return false;
    }

    m_initialSegment = static_cast<PNS_SEGMENT *>(aStartItem);

    p = m_initialSegment->Seg().NearestPoint( aP );

    m_originLine = NULL;
    m_currentNode=NULL;
    m_currentStart  = p;

    m_world = Router()->GetWorld()->Branch();
    m_originLine = m_world->AssembleLine( m_initialSegment );

    PNS_TOPOLOGY topo ( m_world );
    m_tunedPath = topo.AssembleTrivialPath ( m_initialSegment );

    m_world->Remove ( m_originLine );
    
    m_currentWidth = m_originLine->Width();
    
    return true;
}


void PNS_MEANDER_PLACER::release()
{
    #if 0
    BOOST_FOREACH(PNS_MEANDER *m, m_meanders)
    {
        delete m;
    }

    m_meanders.clear();
    #endif
}

int PNS_MEANDER_PLACER::origPathLength () const 
{
    int total = 0;
    BOOST_FOREACH ( const PNS_ITEM *item, m_tunedPath.CItems() )
    {
        if ( item->OfKind (PNS_ITEM::LINE))
        {
            total += static_cast <const PNS_LINE *> (item) -> CLine().Length();
        }
    }

    return total;
}

PNS_MEANDER_PLACER::TUNING_STATUS PNS_MEANDER_PLACER::tuneLineLength ( SHAPE_LINE_CHAIN& aTuned, int aElongation )
{
    m_result = PNS_MEANDERED_LINE (this, false);
    m_result.SetWidth ( m_originLine->Width() );
    
    for ( int i = 0; i < aTuned.SegmentCount(); i++)
    {
        m_result.MeanderSegment( aTuned.CSegment(i) );
    }

    int origLength = aTuned.Length();

    int remaining = aElongation;
    bool finished = false;

    BOOST_FOREACH(PNS_MEANDER_SHAPE *m, m_result.Meanders())
    {

        if(m->Type() != MT_CORNER )
        {

            if(remaining >= 0)
                remaining -= m->MaxTunableLength() - m->BaselineLength();

            if(remaining < 0)
            {
                if(!finished)
                    {
                        PNS_MEANDER_TYPE newType;

                        if ( m->Type() == MT_START || m->Type() == MT_SINGLE)
                            newType = MT_SINGLE;
                        else
                            newType = MT_FINISH;

                        m->SetType ( newType );
                        m->Recalculate( );
                        
                        finished = true;
                    } else {
                        m->MakeEmpty();
                    }
            }
        }
    }

    remaining = aElongation;
    int meanderCount = 0;

    BOOST_FOREACH(PNS_MEANDER_SHAPE *m, m_result.Meanders())
    {
        if( m->Type() != MT_CORNER && m->Type() != MT_EMPTY )
        {
            if(remaining >= 0)
            {
                remaining -= m->MaxTunableLength() - m->BaselineLength();
                meanderCount ++;
            }
        }
    }

    int balance = 0;


    if( meanderCount )
        balance = -remaining / meanderCount;
    
    if (balance >= 0)
    {
        BOOST_FOREACH(PNS_MEANDER_SHAPE *m, m_result.Meanders())
        {
            if(m->Type() != MT_CORNER && m->Type() != MT_EMPTY)
            {
     //           int pre = m->MaxTunableLength();
                m->Resize ( std::max( m->Amplitude() - balance / 2, m_settings.m_minAmplitude ) );
            }
        }
        
    }

    aTuned.Clear();

    BOOST_FOREACH(PNS_MEANDER_SHAPE *m, m_result.Meanders())
    {
        if( m->Type() != MT_EMPTY )
            aTuned.Append ( m->CLine(0) );
    }

    int tunedLength = aTuned.Length();

    if (tunedLength < origLength + aElongation)
        return TOO_SHORT;

    return TUNED;
}

bool PNS_MEANDER_PLACER::Move( const VECTOR2I& aP, PNS_ITEM* aEndItem )
{
    if(m_currentNode)
        delete m_currentNode;

    m_currentNode = m_world->Branch();
    
    VECTOR2I n = m_originLine->CLine().NearestPoint(aP);

    SHAPE_LINE_CHAIN l ( m_originLine->CLine() ), l2;
    l.Split ( n );
    l.Split ( m_currentStart );

    int i_start = l.Find ( m_currentStart );
    int i_end = l.Find ( n );

    
    if( i_start > i_end )
    {
        l = l.Reverse();
        i_start = l.Find ( m_currentStart );
        i_end = l.Find ( n );
    }

    
    l2 = l.Slice ( i_start, i_end );

    release();

    m_result.Clear();

    SHAPE_LINE_CHAIN pre = l.Slice (0, i_start );
    SHAPE_LINE_CHAIN post = l.Slice ( i_end, -1 );

    int lineLen = origPathLength();

    m_lastStatus = TUNED;

    if (lineLen > m_settings.m_targetLength)
    {
        m_lastStatus = TOO_LONG;
        m_lastLength = lineLen;
    } else {

        m_lastLength = lineLen - l2.Length();
        m_lastStatus = tuneLineLength(l2, m_settings.m_targetLength - lineLen ); //pre, l2, post);
        m_lastLength += l2.Length(); 

     //   Router()->DisplayDebugLine ( l2, 4, 10000 );
    }

    BOOST_FOREACH ( const PNS_ITEM *item, m_tunedPath.CItems() )
    {
        if ( item->OfKind (PNS_ITEM::LINE))
        {
            Router()->DisplayDebugLine ( static_cast <const PNS_LINE *> (item) -> CLine(), 5, 10000 );
        }
    }


	m_finalShape.Clear( );
    m_finalShape.Append( pre );
    m_finalShape.Append( l2 );
    m_finalShape.Append( post );
    m_finalShape.Simplify();
    return true;
}


bool PNS_MEANDER_PLACER::FixRoute( const VECTOR2I& aP, PNS_ITEM* aEndItem )
{
    m_currentTrace = PNS_LINE(*m_originLine, m_finalShape);  
    m_currentNode->Add ( &m_currentTrace );

    Router()->CommitRouting( m_currentNode );
    return true;
}
        
bool PNS_MEANDER_PLACER::checkFit ( PNS_MEANDER_SHAPE *aShape )
{
    PNS_LINE l ( *m_originLine, aShape->CLine(0) );
    
    if( m_currentNode->CheckColliding( &l ) )
        return false;

    int w = aShape->Width();
    int clearance = w + m_settings.m_spacing;

    return m_result.CheckSelfIntersections( aShape, clearance );
}

  
const PNS_ITEMSET PNS_MEANDER_PLACER::Traces()
{
    m_currentTrace = PNS_LINE(*m_originLine, m_finalShape);
    return PNS_ITEMSET( &m_currentTrace );
}

const VECTOR2I& PNS_MEANDER_PLACER::CurrentEnd() const
{
    return VECTOR2I (0, 0); // fixme!
}
    
int PNS_MEANDER_PLACER::CurrentNet() const
{
    return m_initialSegment->Net();
}

int PNS_MEANDER_PLACER::CurrentLayer() const
{
    return m_initialSegment->Layers().Start();
}


const wxString PNS_MEANDER_PLACER::TuningInfo() const
{
    wxString status;

    switch (m_lastStatus)
    {
        case TOO_LONG:
            status = _("Too long: ");
            break;
        case TOO_SHORT:
            status = _("Too short: ");
            break;
        case TUNED:
            status = _("Tuned: ");
            break;
        default:
            return _("?");
    }

    status += LengthDoubleToString( (double) m_lastLength, false );
    status += "/";
    status += LengthDoubleToString( (double) m_settings.m_targetLength, false );
    
    return status;
}

PNS_MEANDER_PLACER::TUNING_STATUS PNS_MEANDER_PLACER::TuningStatus() const
{
    return m_lastStatus;
}
