/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2016 CERN
 * @author Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "connectivity_impl.h"


CN_CLUSTER::CN_CLUSTER()
{
    m_items.reserve( 64 );
    m_originPad = nullptr;
    m_originNet = -1;
    m_conflicting = false;
}

CN_CLUSTER::~CN_CLUSTER()
{

}

wxString CN_CLUSTER::OriginNetName() const
{
    if( !m_originPad )
        return "<none>";
    else
        return m_originPad->Parent()->GetNetname();
}

bool CN_CLUSTER::Contains( CN_ITEM* aItem )
{
    return std::find( m_items.begin(), m_items.end(), aItem ) != m_items.end();
}


bool CN_CLUSTER::Contains( BOARD_CONNECTED_ITEM* aItem )
{
    for( auto item : m_items )
        if( item->Parent() == aItem )
            return true;
return false;
}

void CN_ITEM::Dump()
{
    printf("    valid: %d, connected: \n", !!Valid());
    for(auto i : m_connected )
    {
        TRACK *t = static_cast<TRACK*>(i->Parent());
        printf("    - %p [%d %d] [%d %d] l %d w %d\n", i, t->GetStart().x, t->GetStart().y, t->GetEnd(), t->GetEnd().y, t->GetLayer(), t->GetWidth() );
    }

    printf("    anchors: \n");
    for(auto i : m_anchors )
        printf("     - %p [%d %d] item %p\n", i, i->Pos().x, i->Pos().y, i->Item() );


}

void CN_CLUSTER::Dump()
{
    for( auto item : m_items )
    {
        wxLogTrace( "CN", " - item : %p bitem : %p type : %d inet %s\n", item, item->Parent(),
                item->Parent()->Type(), (const char*) item->Parent()->GetNetname().c_str() );
        printf( "- item : %p bitem : %p type : %d inet %s\n", item, item->Parent(),
                        item->Parent()->Type(), (const char*) item->Parent()->GetNetname().c_str() );
        item->Dump();

    }
}

