
/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2004-2007 Jean-Pierre Charras, jean-pierre.charras@inpg.fr
 * Copyright (C) 2007 Dick Hollenbeck, dick@softplc.com
 * Copyright (C) 2007 Kicad Developers, see change_log.txt for contributors.
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


/****************************/
/* DRC control				*/
/****************************/

#include "fctsys.h"
#include "common.h"
#include "class_drawpanel.h"
#include "pcbnew.h"
#include "wxPcbStruct.h"
#include "autorout.h"
#include "trigo.h"
#include "gestfich.h"
#include "class_board_design_settings.h"

#include "protos.h"

#include "drc_stuff.h"
#include "dialog_drc.h"


void DRC::ShowDialog()
{
    if( !m_ui )
    {
        m_ui = new DIALOG_DRC_CONTROL( this, m_mainWindow );
        updatePointers();

        // copy data retained in this DRC object into the m_ui DrcPanel:

        PutValueInLocalUnits( *m_ui->m_SetTrackMinWidthCtrl,
                              m_pcb->GetBoardDesignSettings()->m_TrackMinWidth,
                              m_mainWindow->m_InternalUnits );;
        PutValueInLocalUnits( *m_ui->m_SetViaMinSizeCtrl,
                              m_pcb->GetBoardDesignSettings()->m_ViasMinSize,
                              m_mainWindow->m_InternalUnits );;
        PutValueInLocalUnits( *m_ui->m_SetMicroViakMinSizeCtrl,
                              m_pcb->GetBoardDesignSettings()->m_MicroViasMinSize,
                              m_mainWindow->m_InternalUnits );;

        m_ui->m_CreateRptCtrl->SetValue( m_doCreateRptFile );
        m_ui->m_RptFilenameCtrl->SetValue( m_rptFilename );
    }
    else
        updatePointers();

    m_ui->Show( true );
}


void DRC::DestroyDialog( int aReason )
{
    if( m_ui )
    {
        if( aReason == wxID_OK )
        {
            // if user clicked OK, save his choices in this DRC object.
            m_doCreateRptFile = m_ui->m_CreateRptCtrl->GetValue();
            m_rptFilename     = m_ui->m_RptFilenameCtrl->GetValue();
        }

        m_ui->Destroy();
        m_ui = 0;
    }
}


DRC::DRC( WinEDA_PcbFrame* aPcbWindow )
{
    m_mainWindow = aPcbWindow;
    m_drawPanel  = aPcbWindow->DrawPanel;
    m_pcb = aPcbWindow->GetBoard();
    m_ui  = 0;

    // establish initial values for everything:
    m_doPad2PadTest     = true;     // enable pad to pad clearance tests
    m_doUnconnectedTest = true;     // enable unconnected tests
    m_doZonesTest = true;           // enable zone to items clearance tests

    m_doCreateRptFile = false;

    // m_rptFilename set to empty by its constructor

    m_currentMarker = NULL;

    m_segmAngle  = 0;
    m_segmLength = 0;

    m_xcliplo = 0;
    m_ycliplo = 0;
    m_xcliphi = 0;
    m_ycliphi = 0;

    m_drawPanel = 0;
}


DRC::~DRC()
{
    // maybe someday look at pointainer.h  <- google for "pointainer.h"
    for( unsigned i = 0; i<m_unconnected.size();  ++i )
        delete m_unconnected[i];
}


/*********************************************/
int DRC::Drc( TRACK* aRefSegm, TRACK* aList )
/*********************************************/
{
    updatePointers();

    if( !doTrackDrc( aRefSegm, aList, true ) )
    {
        wxASSERT( m_currentMarker );

        m_currentMarker->DisplayInfo( m_mainWindow );
        return BAD_DRC;
    }

    return OK_DRC;
}


/**************************************************************/
int DRC::Drc( ZONE_CONTAINER* aArea, int CornerIndex )
/*************************************************************/

/**
 * Function Drc
 * tests the outline segment starting at CornerIndex and returns the result and displays the error
 * in the status panel only if one exists.
 *      Test Edge inside other areas
 *      Test Edge too close other areas
 * @param aEdge The areaparent which contains the corner.
 * @param CornerIndex The starting point of the segment to test.
 * @return int - BAD_DRC (1) if DRC error  or OK_DRC (0) if OK
 */
{
    updatePointers();

    if( !doEdgeZoneDrc( aArea, CornerIndex ) )
    {
        wxASSERT( m_currentMarker );
        m_currentMarker->DisplayInfo( m_mainWindow );
        return BAD_DRC;
    }

    return OK_DRC;
}


/**
 * Function RunTests
 * will actually run all the tests specified with a previous call to
 * SetSettings()
 */
void DRC::RunTests( wxTextCtrl* aMessages )
{
    // Ensure ratsnest is up to date:
    if( (m_pcb->m_Status_Pcb & LISTE_RATSNEST_ITEM_OK) == 0 )
    {
        if( aMessages )
        {
            aMessages->AppendText( _( "Compile ratsnest...\n" ) );
            wxSafeYield();
        }

        m_mainWindow->Compile_Ratsnest( NULL, true );
    }

    // someone should have cleared the two lists before calling this.

    if( !testNetClasses() )
    {
        // testing the netclasses is a special case because if the netclasses
        // do not pass the g_DesignSettings checks, then every member of a net
        // class (a NET) will cause its items such as tracks, vias, and pads
        // to also fail.  So quit after *all* netclass errors have been reported.
        if( aMessages )
            aMessages->AppendText( _( "Aborting\n" ) );

        // update the m_ui listboxes
        updatePointers();

        return;
    }

    // test pad to pad clearances, nothing to do with tracks, vias or zones.
    if( m_doPad2PadTest )
    {
        if( aMessages )
        {
            aMessages->AppendText( _( "Pad clearances...\n" ) );
            wxSafeYield();
        }

        testPad2Pad();
    }

    // test track and via clearances to other tracks, pads, and vias
    if( aMessages )
    {
        aMessages->AppendText( _( "Track clearances...\n" ) );
        wxSafeYield();
    }

    testTracks();

    // Before testing segments and unconnected, refill all zones:
    // this is a good caution, because filled areas can be outdated.
    if( aMessages )
    {
        aMessages->AppendText( _( "Fill zones...\n" ) );
        wxSafeYield();
    }
    m_mainWindow->Fill_All_Zones( false );

    // test zone clearances to other zones, pads, tracks, and vias
    if( aMessages && m_doZonesTest )
    {
        aMessages->AppendText( _( "Test zones...\n" ) );
        wxSafeYield();
    }

    testZones( m_doZonesTest );

    // find and gather unconnected pads.
    if( m_doUnconnectedTest )
    {
        if( aMessages )
        {
            aMessages->AppendText( _( "Unconnected pads...\n" ) );
            wxSafeYield();
        }

        testUnconnected();
    }

    // update the m_ui listboxes
    updatePointers();

    if( aMessages )
    {
        // no newline on this one because it is last, don't want the window
        // to unnecessarily scroll.
        aMessages->AppendText( _( "Finished" ) );
    }
}


/***************************************************************/
void DRC::ListUnconnectedPads()
/***************************************************************/
{
    testUnconnected();

    // update the m_ui listboxes
    updatePointers();
}


