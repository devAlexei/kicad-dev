#include <vector>

#include <geometry/constraint_solver.h>
#include <geometry/point_set.h>

#include <class_edge_mod.h>
#include <class_dimension.h>
#include <class_drawsegment.h>
#include <class_constraint.h>
#include <class_zone.h>
#include <class_board.h>
#include <class_module.h>

GS_SEGMENT* GS_ANCHOR::NeighbourSegment( GS_SEGMENT* aCurrent )
{
    GS_SEGMENT* rv = nullptr;
    int count = 0;

    for( auto link : m_linkedAnchors )
    {
        auto parent = link->GetParent();

        if( parent->Type() == GST_SEGMENT && parent != aCurrent )
        {
            rv = static_cast<GS_SEGMENT*>( parent );
            count++;
        }
    }

    if( count > 1 )
        return nullptr;

    return rv;
}


/*GS_SEGMENT::GS_SEGMENT( DRAWSEGMENT* aSeg )
 *   : GS_ITEM( GST_SEGMENT, aSeg )
 *  {
 *   m_p0    = aSeg->GetStart();
 *   m_p1    = aSeg->GetEnd();
 *   m_dir   = m_p1 - m_p0;
 *   m_midpoint = (m_p0 + m_p1) / 2;
 *   m_anchors.push_back( new GS_ANCHOR( this, 0, m_p0 ) );
 *   m_anchors.push_back( new GS_ANCHOR( this, 1, m_p1 ) );
 *   auto anc_midpoint = new GS_ANCHOR( this, 2, m_midpoint );
 *   anc_midpoint->SetConstrainable( false );
 *   m_anchors.push_back( anc_midpoint );
 *   m_parent = aSeg;
 *  }
 */

GS_SEGMENT::~GS_SEGMENT()
{
}


void GS_SEGMENT::init()
{
    m_dir = m_p1 - m_p0;
    m_midpoint = (m_p0 + m_p1) / 2;
    m_anchors.push_back( new GS_ANCHOR( this, 0, m_p0 ) );
    m_anchors.push_back( new GS_ANCHOR( this, 1, m_p1 ) );
    auto anc_midpoint = new GS_ANCHOR( this, 2, m_midpoint );
    anc_midpoint->SetConstrainable( false );
    m_anchors.push_back( anc_midpoint );

    SaveState();
}


void GS_SEGMENT::MoveAnchor( int aId, const VECTOR2I& aP, std::vector<GS_ANCHOR*>& aChangedAnchors )
{
    //printf( "MoveAnchor id %d constraint %x\n", aId, m_constrain );

    switch( aId )
    {
    case 0:

        if( m_constrain == ( CS_DIRECTION | CS_LENGTH ) )
        {
            auto d = m_anchors[0]->GetPos() - aP;
            m_anchors[0]->SetNextPos( m_anchors[0]->GetPos() - d );
            m_anchors[1]->SetNextPos( m_anchors[1]->GetPos() - d );
        }
        else if( m_constrain == CS_DIRECTION )
        {
            auto neighbour = m_anchors[1]->NeighbourSegment( this );

            SEG s( aP, aP + m_dir );


            if( !neighbour )
            {
                m_anchors[0]->SetNextPos( aP );
                m_anchors[1]->SetNextPos( s.LineProject( m_anchors[1]->GetPos() ) );
            }
            else
            {
                auto ip = neighbour->GetSeg().IntersectLines( s );
                if ( ip )
                {
                    m_anchors[0]->SetNextPos( aP );
                    m_anchors[1]->SetNextPos( *ip );
                }
            }
        }
        else
        {
            m_anchors[0]->SetNextPos( aP );
            m_anchors[1]->SetNextPos( m_anchors[1]->GetPos() );
        }

        // m_anchors[2]->SetNextPos( (m_anchors[0]->GetNextPos() + m_anchors[1]->GetNextPos() ) / 2 );

        break;

    case 1:

        if( m_constrain == ( CS_DIRECTION | CS_LENGTH ) )
        {
            auto d = m_anchors[1]->GetPos() - aP;
            m_anchors[0]->SetNextPos( m_anchors[0]->GetPos() - d );
            m_anchors[1]->SetNextPos( m_anchors[1]->GetPos() - d );
        }
        else if( m_constrain == CS_DIRECTION )
        {
            auto neighbour = m_anchors[0]->NeighbourSegment( this );

            //printf( "ConstrainDir2!\n" );
            SEG s( aP, aP + m_dir );

            if( !neighbour )
            {
                m_anchors[1]->SetNextPos( aP );
                m_anchors[0]->SetNextPos( s.LineProject( m_anchors[0]->GetPos() ) );
            }
            else
            {
                auto ip = neighbour->GetSeg().IntersectLines( s );
                if ( ip )
                {
                    m_anchors[1]->SetNextPos( aP );
                    m_anchors[0]->SetNextPos( *ip );
                }
            }
        }
        else
        {
            m_anchors[0]->SetNextPos( m_anchors[0]->GetPos() );
            m_anchors[1]->SetNextPos( aP );
        }

        // m_anchors[2]->SetNextPos( (m_anchors[0]->GetNextPos() + m_anchors[1]->GetNextPos() ) / 2 );

        break;

    case 2:
    {
        auto dir = (m_anchors[0]->GetPos() - m_anchors[1]->GetPos() );

        SEG guide = SEG( m_anchors[2]->GetPos(), m_anchors[2]->GetPos() + dir.Perpendicular() );

        auto delta = guide.LineProject( aP ) - m_anchors[2]->GetPos();

        m_anchors[0]->SetNextPos( m_anchors[0]->GetPos() + delta );
        m_anchors[1]->SetNextPos( m_anchors[1]->GetPos() + delta );
        break;
    }

    default:
        assert( false );
    }

    for( int i = 0; i<2; i++ )
        if( m_anchors[i]->PositionChanged() )
            aChangedAnchors.push_back( m_anchors[i] );

}


