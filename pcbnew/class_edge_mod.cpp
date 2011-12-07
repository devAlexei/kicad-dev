/**
 * @file class_edge_mod.cpp
 * @brief EDGE_MODULE class definition.
 */

#include "fctsys.h"
#include "gr_basic.h"
#include "wxstruct.h"
#include "trigo.h"
#include "class_drawpanel.h"
#include "confirm.h"
#include "kicad_string.h"
#include "colors_selection.h"
#include "richio.h"
#include "macros.h"
#include "wxBasePcbFrame.h"
#include "pcbcommon.h"

#include "class_board.h"
#include "class_module.h"
#include "class_edge_mod.h"


/*********************/
/* class EDGE_MODULE */
/*********************/

EDGE_MODULE::EDGE_MODULE( MODULE* parent, STROKE_T aShape ) :
    DRAWSEGMENT( parent, PCB_MODULE_EDGE_T )
{
    m_Shape = aShape;
    m_Angle = 0;
    m_Width = 120;
}


EDGE_MODULE::~EDGE_MODULE()
{
}


void EDGE_MODULE::Copy( EDGE_MODULE* source )
{
    if( source == NULL )
        return;

    DRAWSEGMENT::Copy( source );

    m_Start0 = source->m_Start0;
    m_End0   = source->m_End0;

    m_PolyPoints = source->m_PolyPoints;    // std::vector copy
}


void EDGE_MODULE::SetDrawCoord()
{
    MODULE* module = (MODULE*) m_Parent;

    m_Start = m_Start0;
    m_End   = m_End0;

    if( module )
    {
        RotatePoint( &m_Start.x, &m_Start.y, module->GetOrientation() );
        RotatePoint( &m_End.x,   &m_End.y,   module->GetOrientation() );

        m_Start += module->m_Pos;
        m_End   += module->m_Pos;
    }
}


/* Draw EDGE_MODULE:
 * Entry: offset = offset trace
 * Draw_mode mode = trace (GR_OR, GR_XOR, GR_AND)
 * The contours are of different types:
 * - Segment
 * - Circles
 * - Arcs
 */
void EDGE_MODULE::Draw( EDA_DRAW_PANEL* panel, wxDC* DC, int draw_mode, const wxPoint& offset )
{
    int             ux0, uy0, dx, dy, radius, StAngle, EndAngle;
    int             color, type_trace;
    int             typeaff;
    PCB_BASE_FRAME* frame;
    MODULE* module = (MODULE*) m_Parent;

    if( module == NULL )
        return;

    BOARD * brd = GetBoard( );

    if( brd->IsLayerVisible( m_Layer ) == false )
        return;

    color = brd->GetLayerColor( m_Layer );

    frame = (PCB_BASE_FRAME*) panel->GetParent();

    type_trace = m_Shape;

    ux0 = m_Start.x - offset.x;
    uy0 = m_Start.y - offset.y;

    dx = m_End.x - offset.x;
    dy = m_End.y - offset.y;

    GRSetDrawMode( DC, draw_mode );
    typeaff = frame->m_DisplayModEdge;

    if( m_Layer <= LAST_COPPER_LAYER )
    {
        typeaff = frame->m_DisplayPcbTrackFill;

        if( !typeaff )
            typeaff = SKETCH;
    }

    if( DC->LogicalToDeviceXRel( m_Width ) < MIN_DRAW_WIDTH )
        typeaff = FILAIRE;

    switch( type_trace )
    {
    case S_SEGMENT:
        if( typeaff == FILAIRE )
            GRLine( &panel->m_ClipBox, DC, ux0, uy0, dx, dy, 0, color );
        else if( typeaff == FILLED )
            GRLine( &panel->m_ClipBox, DC, ux0, uy0, dx, dy, m_Width, color );
        else
            // SKETCH Mode
            GRCSegm( &panel->m_ClipBox, DC, ux0, uy0, dx, dy, m_Width, color );

        break;

    case S_CIRCLE:
        radius = (int) hypot( (double) (dx - ux0), (double) (dy - uy0) );

        if( typeaff == FILAIRE )
        {
            GRCircle( &panel->m_ClipBox, DC, ux0, uy0, radius, color );
        }
        else
        {
            if( typeaff == FILLED )
            {
                GRCircle( &panel->m_ClipBox, DC, ux0, uy0, radius, m_Width, color );
            }
            else        // SKETCH Mode
            {
                GRCircle( &panel->m_ClipBox, DC, ux0, uy0, radius + (m_Width / 2), color );
                GRCircle( &panel->m_ClipBox, DC, ux0, uy0, radius - (m_Width / 2), color );
            }
        }

        break;

    case S_ARC:
        radius   = (int) hypot( (double) (dx - ux0), (double) (dy - uy0) );
        StAngle  = (int) ArcTangente( dy - uy0, dx - ux0 );
        EndAngle = StAngle + m_Angle;

        if( StAngle > EndAngle )
            EXCHG( StAngle, EndAngle );

        if( typeaff == FILAIRE )
        {
            GRArc( &panel->m_ClipBox, DC, ux0, uy0, StAngle, EndAngle, radius, color );
        }
        else if( typeaff == FILLED )
        {
            GRArc( &panel->m_ClipBox, DC, ux0, uy0, StAngle, EndAngle, radius, m_Width, color );
        }
        else        // SKETCH Mode
        {
            GRArc( &panel->m_ClipBox, DC, ux0, uy0, StAngle, EndAngle,
                   radius + (m_Width / 2), color );
            GRArc( &panel->m_ClipBox, DC, ux0, uy0, StAngle, EndAngle,
                   radius - (m_Width / 2), color );
        }
        break;

    case S_POLYGON:

        // We must compute true coordinates from m_PolyPoints
        // which are relative to module position, orientation 0

        std::vector<wxPoint> points = m_PolyPoints;

        for( unsigned ii = 0; ii < points.size(); ii++ )
        {
            wxPoint& pt = points[ii];

            RotatePoint( &pt.x, &pt.y, module->GetOrientation() );
            pt += module->m_Pos - offset;
        }

        GRPoly( &panel->m_ClipBox, DC, points.size(), &points[0], true, m_Width, color, color );
        break;
    }
}