void CN_CLUSTER::Add( CN_ITEM* item )
{

    m_items.push_back( item );

    if ( m_originNet < 0 )
    {
        m_originNet = item->Parent()->GetNetCode();
    }

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


CN_CONNECTIVITY_ALGO_IMPL::CN_CONNECTIVITY_ALGO_IMPL()
{
}

CN_CONNECTIVITY_ALGO_IMPL::~CN_CONNECTIVITY_ALGO_IMPL()
{
}

bool CN_CONNECTIVITY_ALGO_IMPL::Remove( BOARD_ITEM* aItem )
{
    switch( aItem->Type() )
    {
    case PCB_MODULE_T:
        for ( auto pad : static_cast<MODULE *> (aItem ) -> PadsIter() )
        {
            m_itemMap[ static_cast<BOARD_CONNECTED_ITEM*>( pad ) ].MarkItemsAsInvalid();
        }
        m_padList.SetDirty(true);

        break;
    case PCB_PAD_T:
        m_itemMap[ static_cast<BOARD_CONNECTED_ITEM*>( aItem ) ].MarkItemsAsInvalid();
        m_padList.SetDirty(true);
        break;

    case PCB_TRACE_T:
        m_itemMap[ static_cast<BOARD_CONNECTED_ITEM*>( aItem ) ].MarkItemsAsInvalid();
        m_trackList.SetDirty(true);
        break;

    case PCB_VIA_T:
        m_itemMap[ static_cast<BOARD_CONNECTED_ITEM*>( aItem ) ].MarkItemsAsInvalid();
        m_viaList.SetDirty(true);

        break;


    case PCB_ZONE_AREA_T:
    case PCB_ZONE_T:
    {
        m_itemMap[ static_cast<BOARD_CONNECTED_ITEM*>( aItem ) ].MarkItemsAsInvalid();
        m_zoneList.SetDirty(true);

        break;
    }
    default:
    return false;
    }

    return true;

}

bool CN_CONNECTIVITY_ALGO_IMPL::Add( BOARD_ITEM* aItem )
    {


        switch( aItem->Type() )
        {
        case PCB_MODULE_T:
            for ( auto pad : static_cast<MODULE *> (aItem ) -> PadsIter() )
                add( m_padList, pad );

            break;
        case PCB_PAD_T:
            add( m_padList, static_cast<D_PAD *> ( aItem  ) );

            break;

        case PCB_TRACE_T:
            add( m_trackList, static_cast<TRACK *> ( aItem ) );

            break;

        case PCB_VIA_T:
            add( m_viaList, static_cast<VIA*> (aItem ));

            break;


        case PCB_ZONE_AREA_T:
        case PCB_ZONE_T:
        {
            auto zone = static_cast<ZONE_CONTAINER*> ( aItem );
            m_itemMap[zone] = ITEM_MAP_ENTRY();

            for( auto zitem : m_zoneList.Add( zone ) )
                m_itemMap[zone].Link(zitem);

            break;
        }
        default:
            return false;
        }

        return true;
    }


void CN_CONNECTIVITY_ALGO_IMPL::searchConnections( bool aIncludeZones )
{
    auto checkForConnection = [] ( const CN_ANCHOR& point, CN_ITEM *aRefItem, int aMaxDist = 0)
                    {
                        const auto parent = aRefItem->Parent();

                        assert ( point.Item() );
                        assert ( point.Item()->Parent() );
                        assert ( aRefItem->Parent() );

                        if ( !point.Item()->Valid() )
                            return;

                        if ( !aRefItem->Valid() )
                            return;

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

                                if( point.Item()->Parent()->GetNetCode() != parent->GetNetCode() )
                                    return;

                                if( !( zone->GetLayerSet() &
                                                           point.Item()->Parent()->GetLayerSet() ).any() )
                                                        return;

                                if ( zoneItem->ContainsAnchor ( point ) )
                                    CN_ITEM::Connect( zoneItem, point.Item() );

                                break;

                            }
                            default :
                                //printf("unhandled_type %d\n", parent->Type() );
                                assert ( false );
                        }
                    };

    auto checkInterZoneConnection = [] ( const CN_ANCHOR& point, CN_ZONE *aRefZone )
    {
        auto testedZone = static_cast<CN_ZONE *> (point.Item());
        const auto parentZone = static_cast<const ZONE_CONTAINER*>(aRefZone->Parent());

        //printf("testI")

        if( testedZone->Parent()->Type () != PCB_ZONE_AREA_T )
            return;

        if (testedZone == aRefZone)
            return;

        if( testedZone->Parent()->GetNetCode() != parentZone->GetNetCode() )
            return; // we only test zones belonging to the same net

        if( !( testedZone->Parent()->GetLayerSet() &
                                      parentZone->GetLayerSet() ).any() )
            return; // and on same layer

        if( aRefZone->ContainsAnchor ( point ) )
        {
            CN_ITEM::Connect ( aRefZone, testedZone );
            return;
        }

        const auto& outline = parentZone->GetFilledPolysList().COutline( aRefZone->SubpolyIndex() );

        for( int i = 0; i < outline.PointCount(); i++ )
            if( testedZone ->ContainsPoint( outline.CPoint(i) ) )
            {
                CN_ITEM::Connect ( aRefZone, testedZone );
                return;
            }


    };


#ifdef CONNECTIVITY_DEBUG
    printf("Search start\n");