void DRC::updatePointers()
{
    // update my pointers, m_mainWindow is the only unchangable one
    m_drawPanel = m_mainWindow->DrawPanel;
    m_pcb = m_mainWindow->GetBoard();

    if( m_ui )  // Use diag list boxes only in DRC dialog
    {
        m_ui->m_ClearanceListBox->SetList( new DRC_LIST_MARKERS( m_pcb ) );

        m_ui->m_UnconnectedListBox->SetList( new DRC_LIST_UNCONNECTED( &m_unconnected ) );
    }
}


bool DRC::doNetClass( NETCLASS* nc, wxString& msg )
{
    bool ret = true;

    const BOARD_DESIGN_SETTINGS& g = *m_pcb->GetBoardDesignSettings();

#define FmtVal( x ) GetChars( ReturnStringFromValue( g_UserUnit, x, PCB_INTERNAL_UNIT ) )

#if 0   // set to 1 when (if...) BOARD_DESIGN_SETTINGS has a m_MinClearance value
    if( nc->GetClearance() < g.m_MinClearance )
    {
        msg.Printf( _( "NETCLASS: '%s' has Clearance:%s which is less than global:%s" ),
                    GetChars( nc->GetName() ),
                    FmtVal( nc->GetClearance() ),
                    FmtVal( g.m_TrackClearance )
                    );

        m_currentMarker = fillMarker( DRCE_NETCLASS_CLEARANCE, msg, m_currentMarker );
        m_pcb->Add( m_currentMarker );
        m_currentMarker = 0;
        ret = false;
    }
#endif

    if( nc->GetTrackWidth() < g.m_TrackMinWidth )
    {
        msg.Printf( _( "NETCLASS: '%s' has TrackWidth:%s which is less than global:%s" ),
                    GetChars( nc->GetName() ),
                    FmtVal( nc->GetTrackWidth() ),
                    FmtVal( g.m_TrackMinWidth )
                    );

        m_currentMarker = fillMarker( DRCE_NETCLASS_TRACKWIDTH, msg, m_currentMarker );
        m_pcb->Add( m_currentMarker );
        m_currentMarker = 0;
        ret = false;
    }

    if( nc->GetViaDiameter() < g.m_ViasMinSize )
    {
        msg.Printf( _( "NETCLASS: '%s' has Via Dia:%s which is less than global:%s" ),
                    GetChars( nc->GetName() ),
                    FmtVal( nc->GetViaDiameter() ),
                    FmtVal( g.m_ViasMinSize )
                    );

        m_currentMarker = fillMarker( DRCE_NETCLASS_VIASIZE, msg, m_currentMarker );
        m_pcb->Add( m_currentMarker );
        m_currentMarker = 0;
        ret = false;
    }

    if( nc->GetViaDrill() < g.m_ViasMinDrill )
    {
        msg.Printf( _( "NETCLASS: '%s' has Via Drill:%s which is less than global:%s" ),
                    GetChars( nc->GetName() ),
                    FmtVal( nc->GetViaDrill() ),
                    FmtVal( g.m_ViasMinDrill )
                    );

        m_currentMarker = fillMarker( DRCE_NETCLASS_VIADRILLSIZE, msg, m_currentMarker );
        m_pcb->Add( m_currentMarker );
        m_currentMarker = 0;
        ret = false;
    }

    if( nc->GetuViaDiameter() < g.m_MicroViasMinSize )
    {
        msg.Printf( _( "NETCLASS: '%s' has uVia Dia:%s which is less than global:%s" ),
                    GetChars( nc->GetName() ),
                    FmtVal( nc->GetuViaDiameter() ),
                    FmtVal( g.m_MicroViasMinSize )
                    );

        m_currentMarker = fillMarker( DRCE_NETCLASS_uVIASIZE, msg, m_currentMarker );
        m_pcb->Add( m_currentMarker );
        m_currentMarker = 0;
        ret = false;
    }

    if( nc->GetuViaDrill() < g.m_MicroViasMinDrill )
    {
        msg.Printf( _( "NETCLASS: '%s' has uVia Drill:%s which is less than global:%s" ),
                    GetChars( nc->GetName() ),
                    FmtVal( nc->GetuViaDrill() ),
                    FmtVal( g.m_MicroViasMinDrill )
                    );

        m_currentMarker = fillMarker( DRCE_NETCLASS_uVIADRILLSIZE, msg, m_currentMarker );
        m_pcb->Add( m_currentMarker );
        m_currentMarker = 0;
        ret = false;
    }

    return ret;
}


bool DRC::testNetClasses()
{
    bool        ret = true;

    NETCLASSES& netclasses = m_pcb->m_NetClasses;

    wxString    msg;   // construct this only once here, not in a loop, since somewhat expensive.

    if( !doNetClass( netclasses.GetDefault(), msg ) )
        ret = false;

    for( NETCLASSES::const_iterator i = netclasses.begin();  i != netclasses.end();  ++i )
    {
        NETCLASS* nc = i->second;

        if( !doNetClass( nc, msg ) )
            ret = false;
    }

    return ret;
}


void DRC::testTracks()
{
    for( TRACK* segm = m_pcb->m_Track;  segm && segm->Next();  segm = segm->Next() )
    {
        if( !doTrackDrc( segm, segm->Next(), true ) )
        {
            wxASSERT( m_currentMarker );
            m_pcb->Add( m_currentMarker );
            m_currentMarker = 0;
        }
    }
}


/***********************/
void DRC::testPad2Pad()
/***********************/
{
    std::vector<D_PAD*> sortedPads;

    CreateSortedPadListByXCoord( m_pcb, &sortedPads );

    // find the max size of the pads (used to stop the test)
    int max_size = 0;

    for( unsigned i = 0;  i<sortedPads.size();  ++i )
    {
        D_PAD* pad = sortedPads[i];

        if( pad->m_Rayon > max_size )       // m_Rayon is the radius value of the circle containing the pad
            max_size = pad->m_Rayon;
    }

    // Test the pads
    D_PAD** listEnd = &sortedPads[ sortedPads.size() ];

    for( unsigned i = 0;  i<sortedPads.size();  ++i )
    {
        D_PAD* pad = sortedPads[i];

        int    x_limit = max_size + pad->GetClearance() +
                         pad->m_Rayon + pad->GetPosition().x;

        if( !doPadToPadsDrc( pad, &sortedPads[i], listEnd, x_limit ) )
        {
            wxASSERT( m_currentMarker );
            m_pcb->Add( m_currentMarker );
            m_currentMarker = 0;
        }
    }
}


void DRC::testUnconnected()
{
    if( (m_pcb->m_Status_Pcb & LISTE_RATSNEST_ITEM_OK) == 0 )
    {
        wxClientDC dc( m_mainWindow->DrawPanel );
        m_mainWindow->Compile_Ratsnest( &dc, TRUE );
    }

    if( m_pcb->GetRatsnestsCount() == 0 )
        return;

    for( unsigned ii = 0; ii < m_pcb->GetRatsnestsCount();  ++ii )
    {
        RATSNEST_ITEM* rat = &m_pcb->m_FullRatsnest[ii];
        if( (rat->m_Status & CH_ACTIF) == 0 )
            continue;

        D_PAD*    padStart = rat->m_PadStart;
        D_PAD*    padEnd   = rat->m_PadEnd;

        DRC_ITEM* uncItem = new DRC_ITEM( DRCE_UNCONNECTED_PADS,
                                         padStart->MenuText( m_pcb ), padEnd->MenuText( m_pcb ),
                                         padStart->GetPosition(), padEnd->GetPosition() );

        m_unconnected.push_back( uncItem );
    }
}


