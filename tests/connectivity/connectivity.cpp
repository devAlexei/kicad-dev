#include <class_board.h>
#include <class_pad.h>
#include <class_module.h>
#include <class_zone.h>

#include <geometry/shape_poly_set.h>

#include <io_mgr.h>
#include <profile.h>

#include <memory>
#include <algorithm>
#include <functional>
#include <vector>
#include <deque>
#include <stdarg.h>

using std::shared_ptr;


template <class T>
class INTRUSIVE_LIST
{
public:
    INTRUSIVE_LIST<T>()
    {
        m_prev  = nullptr;
        m_next  = nullptr;
        m_root  = (T*) this;
        m_count = 1;
    }

    T* ListRemove()
    {
        if( m_prev )
            m_prev->m_next = m_next;

        if( m_next )
            m_next->m_prev = m_prev;

        m_root->m_count--;

        T* rv = nullptr;

        if( m_prev )
            rv = m_prev;
        else if( m_next )
            rv = m_next;

        m_root  = nullptr;
        m_prev  = nullptr;
        m_next  = nullptr;
        return rv;
    }

    int ListSize() const
    {
        return m_root ? m_root->m_count : 0;
    }

    void ListInsert( T* item )
    {
        if( !m_root )
            m_root = item;

        if( m_next )
            m_next->m_prev = item;

        item->m_prev = (T*) this;
        item->m_next = m_next;
        item->m_root = m_root;
        m_root->m_count++;

        m_next = item;
    }

    T* ListNext() const { return m_next; };
    T* ListPrev() const { return m_prev; };

private:
    int m_count;
    T* m_prev;
    T* m_next;
    T* m_root;
};

class CN_ITEM : public INTRUSIVE_LIST<CN_ITEM>
{
private:
    BOARD_CONNECTED_ITEM* m_parent;
    std::vector<CN_ITEM*> m_connected;
    bool m_visited;
    bool m_canChangeNet;

public:
    CN_ITEM( BOARD_CONNECTED_ITEM *aParent, bool aCanChangeNet )
    {
        m_parent = aParent;
        m_canChangeNet = aCanChangeNet;
        m_visited = false;
    }

    BOARD_CONNECTED_ITEM *Parent() const
    {
        return m_parent;
    }

    void Connect( CN_ITEM* aOther )
    {
        m_connected.push_back( aOther );
    }

    const std::vector<CN_ITEM*>& ConnectedItems()  const
    {
        return m_connected;
    }

    void ClearConnections()
    {
        m_connected.clear();
    }

    void SetVisited ( bool aVisited )
    {
        m_visited = aVisited;
    }

    bool Visited() const
    {
        return m_visited;
    }

    bool CanChangeNet() const
    {
            return m_canChangeNet;
    }

    static void Connect( CN_ITEM* a, CN_ITEM* b )
    {
       a->m_connected.push_back( b );
       b->m_connected.push_back( a );
    }
};

class CN_ANCHOR
{
public:
    CN_ANCHOR( const VECTOR2I& aPos, CN_ITEM* aItem )
    {
        m_valid = true;
        m_pos   = aPos;
        m_item  = aItem;
    }

    bool operator<( const CN_ANCHOR& aOther ) const
    {
        if( m_pos.x == aOther.m_pos.x )
            return m_pos.y < aOther.m_pos.y;
        else
            return m_pos.x < aOther.m_pos.x;
    }

    CN_ITEM* Item() const
    {
        return m_item;
    }

    BOARD_CONNECTED_ITEM *Parent() const
    {
        return m_item->Parent();
    }

    const VECTOR2I& Pos() const
    {
        return m_pos;
    }

private:

    bool m_valid;
    VECTOR2I m_pos;
    CN_ITEM* m_item;
};


class CN_LIST
{
private:
    bool m_dirty;
    std::vector<CN_ANCHOR> m_anchors;

protected:

    std::vector<CN_ITEM*> m_items;

    void setDirty( bool aDirty = true )
    {
        m_dirty = aDirty;
    }