// see class_edge_mod.h
void EDGE_MODULE::DisplayInfo( EDA_DRAW_FRAME* frame )
{
    wxString msg;

    MODULE*  module = (MODULE*) m_Parent;

    if( !module )
        return;

    BOARD* board = (BOARD*) module->GetParent();

    if( !board )
        return;

    frame->ClearMsgPanel();

    frame->AppendMsgPanel( _( "Graphic Item" ), wxEmptyString, DARKCYAN );
    frame->AppendMsgPanel( _( "Module" ), module->m_Reference->m_Text, DARKCYAN );
    frame->AppendMsgPanel( _( "Value" ), module->m_Value->m_Text, BLUE );

    msg.Printf( wxT( "%8.8lX" ), module->m_TimeStamp );
    frame->AppendMsgPanel( _( "TimeStamp" ), msg, BROWN );
    frame->AppendMsgPanel( _( "Mod Layer" ), board->GetLayerName( module->GetLayer() ), RED );
    frame->AppendMsgPanel( _( "Seg Layer" ), board->GetLayerName( GetLayer() ), RED );

    valeur_param( m_Width, msg );
    frame->AppendMsgPanel( _( "Width" ), msg, BLUE );
}



wxString EDGE_MODULE::GetSelectMenuText() const
{
    wxString text;

    text << _( "Graphic" ) << wxT( " " ) << ShowShape( (STROKE_T) m_Shape );
    text << wxT( " (" ) << GetLayerName() << wxT( ")" );
    text << _( " of " ) << ( (MODULE*) GetParent() )->GetReference();

    return text;
}


#if defined(DEBUG)

/**
 * Function Show
 * is used to output the object tree, currently for debugging only.
 * @param nestLevel An aid to prettier tree indenting, and is the level
 *          of nesting of this object within the overall tree.
 * @param os The ostream& to output to.
 */
void EDGE_MODULE::Show( int nestLevel, std::ostream& os )
{
    wxString shape = ShowShape( (STROKE_T) m_Shape );

    // for now, make it look like XML:
    NestedSpace( nestLevel, os ) << '<' << GetClass().Lower().mb_str() <<
    " type=\"" << TO_UTF8( shape ) << "\">";

    os << " <start" << m_Start0 << "/>";
    os << " <end" << m_End0 << "/>";

    os << " </" << GetClass().Lower().mb_str() << ">\n";
}


#endif