/**********************************************/
void DRC::testZones( bool adoTestFillSegments )
/**********************************************/
{
    // Test copper areas for valide netcodes
    // if a netcode is < 0 the netname was not found when reading a netlist
    // if a netcode is == 0 the netname is void, and the zone is not connected.
    // This is allowed, but i am not sure this is a good idea
    for( int ii = 0; ii < m_pcb->GetAreaCount(); ii++ )
    {
        ZONE_CONTAINER* Area_To_Test = m_pcb->GetArea( ii );
        if( !Area_To_Test->IsOnCopperLayer() )
            continue;
        if( Area_To_Test->GetNet() < 0 )
        {
            m_currentMarker = fillMarker( Area_To_Test,
                                          DRCE_NON_EXISTANT_NET_FOR_ZONE_OUTLINE, m_currentMarker );
            m_pcb->Add( m_currentMarker );
            m_currentMarker = 0;
        }
    }

    // Test copper areas outlines, and create markers when needed
    m_pcb->Test_Drc_Areas_Outlines_To_Areas_Outlines( NULL, true );

    TRACK* zoneSeg;

    if( !adoTestFillSegments )
        return;

    // m_pcb->m_Zone is fully obsolete. Keep this test for compatibility
    // with old designs. Will be removed on day
    for( zoneSeg = m_pcb->m_Zone;  zoneSeg && zoneSeg->Next(); zoneSeg = zoneSeg->Next() )
    {
        // Test zoneSeg with other zone segments and with all pads
        if( !doTrackDrc( zoneSeg, zoneSeg->Next(), true ) )
        {
            wxASSERT( m_currentMarker );
            m_pcb->Add( m_currentMarker );
            m_currentMarker = 0;
        }

        // Pads already tested: disable pad test

        bool rc = doTrackDrc( zoneSeg, m_pcb->m_Track, false );
        if( !rc )
        {
            wxASSERT( m_currentMarker );
            m_pcb->Add( m_currentMarker );
            m_currentMarker = 0;
        }
    }
}


MARKER_PCB* DRC::fillMarker( TRACK* aTrack, BOARD_ITEM* aItem, int aErrorCode, MARKER_PCB* fillMe )
{
    wxString textA = aTrack->MenuText( m_pcb );
    wxString textB;

    wxPoint  position;
    wxPoint  posB;

    if( aItem )     // aItem might be NULL
    {
        textB = aItem->MenuText( m_pcb );
        posB  = aItem->GetPosition();

        if( aItem->Type() == TYPE_PAD )
            position = aItem->GetPosition();

        else if( aItem->Type() == TYPE_VIA )
            position = aItem->GetPosition();

        else if( aItem->Type() == TYPE_TRACK )
        {
            TRACK*  track  = (TRACK*) aItem;
            wxPoint endPos = track->m_End;

            // either of aItem's start or end will be used for the marker position
            // first assume start, then switch at end if needed.  decision made on
            // distance from end of aTrack.
            position = track->m_Start;

            double dToEnd = hypot( endPos.x - aTrack->m_End.x,
                                   endPos.y - aTrack->m_End.y );
            double dToStart = hypot( position.x - aTrack->m_End.x,
                                     position.y - aTrack->m_End.y );

            if( dToEnd < dToStart )
                position = endPos;
        }
    }
    else
        position = aTrack->GetPosition();

    if( fillMe )
    {
        if( aItem )
            fillMe->SetData( aErrorCode, position,
                             textA, aTrack->GetPosition(),
                             textB, posB );
        else
            fillMe->SetData( aErrorCode, position,
                            textA, aTrack->GetPosition() );
    }
    else
    {
        if( aItem )
            fillMe = new MARKER_PCB( aErrorCode, position,
                                     textA, aTrack->GetPosition(),
                                     textB, posB );
        else
            fillMe = new MARKER_PCB( aErrorCode, position,
                                    textA, aTrack->GetPosition() );
    }

    return fillMe;
}


MARKER_PCB* DRC::fillMarker( D_PAD* aPad, D_PAD* bPad, int aErrorCode, MARKER_PCB* fillMe )
{
    wxString textA = aPad->MenuText( m_pcb );
    wxString textB = bPad->MenuText( m_pcb );

    wxPoint  posA = aPad->GetPosition();
    wxPoint  posB = bPad->GetPosition();

    if( fillMe )
        fillMe->SetData( aErrorCode, posA, textA, posA, textB, posB );
    else
        fillMe = new MARKER_PCB( aErrorCode, posA, textA, posA, textB, posB );

    return fillMe;
}


MARKER_PCB* DRC::fillMarker( ZONE_CONTAINER* aArea, int aErrorCode, MARKER_PCB* fillMe )
{
    wxString textA = aArea->MenuText( m_pcb );

    wxPoint  posA = aArea->GetPosition();

    if( fillMe )
        fillMe->SetData( aErrorCode, posA, textA, posA );
    else
        fillMe = new MARKER_PCB( aErrorCode, posA, textA, posA );

    return fillMe;
}


MARKER_PCB* DRC::fillMarker( const ZONE_CONTAINER* aArea,
                             const wxPoint&        aPos,
                             int                   aErrorCode,
                             MARKER_PCB*           fillMe )
{
    wxString textA = aArea->MenuText( m_pcb );

    wxPoint  posA = aPos;

    if( fillMe )
        fillMe->SetData( aErrorCode, posA, textA, posA );
    else
        fillMe = new MARKER_PCB( aErrorCode, posA, textA, posA );

    return fillMe;
}


MARKER_PCB* DRC::fillMarker( int aErrorCode, const wxString& aMessage, MARKER_PCB* fillMe )
{
    wxPoint posA;   // not displayed

    if( fillMe )
        fillMe->SetData( aErrorCode, posA, aMessage, posA );
    else
        fillMe = new MARKER_PCB( aErrorCode, posA, aMessage, posA );

    fillMe->SetShowNoCoordinate();

    return fillMe;
}