    void addAnchor( VECTOR2I pos, CN_ITEM* item )
    {
        m_anchors.push_back( CN_ANCHOR( pos, item ) );
    }

    using ITER = decltype(m_items)::iterator;

public:
    CN_LIST()
    {
        m_dirty = false;
    };

    ITER begin() { return m_items.begin(); };
    ITER end() { return m_items.end(); };


    template <class T>
    void FindNearby( VECTOR2I aPosition, int aDistMax, T aFunc );

    template <class T>
    void FindNearby( BOX2I aBBox, T aFunc );

    void ClearConnections()
    {
        for( auto& anchor : m_anchors )
            anchor.Item()->ClearConnections();
    }

    int Size() const
    {
        return m_items.size();
    }

    void Remove( CN_ITEM* aItem )
    {
        auto i = std::find( m_items.begin(), m_items.end(), aItem );

        assert( i != m_items.end() );
        m_items.erase( i );
        setDirty();
    }

    void Remove( BOARD_CONNECTED_ITEM* aItem )
    {
        m_items.erase(
                std::remove_if(
                        m_items.begin(), m_items.end(),
                        [aItem]( CN_ITEM* item ) { return item->Parent() == aItem; } ),
                m_items.end() );

        m_anchors.erase(
                std::remove_if(
                        m_anchors.begin(), m_anchors.end(),
                        [aItem]( CN_ANCHOR& p ) { return p.Parent() == aItem; } ),
                m_anchors.end() );


        setDirty();
    }
};


// vector<CN_ANCHOR*> m_candidates;
// vector<CN_CLUSTER> m_clusters;

template <class T>
void CN_LIST::FindNearby( BOX2I aBBox, T aFunc )
{
    for( auto p : m_anchors )
        aFunc( p );
}


template <class T>
void CN_LIST::FindNearby( VECTOR2I aPosition, int aDistMax, T aFunc )
{
    /* Search items in m_Candidates that position is <= aDistMax from aPosition
     * (Rectilinear distance)
     * m_Candidates is sorted by X then Y values, so a fast binary search is used
     * to locate the "best" entry point in list
     * The best entry is a pad having its m_Pos.x == (or near) aPosition.x
     * All candidates are near this candidate in list
     * So from this entry point, a linear search is made to find all candidates
     */

    if( m_dirty )
    {
        sort( m_anchors.begin(), m_anchors.end() );
        m_dirty = false;
    }

    int idxmax = m_anchors.size() - 1;

    int delta = m_anchors.size();

    int idx = 0;        // Starting index is the beginning of list

    while( delta )
    {
        // Calculate half size of remaining interval to test.
        // Ensure the computed value is not truncated (too small)
        if( (delta & 1) && ( delta > 1 ) )
            delta++;

        delta /= 2;

        CN_ANCHOR& p = m_anchors[idx];

        int dist = p.Pos().x - aPosition.x;

        if( std::abs( dist ) <= aDistMax )
        {
            break;                              // A good entry point is found. The list can be scanned from this point.
        }
        else if( p.Pos().x < aPosition.x )      // We should search after this point
        {
            idx += delta;

            if( idx > idxmax )
                idx = idxmax;
        }
        else    // We should search before this p
        {
            idx -= delta;

            if( idx < 0 )
                idx = 0;
        }
    }

    /* Now explore the candidate list from the "best" entry point found
     * (candidate "near" aPosition.x)
     * We exp the list until abs(candidate->m_Point.x - aPosition.x) > aDistMashar* Currently a linear search is made because the number of candidates
     * having the right X position is usually small
     */
    // search next candidates in list
    VECTOR2I diff;

    for( int ii = idx; ii <= idxmax; ii++ )
    {
        CN_ANCHOR& p = m_anchors[ii];
        diff = p.Pos() - aPosition;;

        if( std::abs( diff.x ) > aDistMax )
            break; // Exit: the distance is to long, we cannot find other candidates

        if( std::abs( diff.y ) > aDistMax )
            continue; // the y distance is to long, but we can find other candidates

        // We have here a good candidate: add it
        aFunc( p );
    }

    // search previous candidates in list
    for(  int ii = idx - 1; ii >=0; ii-- )
    {
        CN_ANCHOR& p = m_anchors[ii];
        diff = p.Pos() - aPosition;

        if( abs( diff.x ) > aDistMax )
            break;

        if( abs( diff.y ) > aDistMax )
            continue;

        // We have here a good candidate:add it
        aFunc( p );
    }
}