#endif

    PROF_COUNTER search_cnt( "search-connections" ); search_cnt.start();

    m_padList.RemoveInvalidItems();
    m_viaList.RemoveInvalidItems();
    m_trackList.RemoveInvalidItems();
    m_zoneList.RemoveInvalidItems();

    using namespace std::placeholders;

    for( auto padItem : m_padList )
    {
        auto pad = static_cast<D_PAD*> ( padItem->Parent() );
        auto searchPads = std::bind( checkForConnection, _1, padItem );

        if (padItem->Dirty() )
        {
        padItem->SetDirty ( false );

        m_padList.FindNearby( pad->ShapePos(), pad->GetBoundingRadius(), searchPads );
        m_trackList.FindNearby( pad->ShapePos(), pad->GetBoundingRadius(), searchPads );
        m_viaList.FindNearby( pad->ShapePos(), pad->GetBoundingRadius(), searchPads );
        }
    }

    for( auto& trackItem : m_trackList )
    {
        auto track = static_cast<TRACK*> ( trackItem->Parent() );
        int dist_max = track->GetWidth() / 2;
        auto searchTracks = std::bind( checkForConnection, _1, trackItem, dist_max );
#ifdef CONNECTIVITY_DEBUG
        printf("check item %p\n", trackItem);
#endif
        if (trackItem->Dirty())
        {
        trackItem->SetDirty( false );
#ifdef CONNECTIVITY_DEBUG
        printf("search start %p parent %p [%d %d] [%d %d]\n", trackItem, track, track->GetStart().x, track->GetStart().y, track->GetEnd().x, track->GetEnd().y );
#endif
        m_trackList.FindNearby( track->GetStart(), dist_max, searchTracks );
        m_trackList.FindNearby( track->GetEnd(), dist_max, searchTracks );
        }
    }

    for( auto& viaItem : m_viaList )
    {
        auto via = static_cast<VIA*> ( viaItem->Parent() );
        int dist_max = via->GetWidth() / 2;
        auto searchVias = std::bind( checkForConnection, _1, viaItem, dist_max );
        if (viaItem->Dirty())
        {

        viaItem->SetDirty ( false );
        m_viaList.FindNearby( via->GetStart(), dist_max, searchVias );
        m_trackList.FindNearby( via->GetStart(), dist_max, searchVias );
        }

    }

    if( aIncludeZones )
    {
        for( auto& item : m_zoneList )
        {
            auto zoneItem = static_cast<CN_ZONE *> (item);
            auto searchZones = std::bind( checkForConnection, _1, zoneItem );

            if(zoneItem->Dirty())
            {


            zoneItem->SetDirty ( false );
            m_viaList.FindNearby( zoneItem->BBox(), searchZones );
            m_trackList.FindNearby( zoneItem->BBox(), searchZones );
            m_padList.FindNearby( zoneItem->BBox(), searchZones );
            m_zoneList.FindNearby( zoneItem->BBox(),  std::bind( checkInterZoneConnection, _1, static_cast<CN_ZONE *> (zoneItem) ) );

            // fixme: use bounding boxes
            //m_zoneList.FindNearby( zoneItem->BBox(),  std::bind( checkInterZoneConnection, _1, static_cast<CN_ZONE *> (zoneItem) ) );
            }

        }
    }

#ifdef CONNECTIVITY_DEBUG
    printf("Search end\n");
#endif
    search_cnt.show();
}