/***********************************************************************/
bool DRC::doTrackDrc( TRACK* aRefSeg, TRACK* aStart, bool testPads )
/***********************************************************************/
{
    TRACK*    track;
    int       dx, dy;           // utilise pour calcul des dim x et dim y des segments
    int       layerMask;
    int       net_code_ref;
    wxPoint   shape_pos;

    NETCLASS* netclass = aRefSeg->GetNetClass();

    /* In order to make some calculations more easier or faster,
     * pads and tracks coordinates will be made relative to the reference segment origin
     */
    wxPoint origin = aRefSeg->m_Start;  // origin will be the origin of other coordinates

    m_segmEnd.x = dx = aRefSeg->m_End.x - origin.x;
    m_segmEnd.y = dy = aRefSeg->m_End.y - origin.y;

    layerMask    = aRefSeg->ReturnMaskLayer();
    net_code_ref = aRefSeg->GetNet();

    m_segmAngle = 0;

    // Phase 0 : Test vias
    if( aRefSeg->Type() == TYPE_VIA )
    {
        // test if the via size is smaller than minimum
        if( aRefSeg->Shape() == VIA_MICROVIA )
        {
            if( aRefSeg->m_Width < netclass->GetuViaMinDiameter() )
            {
                m_currentMarker = fillMarker( aRefSeg, NULL,
                                              DRCE_TOO_SMALL_MICROVIA, m_currentMarker );
                return false;
            }
        }
        else
        {
            if( aRefSeg->m_Width < netclass->GetViaMinDiameter() )
            {
                m_currentMarker = fillMarker( aRefSeg, NULL,
                                              DRCE_TOO_SMALL_VIA, m_currentMarker );
                return false;
            }
        }

        // test if via's hole is bigger than its diameter
        // This test is necessary since the via hole size and width can be modified
        // and a default via hole can be bigger than some vias sizes
        if( aRefSeg->GetDrillValue() > aRefSeg->m_Width )
        {
            m_currentMarker = fillMarker( aRefSeg, NULL,
                                          DRCE_VIA_HOLE_BIGGER, m_currentMarker );
            return false;
        }

        // For microvias: test if they are blind vias and only between 2 layers
        // because they are used for very small drill size and are drill by laser
        // and **only** one layer can be drilled
        if( aRefSeg->Shape() == VIA_MICROVIA )
        {
            int  layer1, layer2;
            bool err = true;

            ( (SEGVIA*) aRefSeg )->ReturnLayerPair( &layer1, &layer2 );
            if( layer1> layer2 )
                EXCHG( layer1, layer2 );

            // test:
            if( layer1 == LAYER_N_BACK && layer2 == LAYER_N_2 )
                err = false;
            if( layer1 == (m_pcb->GetBoardDesignSettings()->GetCopperLayerCount() - 2 )
                && layer2 == LAYER_N_FRONT )
                err = false;
            if( err )
            {
                m_currentMarker = fillMarker( aRefSeg, NULL,
                                              DRCE_MICRO_VIA_INCORRECT_LAYER_PAIR, m_currentMarker );
                return false;
            }
        }
    }
    else    // This is a track segment
    {
        if( aRefSeg->m_Width < netclass->GetTrackMinWidth() )
        {
            m_currentMarker = fillMarker( aRefSeg, NULL,
                                          DRCE_TOO_SMALL_TRACK_WIDTH, m_currentMarker );
            return false;
        }
    }

    // for a non horizontal or vertical segment Compute the segment angle
    // in tenths of degrees and its length
    if( dx || dy )
    {
        // Compute the segment angle in 0,1 degrees
        m_segmAngle = ArcTangente( dy, dx );

        // Compute the segment length: we build an equivalent rotated segment,
        // this segment is horizontal, therefore dx = length
        RotatePoint( &dx, &dy, m_segmAngle );    // dx = length, dy = 0
    }

    m_segmLength = dx;

    /******************************************/
    /* Phase 1 : test DRC track to pads :     */
    /******************************************/

    D_PAD pseudo_pad( (MODULE*) NULL );     // construct this once outside following loop

    // Compute the min distance to pads
    int   refsegm_half_width = aRefSeg->m_Width >> 1;

    if( testPads )
    {
        for( unsigned ii = 0;  ii<m_pcb->GetPadsCount();  ++ii )
        {
            D_PAD* pad = m_pcb->m_NetInfo->GetPad( ii );

            /* No problem if pads are on an other layer,
             * But if a drill hole exists	(a pad on a single layer can have a hole!)
             * we must test the hole
             */
            if( (pad->m_Masque_Layer & layerMask ) == 0 )
            {
                /* We must test the pad hole. In order to use the function "checkClearanceSegmToPad",
                 * a pseudo pad is used, with a shape and a size like the hole
                 */
                if( pad->m_Drill.x == 0 )
                    continue;

                pseudo_pad.m_Size = pad->m_Drill;
                pseudo_pad.SetPosition( pad->GetPosition() );
                pseudo_pad.m_PadShape = pad->m_DrillShape;
                pseudo_pad.m_Orient   = pad->m_Orient;
                pseudo_pad.ComputeRayon();      // compute the radius

                m_padToTestPos.x = pseudo_pad.GetPosition().x - origin.x;
                m_padToTestPos.y = pseudo_pad.GetPosition().y - origin.y;

                if( !checkClearanceSegmToPad( &pseudo_pad, refsegm_half_width,
                                             netclass->GetClearance() ) )
                {
                    m_currentMarker = fillMarker( aRefSeg, pad,
                                                  DRCE_TRACK_NEAR_THROUGH_HOLE, m_currentMarker );
                    return false;
                }
                continue;
            }

            /* The pad must be in a net (i.e pt_pad->GetNet() != 0 )
             * but no problem if the pad netcode is the current netcode (same net)
             */
            if( pad->GetNet()                       // the pad must be connected
               && net_code_ref == pad->GetNet() )   // the pad net is the same as current net -> Ok
                continue;

            // DRC for the pad
            shape_pos = pad->ReturnShapePos();
            m_padToTestPos.x  = shape_pos.x - origin.x;
            m_padToTestPos.y  = shape_pos.y - origin.y;

            if( !checkClearanceSegmToPad( pad, refsegm_half_width, aRefSeg->GetClearance( pad ) ) )
            {
                m_currentMarker = fillMarker( aRefSeg, pad,
                                              DRCE_TRACK_NEAR_PAD, m_currentMarker );
                return false;
            }
        }
    }

    /***********************************************/
    /* Phase 2: test DRC with other track segments */
    /***********************************************/

    // At this point the reference segment is the X axis

    // Test the reference segment with other track segments
    for( track = aStart;  track;  track = track->Next() )
    {
        // coord des extremites du segment teste dans le repere modifie
        int x0;
        int y0;
        int xf;
        int yf;

        // No problem if segments have the same net code:
        if( net_code_ref == track->GetNet() )
            continue;

        // No problem if segment are on different layers :
        if( ( layerMask & track->ReturnMaskLayer() ) == 0 )
            continue;

        // the minimum distance = clearance plus half the reference track
        // width plus half the other track's width
        int w_dist = aRefSeg->GetClearance( track );
        w_dist += (aRefSeg->m_Width + track->m_Width) / 2;

        // If the reference segment is a via, we test it here
        if( aRefSeg->Type() == TYPE_VIA )
        {
            int angle = 0;  // angle du segment a tester;

            dx = track->m_End.x - track->m_Start.x;
            dy = track->m_End.y - track->m_Start.y;

            x0 = aRefSeg->m_Start.x - track->m_Start.x;
            y0 = aRefSeg->m_Start.y - track->m_Start.y;

            if( track->Type() == TYPE_VIA )
            {
                // Test distance between two vias, i.e. two circles, trivial case
                if( (int) hypot( x0, y0 ) < w_dist )
                {
                    m_currentMarker = fillMarker( aRefSeg, track,
                                                  DRCE_VIA_NEAR_VIA, m_currentMarker );
                    return false;
                }
            }
            else    // test via to segment
            {
                // Compute l'angle
                angle = ArcTangente( dy, dx );

                // Compute new coordinates ( the segment become horizontal)
                RotatePoint( &dx, &dy, angle );
                RotatePoint( &x0, &y0, angle );

                if( !checkMarginToCircle( x0, y0, w_dist, dx ) )
                {
                    m_currentMarker = fillMarker( track, aRefSeg,
                                                  DRCE_VIA_NEAR_TRACK, m_currentMarker );
                    return false;
                }
            }
            continue;
        }

        /* We compute x0,y0, xf,yf = starting and ending point coordinates for
         * the segment to test in the new axis : the new X axis is the
         * reference segment.  We must translate and rotate the segment to test
         */
        x0 = track->m_Start.x - origin.x;
        y0 = track->m_Start.y - origin.y;

        xf = track->m_End.x - origin.x;
        yf = track->m_End.y - origin.y;

        RotatePoint( &x0, &y0, m_segmAngle );
        RotatePoint( &xf, &yf, m_segmAngle );

        if( track->Type() == TYPE_VIA )
        {
            if( checkMarginToCircle( x0, y0, w_dist, m_segmLength ) )
                continue;

            m_currentMarker = fillMarker( aRefSeg, track,
                                          DRCE_TRACK_NEAR_VIA, m_currentMarker );
            return false;
        }

        /*	We have changed axis:
         *  the reference segment is Horizontal.
         *  3 cases : the segment to test can be parallel, perpendicular or have an other direction
         */
        if( y0 == yf ) // parallel segments
        {
            if( abs( y0 ) >= w_dist )
                continue;

            if( x0 > xf )
                EXCHG( x0, xf );                                    /* pour que x0 <= xf */

            if( x0 > (-w_dist) && x0 < (m_segmLength + w_dist) )    /* possible error drc */
            {
                /* Fine test : we consider the rounded shape of the ends */
                if( x0 >= 0 && x0 <= m_segmLength )
                {
                    m_currentMarker = fillMarker( aRefSeg, track,
                                                  DRCE_TRACK_ENDS1, m_currentMarker );
                    return false;
                }
                if( !checkMarginToCircle( x0, y0, w_dist, m_segmLength ) )
                {
                    m_currentMarker = fillMarker( aRefSeg, track,
                                                  DRCE_TRACK_ENDS2, m_currentMarker );
                    return false;
                }
            }
            if( xf > (-w_dist) && xf < (m_segmLength + w_dist) )
            {
                /* Fine test : we consider the rounded shape of the ends */
                if( xf >= 0 && xf <= m_segmLength )
                {
                    m_currentMarker = fillMarker( aRefSeg, track,
                                                  DRCE_TRACK_ENDS3, m_currentMarker );
                    return false;
                }
                if( !checkMarginToCircle( xf, yf, w_dist, m_segmLength ) )
                {
                    m_currentMarker = fillMarker( aRefSeg, track,
                                                  DRCE_TRACK_ENDS4, m_currentMarker );
                    return false;
                }
            }

            if( x0 <=0 && xf >= 0 )
            {
                m_currentMarker = fillMarker( aRefSeg, track,
                                              DRCE_TRACK_UNKNOWN1, m_currentMarker );
                return false;
            }
        }
        else if( x0 == xf ) // perpendicular segments
        {
            if( ( x0 <= (-w_dist) ) || ( x0 >= (m_segmLength + w_dist) ) )
                continue;

            // Test if segments are crossing
            if( y0 > yf )
                EXCHG( y0, yf );
            if( (y0 < 0) && (yf > 0) )
            {
                m_currentMarker = fillMarker( aRefSeg, track,
                                              DRCE_TRACKS_CROSSING, m_currentMarker );
                return false;
            }

            // At this point the drc error is due to an end near a reference segm end
            if( !checkMarginToCircle( x0, y0, w_dist, m_segmLength ) )
            {
                m_currentMarker = fillMarker( aRefSeg, track,
                                              DRCE_ENDS_PROBLEM1, m_currentMarker );
                return false;
            }
            if( !checkMarginToCircle( xf, yf, w_dist, m_segmLength ) )
            {
                m_currentMarker = fillMarker( aRefSeg, track,
                                              DRCE_ENDS_PROBLEM2, m_currentMarker );
                return false;
            }
        }
        else    // segments quelconques entre eux
        {
            // calcul de la "surface de securite du segment de reference
            // First rought 'and fast) test : the track segment is like a rectangle

            m_xcliplo = m_ycliplo = -w_dist;
            m_xcliphi = m_segmLength + w_dist;
            m_ycliphi = w_dist;

            // A fine test is needed because a serment is not exactly a
            // rectangle, it has rounded ends
            if( !checkLine( x0, y0, xf, yf ) )
            {
                /* 2eme passe : the track has rounded ends.
                 * we must a fine test for each rounded end and the
                 * rectangular zone
                 */

                m_xcliplo = 0;
                m_xcliphi = m_segmLength;

                if( !checkLine( x0, y0, xf, yf ) )
                {
                    m_currentMarker = fillMarker( aRefSeg, track,
                                                  DRCE_ENDS_PROBLEM3, m_currentMarker );
                    return false;
                }
                else    // The drc error is due to the starting or the ending point of the reference segment
                {
                    // Test the starting and the ending point
                    int angle, rx0, ry0, rxf, ryf;
                    x0 = track->m_Start.x;
                    y0 = track->m_Start.y;

                    xf = track->m_End.x;
                    yf = track->m_End.y;

                    dx = xf - x0;
                    dy = yf - y0;

                    /* Compute the segment orientation (angle) en 0,1 degre */
                    angle = ArcTangente( dy, dx );

                    /* Compute the segment lenght: dx = longueur */
                    RotatePoint( &dx, &dy, angle );

                    /* Comute the reference segment coordinates relatives to a
                     *  X axis = current tested segment
                     */
                    rx0 = aRefSeg->m_Start.x - x0;
                    ry0 = aRefSeg->m_Start.y - y0;
                    rxf = aRefSeg->m_End.x - x0;
                    ryf = aRefSeg->m_End.y - y0;

                    RotatePoint( &rx0, &ry0, angle );
                    RotatePoint( &rxf, &ryf, angle );
                    if( !checkMarginToCircle( rx0, ry0, w_dist, dx ) )
                    {
                        m_currentMarker = fillMarker( aRefSeg, track,
                                                      DRCE_ENDS_PROBLEM4, m_currentMarker );
                        return false;
                    }
                    if( !checkMarginToCircle( rxf, ryf, w_dist, dx ) )
                    {
                        m_currentMarker = fillMarker( aRefSeg, track,
                                                      DRCE_ENDS_PROBLEM5, m_currentMarker );
                        return false;
                    }
                }
            }
        }
    }

    return true;
}