using namespace std;

shared_ptr<BOARD> m_board;


struct CN_PAD_LIST : public CN_LIST
{

    CN_ITEM* Add( D_PAD* pad )
    {
        auto item = new CN_ITEM( pad, false );

        // item->m_item = pad;
        // item->m_canChangeNet = false;
        addAnchor( pad->ShapePos(), item );
        m_items.push_back( item );

        setDirty();
        return item;
    };
};

struct CN_TRACK_LIST : public CN_LIST
{
    CN_ITEM* Add( TRACK* track )
    {
        auto item = new CN_ITEM( track, true );

        // item->m_item = track;
        // item->m_canChangeNet = true;
        m_items.push_back( item );

        addAnchor( track->GetStart(), item );
        addAnchor( track->GetEnd(), item );
        setDirty();
        return item;
    };
};

struct CN_VIA_LIST : public CN_LIST
{
    CN_ITEM* Add( VIA* via )
    {
        auto item = new CN_ITEM( via, true );
        m_items.push_back( item );
        addAnchor( via->GetStart(), item );
        setDirty();
        return item;
    };
};

class CN_ZONE : public CN_ITEM
{
public:
    CN_ZONE ( BOARD_CONNECTED_ITEM *aParent, bool aCanChangeNet, int aSubpolyIndex ) :
        CN_ITEM (aParent, aCanChangeNet ),
        m_subpolyIndex ( aSubpolyIndex )
    {

    }

    int SubpolyIndex() const
    {
        return m_subpolyIndex;
    }

private:
    int m_subpolyIndex;
};

struct CN_ZONE_LIST : public CN_LIST
{
    CN_ZONE_LIST()
    {
    }

    const std::vector<CN_ITEM*> Add( ZONE_CONTAINER* zone )
    {
        const SHAPE_POLY_SET& polys = zone->GetFilledPolysList();

        vector<CN_ITEM*> rv;

        for( int j = 0; j < polys.OutlineCount(); j++ )
        {
            CN_ZONE* zitem = new CN_ZONE( zone, false, j );
            m_items.push_back( zitem );
            rv.push_back( zitem );
            setDirty();
        }

        return rv;
    };
};

class CN_CLUSTER
{
private:

    bool m_conflicting;
    int m_originNet;
    CN_ITEM* m_originPad;
    std::vector<CN_ITEM*> m_items;
public:
    CN_CLUSTER()
    {
        m_items.reserve( 64 );
        m_originPad = nullptr;
        m_originNet = -1;
        m_conflicting = false;
    }

    bool HasValidNet() const
    {
        return m_originNet >= 0;
    }

    int OriginNet() const
    {
        return m_originNet;
    }

    wxString OriginNetName() const
    {
        if( !m_originPad )
            return "<none>";
        else
            return m_originPad->Parent()->GetNetname();
    }

    bool Contains( CN_ITEM* aItem )
    {
        return std::find( m_items.begin(), m_items.end(), aItem ) != m_items.end();
    }

    bool Contains( BOARD_CONNECTED_ITEM* aItem )
    {
        for( auto item : m_items )
            if( item->Parent() == aItem )
                return true;


        return false;
    }

    void Dump()
    {
        for( auto item : m_items )
            printf( " - item : %p bitem : %p type : %d inet %s\n", item, item->Parent(),
                    item->Parent()->Type(), (const char*) item->Parent()->GetNetname().c_str() );
    }

    int Size() const
    {
        return m_items.size();
    }

    bool HasNet() const
    {
        return m_originNet >= 0;
    }

    bool IsOrphaned() const
    {
        return m_originPad == nullptr;
    }

    bool IsConflicting() const
    {
        return m_conflicting;
    }