void GS_SEGMENT::SaveState()
{
    //printf( "Save!\n" );
    m_p0_saved  = m_p0;
    m_p1_saved  = m_p1;
    // m_midpoint_saved = m_midpoint;
}


void GS_SEGMENT::RestoreState()
{
    //printf( "Restore!\n" );
    m_p0    = m_p0_saved;
    m_p1    = m_p1_saved;
    // m_midpoint = m_midpoint_saved;
    m_anchors[0]->SetPos( m_p0 );
    m_anchors[1]->SetPos( m_p1 );
    m_anchors[2]->SetPos( (m_p0 + m_p1) / 2 );
}


void GS_SEGMENT::UpdateAnchors()
{
    m_anchors[0]->UpdatePos();
    m_anchors[1]->UpdatePos();
    m_p0    = m_anchors[0]->GetPos();
    m_p1    = m_anchors[1]->GetPos();
    m_anchors[2]->SetPos( (m_p0 + m_p1) / 2 );
    // m_midpoint    = m_anchors[2]->GetPos();

    //printf( "update-anchors %d %d %d %d\n", m_p0.x, m_p0.y, m_p1.x, m_p1.y );
}


GS_LINEAR_CONSTRAINT::GS_LINEAR_CONSTRAINT( const VECTOR2I& aP0,
        const VECTOR2I& aP1,
        const VECTOR2I& aDirection,
        const VECTOR2I& aOrigin )
    : GS_ITEM(
            GST_LINEAR_CONSTRAINT,
            nullptr )
{
    m_p0    = aP0;
    m_p1    = aP1;
    m_dir   = aDirection;
    m_origin = aOrigin;
    m_anchors.push_back( new GS_ANCHOR( this, 0, m_p0 ) );
    m_anchors.push_back( new GS_ANCHOR( this, 1, m_p1 ) );
    auto anc_origin = new GS_ANCHOR( this, 2, m_origin );
    anc_origin->SetConstrainable( false );
    m_anchors.push_back( anc_origin );
}


void GS_LINEAR_CONSTRAINT::SaveState()
{
    m_p0_saved  = m_p0;
    m_p1_saved  = m_p1;
    m_origin_saved = m_origin;
}


void GS_LINEAR_CONSTRAINT::RestoreState()
{
    m_p0    = m_p0_saved;
    m_p1    = m_p1_saved;
    m_origin = m_origin_saved;
    m_anchors[0]->SetPos( m_p0 );
    m_anchors[1]->SetPos( m_p1 );
    m_anchors[2]->SetPos( m_origin );
}


bool GS_LINEAR_CONSTRAINT::IsSatisfied() const
{
    return ( m_p0 + m_dir - m_p1 ).EuclideanNorm() <= 1;
}


void GS_LINEAR_CONSTRAINT::UpdateAnchors()
{
    m_anchors[0]->UpdatePos();
    m_anchors[1]->UpdatePos();
    m_anchors[2]->UpdatePos();
    m_p0    = m_anchors[0]->GetPos();
    m_p1    = m_anchors[1]->GetPos();
    m_origin = m_anchors[2]->GetPos();
}