/*****************************************************************************/
bool DRC::doPadToPadsDrc( D_PAD* aRefPad, LISTE_PAD* aStart, LISTE_PAD* aEnd,
                          int x_limit )
/*****************************************************************************/
{
    int          layerMask = aRefPad->m_Masque_Layer & ALL_CU_LAYERS;

    static D_PAD dummypad( (MODULE*) NULL );       // used to test DRC pad to holes: this dummypad is the hole to test

    dummypad.m_Masque_Layer = ALL_CU_LAYERS;

    for(  LISTE_PAD* pad_list = aStart;  pad_list<aEnd;  ++pad_list )
    {
        D_PAD* pad = *pad_list;
        if( pad == aRefPad )
            continue;

        // We can stop the test when pad->m_Pos.x > x_limit
        // because the list is sorted by X values
        if( pad->m_Pos.x > x_limit )
            break;

        // No problem if pads are on different copper layers,
        // but their hole (if any ) can create RDC error because they are on all
        // copper layers, so we test them
        if( (pad->m_Masque_Layer & layerMask ) == 0 )
        {
            // if holes are in the same location and have the same size and shape,
            // this can be accepted
            if( pad->GetPosition() == aRefPad->GetPosition()
                && pad->m_Drill == aRefPad->m_Drill
                && pad->m_DrillShape == aRefPad->m_DrillShape )
            {
                if( aRefPad->m_DrillShape == PAD_CIRCLE )
                    continue;
                if( pad->m_Orient == aRefPad->m_Orient )    // for oval holes: must also have the same orientation
                    continue;
            }

            /* Here, we must test clearance between holes and pads
             * dummypad size and shape is adjusted to pad drill size and shape
             */
            if( pad->m_Drill.x ) // pad under testing has a hole, test this hole against pad reference
            {
                dummypad.SetPosition( pad->GetPosition() );
                dummypad.m_Size     = pad->m_Drill;
                dummypad.m_PadShape = pad->m_DrillShape == PAD_OVAL ? PAD_OVAL : PAD_CIRCLE;
                dummypad.m_Orient   = pad->m_Orient;
                if( !checkClearancePadToPad( aRefPad, &dummypad ) )
                {
                    // here we have a drc error on pad!
                    m_currentMarker = fillMarker( pad, aRefPad,
                                                  DRCE_HOLE_NEAR_PAD, m_currentMarker );
                    return false;
                }
            }

            if( aRefPad->m_Drill.x ) // pad reference has a hole
            {
                dummypad.SetPosition( aRefPad->GetPosition() );
                dummypad.m_Size     = aRefPad->m_Drill;
                dummypad.m_PadShape = aRefPad->m_DrillShape == PAD_OVAL ? PAD_OVAL : PAD_CIRCLE;
                dummypad.m_Orient   = aRefPad->m_Orient;
                if( !checkClearancePadToPad( pad, &dummypad ) )
                {
                    // here we have a drc erroron aRefPad!
                    m_currentMarker = fillMarker( aRefPad, pad,
                                                  DRCE_HOLE_NEAR_PAD, m_currentMarker );
                    return false;
                }
            }
            continue;
        }

        // The pad must be in a net (i.e pt_pad->GetNet() != 0 ),
        // But no problem if pads have the same netcode (same net)
        if( pad->GetNet() && ( aRefPad->GetNet() == pad->GetNet() ) )
            continue;

        // if pads are from the same footprint
        if( pad->GetParent() == aRefPad->GetParent() )
        {
            // and have the same pad number ( equivalent pads  )

            // one can argue that this 2nd test is not necessary, that any
            // two pads from a single module are acceptable.  This 2nd test
            // should eventually be a configuration option.
            if( pad->m_NumPadName == aRefPad->m_NumPadName )
                continue;
        }

        if( !checkClearancePadToPad( aRefPad, pad ) )
        {
            // here we have a drc error!
            m_currentMarker = fillMarker( aRefPad, pad,
                                          DRCE_PAD_NEAR_PAD1, m_currentMarker );
            return false;
        }
    }

    return true;
}