    void Add( CN_ITEM* item )
    {
        m_items.push_back( item );

        // printf("cluster %p add %p\n", this, item );

        if( item->Parent()->Type() == PCB_PAD_T )
        {
            if( !m_originPad )
            {
                m_originPad = item;
                m_originNet = item->Parent()->GetNetCode();
            }

            if( m_originPad && item->Parent()->GetNetCode() != m_originNet )
            {
                m_conflicting = true;
            }
        }
    }

    using ITER = decltype(m_items)::iterator;

    ITER begin() { return m_items.begin(); };
    ITER end() { return m_items.end(); };


};

class CN_CONNECTIVITY_IMPL
{
    vector<CN_CLUSTER*> m_clusters;

public:

    CN_CONNECTIVITY_IMPL()
    {

    }

    void searchConnections( bool aIncludeZones = false )
    {
        auto checkForConnection = [] ( const CN_ANCHOR& point, CN_ITEM *aRefItem, int aMaxDist = 0)
                        {
                            const auto parent = aRefItem->Parent();

                            if( parent == point.Item()->Parent() )
                                return;

                            if( !( parent->GetLayerSet() &
                                   point.Item()->Parent()->GetLayerSet() ).any() )
                                return;

                            switch ( parent->Type() )
                            {
                                case PCB_PAD_T:
                                case PCB_VIA_T:

                                    if( parent->HitTest( wxPoint( point.Pos().x, point.Pos().y ) ) )
                                        CN_ITEM::Connect( aRefItem, point.Item() );

                                    break;
                                case PCB_TRACE_T:
                                {
                                    const auto track = static_cast<TRACK*> ( parent );

                                    const VECTOR2I d_start( VECTOR2I( track->GetStart() ) - point.Pos() );
                                    const VECTOR2I d_end( VECTOR2I( track->GetEnd() ) - point.Pos() );

                                    if( d_start.EuclideanNorm() < aMaxDist
                                        || d_end.EuclideanNorm() < aMaxDist )
                                        CN_ITEM::Connect( aRefItem, point.Item() );
                                    break;

                                }

                                case PCB_ZONE_T:
                                case PCB_ZONE_AREA_T:
                                {
                                    const auto zone = static_cast<ZONE_CONTAINER*> ( parent );
                                    auto zoneItem = static_cast<CN_ZONE*> ( aRefItem );
                                    const auto& polys = zone->GetFilledPolysList();

                                    if( point.Item()->Parent()->GetNetCode() != parent->GetNetCode() )
                                        return;

                                    if( !( zone->GetLayerSet() &
                                                               point.Item()->Parent()->GetLayerSet() ).any() )
                                                            return;

                                                        if( polys.Contains( point.Pos(), zoneItem->SubpolyIndex() ) )
                                                            CN_ITEM::Connect( zoneItem, point.Item() );
                                    break;

                                }
                                default :
                                    printf("unhandled_type %d\n", parent->Type() );
                                    assert ( false );
                            }
                        };

        PROF_COUNTER search_cnt( "search-connections" ); search_cnt.start();

        padList.ClearConnections();
        viaList.ClearConnections();
        trackList.ClearConnections();
        zoneList.ClearConnections();

        using namespace std::placeholders;

        for( auto padItem : padList )
        {
            auto pad = static_cast<D_PAD*> ( padItem->Parent() );
            auto searchPads = std::bind( checkForConnection, _1, padItem );

            padList.FindNearby( pad->ShapePos(), pad->GetBoundingRadius(), searchPads );
            trackList.FindNearby( pad->ShapePos(), pad->GetBoundingRadius(), searchPads );
            viaList.FindNearby( pad->ShapePos(), pad->GetBoundingRadius(), searchPads );
        }

        for( auto& trackItem : trackList )
        {
            auto track = static_cast<TRACK*> ( trackItem->Parent() );
            int dist_max = track->GetWidth() / 2;
            auto searchTracks = std::bind( checkForConnection, _1, trackItem, dist_max );

            trackList.FindNearby( track->GetStart(), dist_max, searchTracks );
            trackList.FindNearby( track->GetEnd(), dist_max, searchTracks );
        }

        for( auto& viaItem : viaList )
        {
            auto via = static_cast<VIA*> ( viaItem->Parent() );
            int dist_max = via->GetWidth() / 2;
            auto searchVias = std::bind( checkForConnection, _1, viaItem, dist_max );

            viaList.FindNearby( via->GetStart(), dist_max, searchVias );
            trackList.FindNearby( via->GetStart(), dist_max, searchVias );
        }

        if( aIncludeZones )
        {
            for( auto& zoneItem : zoneList )
            {
                auto searchZones = std::bind( checkForConnection, _1, zoneItem );

                // fixme: use bounding boxes
                viaList.FindNearby( BOX2I(), searchZones );
                trackList.FindNearby( BOX2I(), searchZones );
                padList.FindNearby(  BOX2I(), searchZones );
            }
        }

        search_cnt.show();
    }