void GS_LINEAR_CONSTRAINT::MoveAnchor( int aId,
        const VECTOR2I& aP,
        std::vector<GS_ANCHOR*>& aChangedAnchors )
{
    switch( aId )
    {
    case 0:
    {
        auto    dp1 = m_anchors[1]->GetPos() - m_anchors[0]->GetPos();
        auto    dp2 = m_anchors[2]->GetPos() - m_anchors[0]->GetPos();
        m_anchors[ 0 ]->SetNextPos( aP );
        m_anchors[ 1 ]->SetNextPos( aP + dp1 );
        m_anchors[ 2 ]->SetNextPos( aP + dp2 );
        aChangedAnchors.push_back( m_anchors[0] );
        aChangedAnchors.push_back( m_anchors[1] );
// aChangedAnchors.push_back( m_anchors[2] );
        break;
    }

    case 1:
    {
        auto    dp1 = m_anchors[1]->GetPos() - m_anchors[0]->GetPos();
        auto    dp2 = m_anchors[1]->GetPos() - m_anchors[2]->GetPos();

        m_anchors[ 0 ]->SetNextPos( aP - dp1 );
        m_anchors[ 1 ]->SetNextPos( aP );
        m_anchors[ 2 ]->SetNextPos( aP - dp2 );
        aChangedAnchors.push_back( m_anchors[0] );
        aChangedAnchors.push_back( m_anchors[1] );
// aChangedAnchors.push_back( m_anchors[2] );
        break;
    }

    case 2:
    {
        m_anchors[ 2 ]->SetNextPos( aP );
        //printf( "cla %d %d\n", aP.x, aP.y );
// aChangedAnchors.push_back( m_anchors[2] );

        break;
    }

    default:
        break;
    }
}


GEOM_SOLVER::GEOM_SOLVER()
{
}


GEOM_SOLVER::~GEOM_SOLVER()
{
}


GS_ANCHOR* GEOM_SOLVER::FindAnchor( VECTOR2I pos, double snapRadius )
{
    for( auto anchor : m_anchors )
    {
        auto d = anchor->GetPos() - pos;

        //printf( "d %d %d\n", d.x, d.y );

        if( abs( d.x ) <= snapRadius && abs( d.y ) <= snapRadius )
            return anchor;
    }

    return nullptr;
}


void GEOM_SOLVER::StartMove()
{
    for( auto item : m_items )
        item->SaveState();
}


bool GEOM_SOLVER::ValidateContinuity()
{
    for( auto anchor : m_anchors )
    {
        int d_max = 0;

        for( auto link : anchor->GetLinks() )
        {
            d_max = std::max( d_max, ( link->GetPos() - anchor->GetPos() ).EuclideanNorm() );

            if( d_max > c_epsilon )
                return false;
        }
    }

    return true;
}


bool GEOM_SOLVER::IsResultOK() const
{
    return m_lastResultOk;
}


bool GEOM_SOLVER::MoveAnchor( GS_ANCHOR* refAnchor, VECTOR2I pos )
{
    std::deque<DISPLACEMENT> Q;

    m_lastResultOk = false;

    //printf( "move-a %p (%d %d)\n", refAnchor, pos.x, pos.y );

    for( auto item : m_items )
        item->RestoreState();

    //for( auto anchor : m_anchors )
    //    printf( " a %p parent %p\n", anchor, anchor->GetParent() );

    const int iterLimit = 100;
    int iter = 0;

    Q.push_back( DISPLACEMENT( refAnchor, pos ) );

    while( !Q.empty() && iter < iterLimit )
    {
        //printf( "iter %d disps %d\n", iter, Q.size() );
        iter++;

        std::set<GS_ANCHOR*> moved;

        for( auto& displacement : Q )
        {
            if( moved.find( displacement.anchor ) == moved.end() )
            {
                //printf( "do %p\n", displacement.anchor );
                std::vector<GS_ANCHOR*> changedAnchors;
                displacement.anchor->GetParent()->MoveAnchor(
                        displacement.anchor->GetId(), displacement.p, changedAnchors );
                //printf( "changed links : %d\n", changedAnchors.size() );

                for( auto am : changedAnchors )
                {
                    //printf( "  change %p\n", am );
                    moved.insert( am );
                }
            }
        }

        Q.clear();

        //printf( "Moved links : %d\n", moved.size() );

        for( auto anchor : moved )
        {
        //    printf( "anchr %p\N", anchor );

            for( auto link : anchor->GetLinks() )
            {
        //        printf( "- provess link: %p\n", link );
                Q.push_back( DISPLACEMENT( link, anchor->GetNextPos() ) );
            }
        }

        std::random_shuffle( Q.begin(), Q.end() );

        for( auto anchor : moved )
        {
        //    printf( "update %p %p\n", anchor, anchor->GetParent() );
            anchor->GetParent()->UpdateAnchors();
        }

        if( ValidateContinuity() )
            break;
    }

    m_lastResultOk = true;

    if( iter == iterLimit )
        m_lastResultOk = false;


    if( !m_lastResultOk )
    {
        printf("Unable to solve after %d iterations\n", iter);
    } else {
        printf("%d iterations\n", iter);
    }


    return m_lastResultOk;
}


void GEOM_SOLVER::Add( GS_ITEM* aItem, bool aPrimary )
{
    m_items.push_back( aItem );
}