// Rotate a vector by an angle
wxPoint rotate( wxPoint p, int angle )
{
    wxPoint n;
    double  theta = M_PI * (double) angle / 1800.0;

    n.x = wxRound( (double) p.x * cos( theta ) - (double) p.y * sin( theta ) );
    n.y = wxRound( p.x * sin( theta ) + p.y * cos( theta ) );
    return n;
}


bool DRC::checkClearancePadToPad( D_PAD* aRefPad, D_PAD* aPad )
{
    wxPoint rel_pos;
    int     dist;;

    wxPoint shape_pos;
    int     pad_angle;

    int     dist_min = aRefPad->GetClearance( aPad );

    rel_pos   = aPad->ReturnShapePos();
    shape_pos = aRefPad->ReturnShapePos();

    // rel_pos is the aPad position relative to the aRefPad position
    rel_pos -= shape_pos;

    dist = (int) hypot( rel_pos.x, rel_pos.y );

    bool diag = true;

    // Quick test: Clearance is OK if the bounding circles are further away than "dist_min"
    if( (dist - aRefPad->m_Rayon - aPad->m_Rayon) >= dist_min )
        goto exit;

    /* Here, pads are near and DRC depend on the pad shapes
     *  We must compare distance using a fine shape analysis
     * Because a circle or oval shape is the easier shape to test, try to have
     * aRefPad shape type = PAD_CIRCLE or PAD_OVAL. Swap aRefPad and aPad if needed
     */
    bool swap_pads;
    swap_pads = false;
    if( (aRefPad->m_PadShape != PAD_CIRCLE) && (aPad->m_PadShape == PAD_CIRCLE) )
        swap_pads = true;
    else if( (aRefPad->m_PadShape != PAD_OVAL) && (aPad->m_PadShape == PAD_OVAL) )
        swap_pads = true;

    if( swap_pads )
    {
        EXCHG( aRefPad, aPad );
        rel_pos = -rel_pos;
    }

    /* Because pad exchange, aRefPad shape is PAD_CIRCLE or PAD_OVAL,
     * if one of the 2 pads was a PAD_CIRCLE or PAD_OVAL.
     * Therefore, if aRefPad is a PAD_RECT or a PAD_TRAPEZOID,
     * aPad is also a PAD_RECT or a PAD_TRAPEZOID
     */
    switch( aRefPad->m_PadShape )
    {
    case PAD_CIRCLE:        // aRefPad is like a track segment with a null lenght
        m_segmLength = 0;
        m_segmAngle  = 0;

        m_segmEnd.x = m_segmEnd.y = 0;

        m_padToTestPos.x = rel_pos.x;
        m_padToTestPos.y = rel_pos.y;

        diag = checkClearanceSegmToPad( aPad, aRefPad->m_Rayon, dist_min );
        break;

    case PAD_RECT:
        RotatePoint( &rel_pos, aRefPad->m_Orient );

        // pad_angle = pad orient relative to the aRefPad orient
        pad_angle = aRefPad->m_Orient + aPad->m_Orient;
        NORMALIZE_ANGLE_POS( pad_angle );
        if( aPad->m_PadShape == PAD_RECT )
        {
            wxSize size = aPad->m_Size;

            // The trivial case is if both rects are rotated by multiple of 90 deg
            // Most of time this is the case, and the test is fast
            if( ( (aRefPad->m_Orient == 0) || (aRefPad->m_Orient == 900)
                 || (aRefPad->m_Orient == 1800) || (aRefPad->m_Orient == 2700) )
               && ( (aPad->m_Orient == 0) || (aPad->m_Orient == 900) || (aPad->m_Orient == 1800)
                   || (aPad->m_Orient == 2700) ) )
            {
                if( (pad_angle == 900) || (pad_angle == 2700) )
                {
                    EXCHG( size.x, size.y );
                }

                // Test DRC:
                diag = false;

                rel_pos.x = ABS( rel_pos.x );
                rel_pos.y = ABS( rel_pos.y );

                if( ( rel_pos.x - ( (size.x + aRefPad->m_Size.x) / 2 ) ) >= dist_min )
                    diag = true;

                if( ( rel_pos.y - ( (size.y + aRefPad->m_Size.y) / 2 ) ) >= dist_min )
                    diag = true;
            }
            else        // al least on pad has any other orient. Test is more tricky
            {
                /* Use TestForIntersectionOfStraightLineSegments() for all 4 edges (segments).*/

                /* Test if one center point is contained in the other and thus the pads overlap.
                 * This case is not covered by the following check if one pad is
                 * completely contained in the other (because edges don't intersect)!
                 */
                if( ( (dist < aPad->m_Size.x) && (dist < aPad->m_Size.y) )
                   || ( (dist < aRefPad->m_Size.x) && (dist < aRefPad->m_Size.y) ) )
                {
                    diag = false;
                }

                // Vectors from center to corner
                wxPoint aPad_c2c    = wxPoint( aPad->m_Size.x / 2, aPad->m_Size.y / 2 );
                wxPoint aRefPad_c2c = wxPoint( aRefPad->m_Size.x / 2, aRefPad->m_Size.y / 2 );

                for( int i = 0; i<4; i++ )  // for all edges in aPad
                {
                    wxPoint p11 = aPad->ReturnShapePos() + rotate( aPad_c2c, aPad->m_Orient );

                    // flip the center-to-corner vector
                    if( i % 2 == 0 )
                    {
                        aPad_c2c.x = -aPad_c2c.x;
                    }
                    else
                    {
                        aPad_c2c.y = -aPad_c2c.y;
                    }
                    wxPoint p12 = aPad->ReturnShapePos() + rotate( aPad_c2c, aPad->m_Orient );

                    for( int j = 0; j<4; j++ ) // for all edges in aRefPad
                    {
                        wxPoint p21 = aRefPad->ReturnShapePos() + rotate( aRefPad_c2c,
                                                                          aRefPad->m_Orient );

                        // flip the center-to-corner vector
                        if( j % 2 == 0 )
                        {
                            aRefPad_c2c.x = -aRefPad_c2c.x;
                        }
                        else
                        {
                            aRefPad_c2c.y = -aRefPad_c2c.y;
                        }
                        wxPoint p22 = aRefPad->ReturnShapePos() + rotate( aRefPad_c2c,
                                                                          aRefPad->m_Orient );

                        int     x, y;
                        double  d;
                        int     intersect = TestForIntersectionOfStraightLineSegments( p11.x,
                                                                                       p11.y,
                                                                                       p12.x,
                                                                                       p12.y,
                                                                                       p21.x,
                                                                                       p21.y,
                                                                                       p22.x,
                                                                                       p22.y,
                                                                                       &x,
                                                                                       &y,
                                                                                       &d );
                        if( intersect || (d< dist_min) )
                        {
                            diag = false;
                        }
                    }
                }
            }
        }
        else
        {
            // TODO: Pad -> other shape! (PAD_TRAPEZOID)
        }
        break;

    case PAD_OVAL:     /* an oval pad is like a track segment */
    {
        /* Create a track segment with same dimensions as the oval aRefPad
         * and use checkClearanceSegmToPad function to test aPad to aRefPad clearance
        */
        int segm_width;
        m_segmAngle = aRefPad->m_Orient;              // Segment orient.
        if( aRefPad->m_Size.y < aRefPad->m_Size.x )   // Build an horizontal equiv segment
        {
            segm_width   = aRefPad->m_Size.y;
            m_segmLength = aRefPad->m_Size.x - aRefPad->m_Size.y;
        }
        else        // Vertical oval: build an horizontal equiv segment and rotate 90.0 deg
        {
            segm_width   = aRefPad->m_Size.x;
            m_segmLength = aRefPad->m_Size.y - aRefPad->m_Size.x;
            m_segmAngle += 900;
        }

        /* the start point must be 0,0 and currently rel_pos
         * is relative the center of pad coordinate */
        wxPoint segstart;
        segstart.x = -m_segmLength / 2;         // Start point coordinate of the horizontal equivalent segment

        RotatePoint( &segstart, m_segmAngle );       // True start point coordinate of the equivalent segment

        // move pad position relative to the segment origin
        m_padToTestPos = rel_pos - segstart;
        // Calculate segment end
        m_segmEnd.x = -2 * segstart.x;
        m_segmEnd.y = -2 * segstart.y;                              // end of segment coordinate
        diag   = checkClearanceSegmToPad( aPad, segm_width / 2, dist_min );
        break;
    }

    case PAD_TRAPEZOID:
    default:
        /* TODO...*/
        break;
    }

exit:       // the only way out (hopefully) for simpler debugging

    return diag;
}