    void searchClusters( bool aIncludeZones = false )
    {

        std::deque<CN_ITEM*> Q;
        CN_ITEM* head = nullptr;
        m_clusters.clear();

        PROF_COUNTER cnt( "search-clusters" );
        cnt.start();

        auto addToSearchList = [&head] ( CN_ITEM *aItem )
        {
                aItem->SetVisited( false );

                if ( !head )
                    head = aItem;
                else
                    head->ListInsert( aItem );
        };

        std::for_each( padList.begin(), padList.end(), addToSearchList );
        std::for_each( trackList.begin(), trackList.end(), addToSearchList );
        std::for_each( viaList.begin(), viaList.end(), addToSearchList );

        if (aIncludeZones)
        {
            std::for_each( zoneList.begin(), zoneList.end(), addToSearchList );
        }


        while( head )
        {
            auto cluster = new CN_CLUSTER();

            Q.clear();
            CN_ITEM* root = head;
            root->SetVisited ( true );

            head = root->ListRemove();

            Q.push_back( root );

            while( Q.size() )
            {
                CN_ITEM* current = Q.front();

                Q.pop_front();
                cluster->Add( current );

                for( auto n : current->ConnectedItems() )
                {
                    if( !n->Visited() )
                    {
                        n->SetVisited( true );
                        Q.push_back( n );
                        head = n->ListRemove();
                    }
                }
            }

            m_clusters.push_back( cluster );
        }

        cnt.show();


        std::sort( m_clusters.begin(), m_clusters.end(), []( CN_CLUSTER* a, CN_CLUSTER* b ) {
            return a->OriginNet() < b->OriginNet();
        } );

        int n_items = 0;
        int n = 0;
        for( auto cl : m_clusters )
        {
             //printf("cluster %d: net %d [%s], %d items, conflict: %d, orphan: %d \n", n, cl->OriginNet(), (const char *) cl->OriginNetName(), cl->Size(), !!cl->IsConflicting(), !!cl->IsOrphaned()  );
             //cl->Dump();
            n++;
            n_items += cl->Size();
        }

//        printf( "all cluster items : %d\n", n_items );
    }

    void SetBoard( shared_ptr<BOARD> aBoard )
    {
        for( int i = 0; i<aBoard->GetAreaCount(); i++ )
        {
            ZONE_CONTAINER* zone = aBoard->GetArea( i );

            zone->ClearFilledPolysList();
            zone->UnFill();

            // Cannot fill keepout zones:
            if( zone->GetIsKeepout() )
                continue;

            zone->BuildFilledSolidAreasPolygons( aBoard.get() );

            Add( zone );
        }

        for( auto tv : aBoard->Tracks() )
            Add( tv );

        for( auto mod : aBoard->Modules() )
            for( auto pad : mod->PadsIter() )
                Add( pad );



        m_board = aBoard;

        printf( "zones : %lu, pads : %lu vias : %lu tracks : %lu\n",
                zoneList.Size(), padList.Size(),
                viaList.Size(), trackList.Size() );
    }

    template <class Container, class BItem>
    void add( Container& c, BItem* brditem )
    {
        auto item = c.Add( brditem );

        m_itemMap.insert( ItemMapPair( brditem, item ) );
    }