void CN_CONNECTIVITY_ALGO_IMPL::searchClusters( bool aIncludeZones )
{

    std::deque<CN_ITEM*> Q;
    CN_ITEM* head = nullptr;
    m_clusters.clear();

    PROF_COUNTER cnt( "search-clusters" );
    cnt.start();

    auto addToSearchList = [&head] ( CN_ITEM *aItem )
    {
        if( !aItem->Valid() )
            return;

        aItem->ListClear();
        aItem->SetVisited( false );

        if ( !head )
            head = aItem;
        else
            head->ListInsert( aItem );
    };

    std::for_each( m_padList.begin(), m_padList.end(), addToSearchList );
    std::for_each( m_trackList.begin(), m_trackList.end(), addToSearchList );
    std::for_each( m_viaList.begin(), m_viaList.end(), addToSearchList );

    if (aIncludeZones)
    {
        std::for_each( m_zoneList.begin(), m_zoneList.end(), addToSearchList );
    }


    while( head )
    {
        CN_CLUSTER_PTR cluster ( new CN_CLUSTER() );

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
                if( !n->Visited() && n->Valid() )
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


    std::sort( m_clusters.begin(), m_clusters.end(), []( CN_CLUSTER_PTR a, CN_CLUSTER_PTR b ) {
        return a->OriginNet() < b->OriginNet();
    } );

#ifdef CONNECTIVITY_DEBUG

    printf("Active clusters: %d\n");
    for (auto cl:m_clusters)
    {
    printf("Net %d\n", cl->OriginNet());
        cl->Dump();
    }
#endif
}

void CN_CONNECTIVITY_ALGO_IMPL::SetBoard( BOARD* aBoard )
{
    for( int i = 0; i<aBoard->GetAreaCount(); i++ )
    {
        auto zone = aBoard->GetArea( i );
        Add( zone );
    }

    for( auto tv : aBoard->Tracks() )
        Add( tv );

    for( auto mod : aBoard->Modules() )
        for( auto pad : mod->PadsIter() )
            Add( pad );

    /*wxLogTrace( "CN", "zones : %lu, pads : %lu vias : %lu tracks : %lu\n",
            m_zoneList.Size(), m_padList.Size(),
            m_viaList.Size(), m_trackList.Size() );*/
}

void CN_CONNECTIVITY_ALGO_IMPL::propagateConnections()
{
    for( auto cluster : m_clusters )
    {
        if( cluster->IsConflicting() )
        {
            wxLogTrace( "CN", "Conflicting nets in cluster %p\n", cluster.get() );
        }
        else if( cluster->IsOrphaned() )
        {
            wxLogTrace( "CN", "Skipping orphaned cluster %p [net: %s]\n", cluster.get(),
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
                wxLogTrace( "CN", "Cluster %p : net : %d %s\n", cluster.get(),
                        cluster->OriginNet(), (const char*) cluster->OriginNetName() );
            else
                wxLogTrace( "CN", "Cluster %p : nothing to propagate\n", cluster.get() );
        }
        else
        {
            wxLogTrace( "CN", "Cluster %p : connected to unused net\n", cluster.get() );
        }
    }
}


void CN_CONNECTIVITY_ALGO_IMPL::PropagateNets()
{
    searchConnections( false );
    searchClusters( false );
    propagateConnections();
}

void CN_CONNECTIVITY_ALGO_IMPL::FindIsolatedCopperIslands( ZONE_CONTAINER* aZone, std::vector<int>& aIslands )
{
    if ( aZone->GetFilledPolysList().IsEmpty() )
        return;

    aIslands.clear();

    Remove( aZone );
    Add( aZone );

    // m_zoneList->Remove ( aZone );
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

    wxLogTrace( "CN", "Found %llu isolated islands\n", aIslands.size() );
}

bool CN_CONNECTIVITY_ALGO_IMPL::CheckConnectivity( std::vector<CN_DISJOINT_NET_ENTRY>& aReport )
{
    bool rv = true;

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
        {
            wxLogTrace( "CN", "Net %u [%s] is not completely routed (%d disjoint clusters).\n", net,
                    (const char*) name, count );

            rv = false;
        }
    }

    return rv;
};

int CN_CONNECTIVITY_ALGO_IMPL::GetUnconnectedCount()
{
    int cnt = 0;

    std::set<int> hits;

    searchConnections( true );
    searchClusters( true );

    for( auto cluster : m_clusters )
    {
        auto netcode = cluster->OriginNet();
        if (netcode <= 0 )
            continue;
        //printf("Found cluster : %d\n", netcode);
        //for ( auto item : *cluster )
        //    printf(" - item %p valid %d net %d\n",item, !!item->Valid(), item->Parent()->GetNetCode());

        if ( hits.find(netcode ) == hits.end() )
            hits.insert( netcode );
        else {
        //    printf("(duplicate)\n");
            cnt++;
        }
    }

    printf("unconnected: %d\n", cnt);
    return cnt;
}

const CN_CONNECTIVITY_ALGO_IMPL::CLUSTERS& CN_CONNECTIVITY_ALGO_IMPL::GetClusters()
{
    return m_clusters;
}