bool DRC::checkClearanceSegmToPad( const D_PAD* pad_to_test, int w_segm, int aMinDist )
{
    wxSize padHalfsize;         // half the dimension of the pad
    int orient;
    int x0, y0, xf, yf;
    int seuil;
    int deltay;

    seuil  = w_segm + aMinDist;
    padHalfsize.x = pad_to_test->m_Size.x >> 1;
    padHalfsize.y = pad_to_test->m_Size.y >> 1;

    if( pad_to_test->m_PadShape == PAD_CIRCLE )
    {
        /* calcul des coord centre du pad dans le repere axe X confondu
         *  avec le segment en tst */
        RotatePoint( &m_padToTestPos.x, &m_padToTestPos.y, m_segmAngle );
        return checkMarginToCircle( m_padToTestPos.x, m_padToTestPos.y, seuil + padHalfsize.x, m_segmLength );
    }
    else
    {
        /* calcul de la "surface de securite" du pad de reference */
        m_xcliplo = m_padToTestPos.x - seuil - padHalfsize.x;
        m_ycliplo = m_padToTestPos.y - seuil - padHalfsize.y;
        m_xcliphi = m_padToTestPos.x + seuil + padHalfsize.x;
        m_ycliphi = m_padToTestPos.y + seuil + padHalfsize.y;

        x0 = y0 = 0;

        xf = m_segmEnd.x;
        yf = m_segmEnd.y;

        orient = pad_to_test->m_Orient;

        RotatePoint( &x0, &y0, m_padToTestPos.x, m_padToTestPos.y, -orient );
        RotatePoint( &xf, &yf, m_padToTestPos.x, m_padToTestPos.y, -orient );

        if( checkLine( x0, y0, xf, yf ) )
            return true;

        /* Erreur DRC : analyse fine de la forme de la pastille */

        switch( pad_to_test->m_PadShape )
        {
        default:
            return false;

        case PAD_OVAL:
            /* test de la pastille ovale ramenee au type ovale vertical */
            if( padHalfsize.x > padHalfsize.y )
            {
                EXCHG( padHalfsize.x, padHalfsize.y );
                orient += 900;
                if( orient >= 3600 )
                    orient -= 3600;
            }
            deltay = padHalfsize.y - padHalfsize.x;

            /* ici: padHalfsize.x = rayon,
             *      delta = dist centre cercles a centre pad */

            /* Test du rectangle separant les 2 demi cercles */
            m_xcliplo = m_padToTestPos.x - seuil - padHalfsize.x;
            m_ycliplo = m_padToTestPos.y - w_segm - deltay;
            m_xcliphi = m_padToTestPos.x + seuil + padHalfsize.x;
            m_ycliphi = m_padToTestPos.y + w_segm + deltay;

            if( !checkLine( x0, y0, xf, yf ) )
                return false;

            /* test des 2 cercles */
            x0 = m_padToTestPos.x;     /* x0,y0 = centre du cercle superieur du pad ovale */
            y0 = m_padToTestPos.y + deltay;
            RotatePoint( &x0, &y0, m_padToTestPos.x, m_padToTestPos.y, orient );
            RotatePoint( &x0, &y0, m_segmAngle );

            if( !checkMarginToCircle( x0, y0, padHalfsize.x + seuil, m_segmLength ) )
                return false;

            x0 = m_padToTestPos.x;     /* x0,y0 = centre du cercle inferieur du pad ovale */
            y0 = m_padToTestPos.y - deltay;
            RotatePoint( &x0, &y0, m_padToTestPos.x, m_padToTestPos.y, orient );
            RotatePoint( &x0, &y0, m_segmAngle );

            if( !checkMarginToCircle( x0, y0, padHalfsize.x + seuil, m_segmLength ) )
                return false;
            break;

        case PAD_RECT:      /* 2 rectangle + 4 1/4 cercles a tester */
            /* Test du rectangle dimx + seuil, dimy */
            m_xcliplo = m_padToTestPos.x - padHalfsize.x - seuil;
            m_ycliplo = m_padToTestPos.y - padHalfsize.y;
            m_xcliphi = m_padToTestPos.x + padHalfsize.x + seuil;
            m_ycliphi = m_padToTestPos.y + padHalfsize.y;

            if( !checkLine( x0, y0, xf, yf ) )
            {
                return false;
            }

            /* Test du rectangle dimx , dimy + seuil */
            m_xcliplo = m_padToTestPos.x - padHalfsize.x;
            m_ycliplo = m_padToTestPos.y - padHalfsize.y - seuil;
            m_xcliphi = m_padToTestPos.x + padHalfsize.x;
            m_ycliphi = m_padToTestPos.y + padHalfsize.y + seuil;

            if( !checkLine( x0, y0, xf, yf ) )
            {
                return false;
            }

            /* test des 4 cercles ( surface d'solation autour des sommets */
            /* test du coin sup. gauche du pad */
            x0 = m_padToTestPos.x - padHalfsize.x;
            y0 = m_padToTestPos.y - padHalfsize.y;
            RotatePoint( &x0, &y0, m_padToTestPos.x, m_padToTestPos.y, orient );
            RotatePoint( &x0, &y0, m_segmAngle );
            if( !checkMarginToCircle( x0, y0, seuil, m_segmLength ) )
            {
                return false;
            }

            /* test du coin sup. droit du pad */
            x0 = m_padToTestPos.x + padHalfsize.x;
            y0 = m_padToTestPos.y - padHalfsize.y;
            RotatePoint( &x0, &y0, m_padToTestPos.x, m_padToTestPos.y, orient );
            RotatePoint( &x0, &y0, m_segmAngle );
            if( !checkMarginToCircle( x0, y0, seuil, m_segmLength ) )
            {
                return false;
            }

            /* test du coin inf. gauche du pad */
            x0 = m_padToTestPos.x - padHalfsize.x;
            y0 = m_padToTestPos.y + padHalfsize.y;
            RotatePoint( &x0, &y0, m_padToTestPos.x, m_padToTestPos.y, orient );
            RotatePoint( &x0, &y0, m_segmAngle );
            if( !checkMarginToCircle( x0, y0, seuil, m_segmLength ) )
            {
                return false;
            }

            /* test du coin inf. droit du pad */
            x0 = m_padToTestPos.x + padHalfsize.x;
            y0 = m_padToTestPos.y + padHalfsize.y;
            RotatePoint( &x0, &y0, m_padToTestPos.x, m_padToTestPos.y, orient );
            RotatePoint( &x0, &y0, m_segmAngle );
            if( !checkMarginToCircle( x0, y0, seuil, m_segmLength ) )
            {
                return false;
            }

            break;

        case PAD_TRAPEZOID:      //TODO
            break;
        }
    }
    return true;
}