    void Remove( BOARD_CONNECTED_ITEM* aItem )
    {
        switch( aItem->Type() )
        {
        case PCB_PAD_T:

            break;

        case PCB_TRACE_T:

            break;

        case PCB_VIA_T:

            break;


        case PCB_ZONE_AREA_T:
        case PCB_ZONE_T:

            zoneList.Remove( aItem );

            break;

        default:
            return;
        }
    }

    void Add( BOARD_CONNECTED_ITEM* aItem )
    {
        switch( aItem->Type() )
        {
        case PCB_PAD_T:
            add( padList, static_cast<D_PAD*>(aItem) );
            break;

        case PCB_TRACE_T:
            add( trackList, static_cast<TRACK*>(aItem) );
            break;

        case PCB_VIA_T:
            add( viaList, static_cast<VIA*>(aItem) );
            break;


        case PCB_ZONE_AREA_T:
        case PCB_ZONE_T:

            for( auto zitem : zoneList.Add( static_cast<ZONE_CONTAINER*>(aItem) ) )
            {
                m_itemMap.insert( ItemMapPair( aItem, zitem ) );
            }

            break;

        default:
            return;
        }
    }

    void update();

    void propagateConnections();

    struct CnDisjointNetEntry
    {
        int net;
        wxString netname;
        BOARD_CONNECTED_ITEM* a, * b;
        VECTOR2I anchorA, anchorB;
    };

// PUBLIC API
    void    PropagateNets();
    void    FindIsolatedCopperIslands( ZONE_CONTAINER* aZone, std::vector<int>& aIslands );
    bool    CheckConnectivity( vector<CnDisjointNetEntry>& aReport );


    CN_PAD_LIST padList;
    CN_TRACK_LIST trackList;
    CN_VIA_LIST viaList;
    CN_ZONE_LIST zoneList;

    using ItemMapPair = pair <BOARD_ITEM*, CN_ITEM*>;

    unordered_multimap<BOARD_ITEM*, CN_ITEM*> m_itemMap;
};


// for( auto mod : aBoard->Modules() )
// for( auto pad : mod->PadsIter() )

void loadBoard( string name )
{
    PLUGIN::RELEASER pi( IO_MGR::PluginFind( IO_MGR::KICAD ) );

    try
    {
        m_board.reset( pi->Load( name, NULL, NULL ) );
    }
    catch( const IO_ERROR& ioe )
    {
        wxString msg = wxString::Format( _( "Error loading board.\n%s" ),
                ioe.What() );

        fprintf( stderr, "%s\n", (const char*) msg.mb_str() );
        exit( -1 );
    }
}


void saveBoard( shared_ptr<BOARD> board, string name )
{
    PLUGIN::RELEASER pi( IO_MGR::PluginFind( IO_MGR::KICAD ) );

    try
    {
        pi->Save( name, board.get(), NULL );
    }
    catch( const IO_ERROR& ioe )
    {
        wxString msg = wxString::Format( _( "Error saving board.\n%s" ),
                ioe.What() );

        fprintf( stderr, "%s\n", (const char*) msg.mb_str() );
        exit( -1 );
    }
}


class CONNECTIVITY_DATA
{
    // add , remove, modify
    // PropageteNets()
    // FindIsolatedCopperIslands(ZONE)
    // IsConnected
    // Cluster

    // OrphanCount()
    // UnconnectedCount()
    // ConflictCount()
private:
    CN_CONNECTIVITY_IMPL *m_pimpl;
};

void CN_CONNECTIVITY_IMPL::update()
{
}