const std::vector<GS_ANCHOR*> GEOM_SOLVER::AllAnchors()
{
    std::vector<GS_ANCHOR*> allAnchors;

    for( auto item : m_items )
    {
        const auto& ia = item->GetAnchors();
        std::move( ia.begin(), ia.end(), std::back_inserter( allAnchors ) );
    }

    // printf("anchors: %d\n", allAnchors.size() );
    return allAnchors;
}


const std::vector<GS_ITEM*>& GEOM_SOLVER::AllItems()
{
    return m_items;
}


template <class T>
T* createOrGetParent( GS_ITEM* aItem, bool aCopy )
{
    if( aCopy )
    {
        T* item = new T( *static_cast<T*> (aItem->GetParent() ) );
        return item;
    }
    else
    {
        return static_cast<T*> (aItem->GetParent() );
    }
}


#if 0
void GS_SEGMENT::Commit( BOARD_ITEM* aTarget )
{
    auto ds = static_cast<DRAWSEGMENT*> ( aTarget ? aTarget : GetParent() );

    ds->SetStart( (wxPoint) GetStart() );
    ds->SetEnd( (wxPoint) GetEnd() );
}


void GS_LINEAR_CONSTRAINT::Commit( BOARD_ITEM* aTarget )
{
    auto ds = static_cast<CONSTRAINT_LINEAR*> ( aTarget ? aTarget : GetParent() );

    ds->SetP0( GetP0() );
    ds->SetP1( GetP1() );
    ds->SetMeasureLineOrigin( GetOrigin() );
}


#endif

bool GEOM_SOLVER::findOutlineElements( GS_ITEM* aItem )
{
    std::deque<GS_ANCHOR*>  Q;
    std::set<GS_ANCHOR*>    processed;


    for( auto a : aItem->GetAnchors() )
    {
        Q.push_back( a );
        processed.insert( a );
    }

    //printf( "Init: %d a\n", Q.size() );

    while( !Q.empty() )
    {
        auto current = Q.front();
        Q.pop_front();

        auto scanLinks = [&] ( const GS_ANCHOR* link )
                         {
                             current->Link( link );

                             for( auto ll : link->GetParent()->GetAnchors() )
                             {
                                 // printf("Do link %p\n", ll );
                                 if( processed.find( ll ) == processed.end() )
                                 {
                                     // printf("Scan %p\n", ll);
                                     processed.insert( ll );
                                     Q.push_back( ll );
                                 }
                             }
                         };

        m_allAnchors.Find( current->GetPos(), c_epsilon, scanLinks );
    }

    for( auto anchor : processed )
    {
        anchor->GetParent()->SetPrimary( true );
        m_anchors.push_back( anchor );
    }

    return true;
}


void GEOM_SOLVER::FindOutlines()
{
    //printf( "find-outl: %d items\n", m_items.size() );

    m_allAnchors.Reserve( 10000 );
    m_allAnchors.Clear();

    for( auto item : m_items )
    {
        const auto& ia = item->GetAnchors();

    //    printf( "item %p %d anchors primary %d\n", item, ia.size(), !!item->IsPrimary() );

        for( const auto& anchor : ia )
        {
            m_allAnchors.Add( anchor );
        }
    }

    m_allAnchors.Sort();
    m_anchors.clear();

    for( auto item : m_items )
        if( item->IsPrimary() )
            findOutlineElements( item );




    printf( "total outline anchors : %d\n", m_anchors.size() );
}


void GEOM_SOLVER::Clear()
{
    m_allAnchors.Clear();

    // for ( auto a : m_anchors )
    // delete a;
    // for ( auto item : m_items )
    // delete item;

    m_anchors.clear();
    m_items.clear();
}

bool GEOM_SOLVER::FilletCorner( GS_SEGMENT *aSeg1, GS_SEGMENT *aSeg2, int aDistance )
{
    if( aSeg1->GetStart() == aSeg2->GetEnd() )
    {
        std::swap(aSeg1, aSeg2);
    }

    if ( aSeg1->GetEnd() != aSeg1->GetStart() )
    {
        return false;
    }

    auto s1 = aSeg1->GetSeg();
    auto s2 = aSeg2->GetSeg();

    auto l1 = s1.Length();
    auto l2 = s2.Length();

    if (l1 < aDistance || l2 < aDistance )
        return false;

    auto s1b_next = s1.A + (s1.B - s1.A).Resize( l1 - aDistance );
    auto s2a_next = s2.A + (s2.B - s2.A).Resize( aDistance );

    aSeg1->SetEnd( s1b_next );
    aSeg2->SetStart( s2a_next );

    auto newSeg = new GS_SEGMENT( s1b_next, s2a_next );

    Add( newSeg, true );

    return true;
}

bool GEOM_SOLVER::FilletCorners( std::vector<GS_SEGMENT*> aSegs, int aDistance )
{

}