/**********************************************************************/
bool DRC::checkMarginToCircle( int cx, int cy, int radius, int longueur )
/**********************************************************************/
{
    if( abs( cy ) > radius )
        return true;

    if( (cx >= -radius ) && ( cx <= (longueur + radius) ) )
    {
        if( (cx >= 0) && (cx <= longueur) )
            return false;

        if( cx > longueur )
            cx -= longueur;

        if( hypot( cx, cy ) < radius )
            return false;
    }

    return true;
}


/**********************************************/
/* int Tst_Ligne(int x1,int y1,int x2,int y2) */
/**********************************************/

static inline int USCALE( unsigned arg, unsigned num, unsigned den )
{
    int ii;

    ii = (int) ( ( (double) arg * num ) / den );
    return ii;
}


#define WHEN_OUTSIDE return true
#define WHEN_INSIDE

bool DRC::checkLine( int x1, int y1, int x2, int y2 )
{
    int temp;

    if( x1 > x2 )
    {
        EXCHG( x1, x2 );
        EXCHG( y1, y2 );
    }
    if( (x2 < m_xcliplo) || (x1 > m_xcliphi) )
    {
        WHEN_OUTSIDE;
    }
    if( y1 < y2 )
    {
        if( (y2 < m_ycliplo) || (y1 > m_ycliphi) )
        {
            WHEN_OUTSIDE;
        }
        if( y1 < m_ycliplo )
        {
            temp = USCALE( (x2 - x1), (m_ycliplo - y1), (y2 - y1) );
            if( (x1 += temp) > m_xcliphi )
            {
                WHEN_OUTSIDE;
            }
            y1 = m_ycliplo;
            WHEN_INSIDE;
        }
        if( y2 > m_ycliphi )
        {
            temp = USCALE( (x2 - x1), (y2 - m_ycliphi), (y2 - y1) );
            if( (x2 -= temp) < m_xcliplo )
            {
                WHEN_OUTSIDE;
            }
            y2 = m_ycliphi;
            WHEN_INSIDE;
        }
        if( x1 < m_xcliplo )
        {
            temp = USCALE( (y2 - y1), (m_xcliplo - x1), (x2 - x1) );
            y1  += temp;
            x1   = m_xcliplo;
            WHEN_INSIDE;
        }
        if( x2 > m_xcliphi )
        {
            temp = USCALE( (y2 - y1), (x2 - m_xcliphi), (x2 - x1) );
            y2  -= temp;
            x2   = m_xcliphi;
            WHEN_INSIDE;
        }
    }
    else
    {
        if( (y1 < m_ycliplo) || (y2 > m_ycliphi) )
        {
            WHEN_OUTSIDE;
        }
        if( y1 > m_ycliphi )
        {
            temp = USCALE( (x2 - x1), (y1 - m_ycliphi), (y1 - y2) );
            if( (x1 += temp) > m_xcliphi )
            {
                WHEN_OUTSIDE;
            }
            y1 = m_ycliphi;
            WHEN_INSIDE;
        }
        if( y2 < m_ycliplo )
        {
            temp = USCALE( (x2 - x1), (m_ycliplo - y2), (y1 - y2) );
            if( (x2 -= temp) < m_xcliplo )
            {
                WHEN_OUTSIDE;
            }
            y2 = m_ycliplo;
            WHEN_INSIDE;
        }
        if( x1 < m_xcliplo )
        {
            temp = USCALE( (y1 - y2), (m_xcliplo - x1), (x2 - x1) );
            y1  -= temp;
            x1   = m_xcliplo;
            WHEN_INSIDE;
        }
        if( x2 > m_xcliphi )
        {
            temp = USCALE( (y1 - y2), (x2 - m_xcliphi), (x2 - x1) );
            y2  += temp;
            x2   = m_xcliphi;
            WHEN_INSIDE;
        }
    }

    if( ( (x2 + x1) / 2 <= m_xcliphi ) && ( (x2 + x1) / 2 >= m_xcliplo ) \
       && ( (y2 + y1) / 2 <= m_ycliphi ) && ( (y2 + y1) / 2 >= m_ycliplo ) )
    {
        return false;
    }
    else
        return true;
}