void CN_CONNECTIVITY_IMPL::propagateConnections()
{
    for( auto cluster : m_clusters )
    {
        if( cluster->IsConflicting() )
        {
            printf( "Conflicting nets in cluster %p\n", cluster );
        }
        else if( cluster->IsOrphaned() )
        {
            printf( "Skipping orphaned cluster %p [net: %s]\n", cluster,
                    (const char*) cluster->OriginNetName() );
        }
        else if( cluster->HasValidNet() )
        {
            // normal cluster: just propagate from the pads
            int n_changed = 0;

            for( auto item : *cluster )
            {
                if( item->CanChangeNet() )
                {
                    item->Parent()->SetNetCode( cluster->OriginNet() );
                    n_changed++;
                }
            }

            if( n_changed )
                printf( "Cluster %p : net : %d %s\n", cluster,
                        cluster->OriginNet(), (const char*) cluster->OriginNetName() );
            else
                printf( "Cluster %p : nothing to propagate\n", cluster );
        }
        else
        {
            printf( "Cluster %p : connected to unused net\n", cluster );
        }
    }
}


void CN_CONNECTIVITY_IMPL::PropagateNets()
{
    searchConnections( false );
    searchClusters( false );
    propagateConnections();
}


void CN_CONNECTIVITY_IMPL::FindIsolatedCopperIslands( ZONE_CONTAINER* aZone, std::vector<int>& aIslands )
{
    // auto range = m_itemMap.equal_range( (BOARD_ITEM *)aZone );
    aIslands.clear();
    zoneList.Remove( aZone );
    Add( aZone );

    // zoneList->Remove ( aZone );
    searchConnections( true );
    searchClusters( true );

    for( auto cluster : m_clusters )
        if( cluster->Contains( aZone ) && cluster->IsOrphaned() )
        {
            // printf("cluster %p found orphaned : %d\n", cluster, !!cluster->IsOrphaned());
            for( auto z : *cluster )
            {
                if( z->Parent() == aZone )
                {
                    aIslands.push_back( static_cast<CN_ZONE*>(z)->SubpolyIndex() );
                }
            }
        }



    printf( "Found isolated islands : " );

    for( auto c : aIslands )
        printf( "%d ", c );

    printf( "\n" );
}


bool CN_CONNECTIVITY_IMPL::CheckConnectivity( vector<CnDisjointNetEntry>& aReport )
{
    searchConnections( true );
    searchClusters( true );

    int maxNetCode = 0;

    for( auto cluster : m_clusters )
        maxNetCode = std::max( maxNetCode, cluster->OriginNet() );

    for( int net = 1; net <= maxNetCode; net++ )
    {
        int count = 0;
        wxString name;

        for( auto cluster : m_clusters )
            if( cluster->OriginNet() == net )
            {
                name = cluster->OriginNetName();
                count++;
            }



        if( count > 1 )
            printf( "Net %d [%s] is not completely routed (%d disjoint clusters).\n", net,
                    (const char*) name, count );
    }
};


int main( int argc, char* argv[] )
{
    if( argc < 2 )
    {
        printf( "usage : %s board_file\n", argv[0] );
        return 0;
    }

    loadBoard( argv[1] );

    CN_CONNECTIVITY_IMPL conns;

    conns.SetBoard( m_board );

    vector<int> islands;
    vector<CN_CONNECTIVITY_IMPL::CnDisjointNetEntry> report;

    conns.PropagateNets();

#if 1
    for( int i = 0; i <m_board->GetAreaCount(); i++ )
    {
        ZONE_CONTAINER* zone = m_board->GetArea( i );
        conns.FindIsolatedCopperIslands( zone, islands );


        std::sort( islands.begin(), islands.end(), std::greater<int>() );

        for( auto idx : islands )
        {
            // printf("Delete poly %d/%d\n", idx, zone->FilledPolysList().OutlineCount());
            zone->FilledPolysList().DeletePolygon( idx );
        }

        conns.Remove( zone );
        conns.Add( zone );
    }

    conns.CheckConnectivity( report );

    saveBoard( m_board, "tmp.kicad_pcb" );
    return 0;
    // conns.propagateNets();

    /*{
     *   PROF_COUNTER cnt ("build-pad-list"); cnt.start();
     *   padList.Build( )
     *   cnt.show();
     *  }
     *  {
     *   PROF_COUNTER cnt ("build-track-via-list"); cnt.start();
     *   cnt.show();
     *  }*/
    // lst.Build();
    // lst.searchConnections();
    #endif
    return 0;
}
