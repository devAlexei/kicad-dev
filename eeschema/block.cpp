/****************************************************/
/*  BLOCK.CPP                                       */
/****************************************************/

#include "fctsys.h"
#include "appl_wxstruct.h"
#include "gr_basic.h"
#include "common.h"
#include "class_drawpanel.h"
#include "confirm.h"
#include "block_commande.h"
#include "wxEeschemaStruct.h"
#include "class_sch_screen.h"

#include "general.h"
#include "class_library.h"
#include "lib_pin.h"
#include "protos.h"
#include "sch_marker.h"
#include "sch_items.h"
#include "sch_text.h"
#include "sch_component.h"
#include "sch_sheet.h"

#include <boost/foreach.hpp>


// Imported functions:
void            MoveItemsInList( PICKED_ITEMS_LIST& aItemsList, const wxPoint aMoveVector );
void            RotateListOfItems( PICKED_ITEMS_LIST& aItemsList, wxPoint& Center );
void            Mirror_X_ListOfItems( PICKED_ITEMS_LIST& aItemsList, wxPoint& aMirrorPoint );
void            MirrorListOfItems( PICKED_ITEMS_LIST& aItemsList, wxPoint& Center );
void            DeleteItemsInList( WinEDA_DrawPanel*  panel, PICKED_ITEMS_LIST& aItemsList );
void            DuplicateItemsInList( SCH_SCREEN*        screen,
                                      PICKED_ITEMS_LIST& aItemsList,
                                      const wxPoint      aMoveVector  );

static void     CollectStructsToDrag( SCH_SCREEN* screen );
static void     AddPickedItem( SCH_SCREEN* screen, wxPoint aPosition );
static LIB_PIN* GetNextPinPosition( SCH_COMPONENT* aDrawLibItem,
                                    wxPoint&       aPosition,
                                    bool           aSearchFirst );
static void     DrawMovingBlockOutlines( WinEDA_DrawPanel* panel, wxDC* DC, bool erase );
static void     SaveStructListForPaste( PICKED_ITEMS_LIST& aItemsList );


/* Return the block command (BLOCK_MOVE, BLOCK_COPY...) corresponding to
 *  the key (ALT, SHIFT ALT ..)
 */
int SCH_EDIT_FRAME::ReturnBlockCommand( int key )
{
    int cmd;

    switch( key )
    {
    default:
        cmd = key & 0xFF;
        break;

    case 0:
        cmd = BLOCK_MOVE;
        break;

    case GR_KB_ALT:
    case GR_KB_SHIFT:
        cmd = BLOCK_COPY;
        break;

    case GR_KB_CTRL:
        cmd = BLOCK_DRAG;
        break;

    case GR_KB_SHIFTCTRL:
        cmd = BLOCK_DELETE;
        break;

    case MOUSE_MIDDLE:
        cmd = BLOCK_ZOOM;
        break;
    }

    return cmd;
}


/* Init the parameters used by the block paste command
 */
void SCH_EDIT_FRAME::InitBlockPasteInfos()
{
    BLOCK_SELECTOR* block = &GetScreen()->m_BlockLocate;

    block->m_ItemsSelection.CopyList( g_BlockSaveDataList.m_ItemsSelection );
    DrawPanel->ManageCurseur = DrawMovingBlockOutlines;
}


/* Routine to handle the BLOCK PLACE command
 *  Last routine for block operation for:
 *  - block move & drag
 *  - block copy & paste
 */
void SCH_EDIT_FRAME::HandleBlockPlace( wxDC* DC )
{
    bool            err   = false;
    BLOCK_SELECTOR* block = &GetScreen()->m_BlockLocate;

    if( DrawPanel->ManageCurseur == NULL )
    {
        err = true;
        DisplayError( this, wxT( "HandleBlockPLace() : ManageCurseur = NULL" ) );
    }

    if( block->GetCount() == 0 )
    {
        wxString msg;
        err = true;
        msg.Printf( wxT( "HandleBlockPLace() error : no items to place (cmd %d, state %d)" ),
                    block->m_Command, block->m_State );
        DisplayError( this, msg );
    }

    block->m_State = STATE_BLOCK_STOP;

    switch( block->m_Command )
    {
    case BLOCK_IDLE:
        err = true;
        break;

    case BLOCK_ROTATE:
    case BLOCK_MIRROR_X:
    case BLOCK_MIRROR_Y:
    case BLOCK_DRAG:        /* Drag */
    case BLOCK_MOVE:        /* Move */
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );

        SaveCopyInUndoList( block->m_ItemsSelection, UR_MOVED, block->m_MoveVector );
        MoveItemsInList( block->m_ItemsSelection, block->m_MoveVector );
        block->ClearItemsList();
        break;

    case BLOCK_COPY:                /* Copy */
    case BLOCK_PRESELECT_MOVE:      /* Move with preselection list*/
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );

        DuplicateItemsInList( GetScreen(), block->m_ItemsSelection, block->m_MoveVector );

        SaveCopyInUndoList( block->m_ItemsSelection,
                            ( block->m_Command == BLOCK_PRESELECT_MOVE ) ? UR_CHANGED : UR_NEW );

        block->ClearItemsList();
        break;

    case BLOCK_PASTE:
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );
        PasteListOfItems( DC );
        block->ClearItemsList();
        break;

    case BLOCK_ZOOM:        // Handled by HandleBlockEnd()
    case BLOCK_DELETE:
    case BLOCK_SAVE:
    case BLOCK_FLIP:
    case BLOCK_ABORT:
    case BLOCK_SELECT_ITEMS_ONLY:
        break;
    }

    OnModify();

    // clear struct.m_Flags.
    GetScreen()->ClearDrawingState();

    DrawPanel->ManageCurseur = NULL;
    DrawPanel->ForceCloseManageCurseur = NULL;
    block->m_Flags   = 0;
    block->m_State   = STATE_NO_BLOCK;
    block->m_Command = BLOCK_IDLE;
    GetScreen()->SetCurItem( NULL );

    TestDanglingEnds( GetScreen()->GetDrawItems(), DC );

    if( block->GetCount() )
    {
        DisplayError( this, wxT( "HandleBlockPLace() error: some items left in buffer" ) );
        block->ClearItemsList();
    }

    SetToolID( m_ID_current_state, DrawPanel->m_PanelDefaultCursor, wxEmptyString );
    DrawPanel->Refresh();
}


/**
 * Function HandleBlockEnd( )
 * Handle the "end"  of a block command,
 * i.e. is called at the end of the definition of the area of a block.
 * depending on the current block command, this command is executed
 * or parameters are initialized to prepare a call to HandleBlockPlace
 * in GetScreen()->m_BlockLocate
 * @return false if no item selected, or command finished,
 * true if some items found and HandleBlockPlace must be called later
 */
bool SCH_EDIT_FRAME::HandleBlockEnd( wxDC* DC )
{
    bool            nextcmd = false;
    bool            zoom_command = false;
    BLOCK_SELECTOR* block = &GetScreen()->m_BlockLocate;

    if( block->GetCount() )
    {
        BlockState   state   = block->m_State;
        CmdBlockType command = block->m_Command;
        if( DrawPanel->ForceCloseManageCurseur )
            DrawPanel->ForceCloseManageCurseur( DrawPanel, DC );
        block->m_State   = state;
        block->m_Command = command;
        DrawPanel->ManageCurseur = DrawAndSizingBlockOutlines;
        DrawPanel->ForceCloseManageCurseur = AbortBlockCurrentCommand;
        GetScreen()->m_Curseur = block->GetEnd();
        if( block->m_Command != BLOCK_ABORT )
            DrawPanel->MouseToCursorSchema();
    }

    if( DrawPanel->ManageCurseur != NULL )
        switch( block->m_Command )
        {
        case BLOCK_IDLE:
            DisplayError( this, wxT( "Error in HandleBlockPLace()" ) );
            break;

        case BLOCK_DRAG:    /* Drag */
            BreakSegmentOnJunction( (SCH_SCREEN*) GetScreen() );

        case BLOCK_ROTATE:
        case BLOCK_MIRROR_X:
        case BLOCK_MIRROR_Y:
        case BLOCK_MOVE:    /* Move */
        case BLOCK_COPY:    /* Copy */
            PickItemsInBlock( GetScreen()->m_BlockLocate, GetScreen() );

        case BLOCK_PRESELECT_MOVE: /* Move with preselection list*/
            if( block->GetCount() )
            {
                nextcmd = true;
                CollectStructsToDrag( GetScreen() );
                DrawPanel->ManageCurseur( DrawPanel, DC, false );
                DrawPanel->ManageCurseur = DrawMovingBlockOutlines;
                DrawPanel->ManageCurseur( DrawPanel, DC, false );
                block->m_State = STATE_BLOCK_MOVE;
            }
            else
            {
                DrawPanel->ManageCurseur( DrawPanel, DC, false );
                DrawPanel->ManageCurseur = NULL;
                DrawPanel->ForceCloseManageCurseur = NULL;
            }
            break;

        case BLOCK_DELETE: /* Delete */
            PickItemsInBlock( GetScreen()->m_BlockLocate, GetScreen() );
            DrawAndSizingBlockOutlines( DrawPanel, DC, false );
            if( block->GetCount() )
            {
                DeleteItemsInList( DrawPanel, block->m_ItemsSelection );
                OnModify();
            }
            block->ClearItemsList();
            TestDanglingEnds( GetScreen()->GetDrawItems(), DC );
            DrawPanel->Refresh();
            break;

        case BLOCK_SAVE:  /* Save */
            PickItemsInBlock( GetScreen()->m_BlockLocate, GetScreen() );
            DrawAndSizingBlockOutlines( DrawPanel, DC, false );
            if( block->GetCount() )
            {
                wxPoint move_vector = -GetScreen()->m_BlockLocate.m_BlockLastCursorPosition;
                SaveStructListForPaste( block->m_ItemsSelection );
                MoveItemsInList( g_BlockSaveDataList.m_ItemsSelection, move_vector );
             }
            block->ClearItemsList();
            break;

        case BLOCK_PASTE:
            block->m_State = STATE_BLOCK_MOVE;
            break;

        case BLOCK_FLIP: /* pcbnew only! */
            break;


        case BLOCK_ZOOM: /* Window Zoom */
            zoom_command = true;
            break;

        case BLOCK_SELECT_ITEMS_ONLY:   /* Not used */
        case BLOCK_ABORT:               /* not executed here */
            break;
        }

    if( block->m_Command  == BLOCK_ABORT )
    {
        GetScreen()->ClearDrawingState();
    }

    if( ! nextcmd )
    {
        block->m_Flags   = 0;
        block->m_State   = STATE_NO_BLOCK;
        block->m_Command = BLOCK_IDLE;
        DrawPanel->ManageCurseur = NULL;
        DrawPanel->ForceCloseManageCurseur = NULL;
        GetScreen()->SetCurItem( NULL );
        SetToolID( m_ID_current_state, DrawPanel->m_PanelDefaultCursor, wxEmptyString );
    }

    if( zoom_command )
        Window_Zoom( GetScreen()->m_BlockLocate );

    return nextcmd ;
}


/* Manage end block command from context menu.
 * Can be called only :
 *      after HandleBlockEnd
 *      and if the current command is block move.
 * Execute a command other than block move from the current block move selected items list.
 * Due to (minor) problems in undo/redo or/and display block,
 * a mirror/rotate command is immediatly executed and multible block commands
 * are not allowed (multiple commands are tricky to undo/redo in one time)
 */
void SCH_EDIT_FRAME::HandleBlockEndByPopUp( int Command, wxDC* DC )
{
    bool blockCmdFinished = true;   /* set to false for block command which
                                     * have a next step
                                     * and true if the block command is finished here
                                     */
    BLOCK_SELECTOR* block = &GetScreen()->m_BlockLocate;

    // can convert only a block move command to an other command
    if( block->m_Command != BLOCK_MOVE )
        return;
    // Useless if the new command is block move because we are already in block move.
    if( Command == BLOCK_MOVE )
        return;

    block->m_Command = (CmdBlockType) Command;
    block->SetMessageBlock( this );

    switch( block->m_Command )
    {
    case BLOCK_COPY:     /* move to copy */
        block->m_State = STATE_BLOCK_MOVE;
        blockCmdFinished = false;
        break;

    case BLOCK_DRAG:     /* move to Drag */
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );
        // Clear list of items to move, and rebuild it with items to drag:
        block->ClearItemsList();

        BreakSegmentOnJunction( GetScreen() );

        PickItemsInBlock( GetScreen()->m_BlockLocate, GetScreen() );
        if( block->GetCount() )
        {
            blockCmdFinished = false;
            CollectStructsToDrag( (SCH_SCREEN*) GetScreen() );
            if( DrawPanel->ManageCurseur )
                DrawPanel->ManageCurseur( DrawPanel, DC, false );
            block->m_State = STATE_BLOCK_MOVE;
        }
        break;

    case BLOCK_DELETE:     /* move to Delete */
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );
        if( block->GetCount() )
        {
            DeleteItemsInList( DrawPanel, block->m_ItemsSelection );
            OnModify();
        }
        TestDanglingEnds( GetScreen()->GetDrawItems(), DC );
        DrawPanel->Refresh();
        break;

    case BLOCK_SAVE:     /* Save list in paste buffer*/
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );
        if( block->GetCount() )
        {
            wxPoint move_vector = -GetScreen()->m_BlockLocate.m_BlockLastCursorPosition;
            SaveStructListForPaste( block->m_ItemsSelection );
            MoveItemsInList( g_BlockSaveDataList.m_ItemsSelection, move_vector );
        }
        break;

    case BLOCK_ZOOM:     /* Window Zoom */
        DrawPanel->ForceCloseManageCurseur( DrawPanel, DC );
        DrawPanel->SetCursor( DrawPanel->m_PanelCursor = DrawPanel->m_PanelDefaultCursor );
        Window_Zoom( GetScreen()->m_BlockLocate );
        break;


    case BLOCK_ROTATE:
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );
        if( block->GetCount() )
        {
//            blockCmdFinished = true;
            /* Compute the rotation center and put it on grid */
            wxPoint rotationPoint = block->Centre();
            PutOnGrid( &rotationPoint );
            SaveCopyInUndoList( block->m_ItemsSelection, UR_ROTATED, rotationPoint );
            RotateListOfItems( block->m_ItemsSelection, rotationPoint );
            OnModify();
        }
        TestDanglingEnds( GetScreen()->GetDrawItems(), DC );
        DrawPanel->Refresh();
//        block->m_State   = STATE_BLOCK_MOVE;
//        block->m_Command = BLOCK_MOVE; //allows multiple rotate
        break;

    case BLOCK_MIRROR_X:
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );
        if( block->GetCount() )
        {
//            blockCmdFinished = true;
            /* Compute the mirror center and put it on grid */
            wxPoint mirrorPoint = block->Centre();
            PutOnGrid( &mirrorPoint );
            SaveCopyInUndoList( block->m_ItemsSelection, UR_MIRRORED_X, mirrorPoint );
            Mirror_X_ListOfItems( block->m_ItemsSelection, mirrorPoint );
            OnModify();
//            block->m_State   = STATE_BLOCK_MOVE;
//            block->m_Command = BLOCK_MOVE; //allows multiple mirrors
        }
        TestDanglingEnds( GetScreen()->GetDrawItems(), DC );
        DrawPanel->Refresh();
        break;

    case BLOCK_MIRROR_Y:
        if( DrawPanel->ManageCurseur )
            DrawPanel->ManageCurseur( DrawPanel, DC, false );
        if( block->GetCount() )
        {
//            blockCmdFinished = true;
            /* Compute the mirror center and put it on grid */
            wxPoint mirrorPoint = block->Centre();
            PutOnGrid( &mirrorPoint );
            SaveCopyInUndoList( block->m_ItemsSelection, UR_MIRRORED_Y, mirrorPoint );
            MirrorListOfItems( block->m_ItemsSelection, mirrorPoint );
            OnModify();
//            block->m_State   = STATE_BLOCK_MOVE;
//            block->m_Command = BLOCK_MOVE; //allows multiple mirrors
        }

        TestDanglingEnds( GetScreen()->GetDrawItems(), DC );
        DrawPanel->Refresh();
        break;

    default:
        break;
    }

    if( blockCmdFinished )
    {
        block->ClearItemsList();
        block->m_Flags   = 0;
        block->m_State   = STATE_NO_BLOCK;
        block->m_Command = BLOCK_IDLE;
        DrawPanel->ManageCurseur = NULL;
        DrawPanel->ForceCloseManageCurseur = NULL;
        GetScreen()->SetCurItem( NULL );
        SetToolID( m_ID_current_state, DrawPanel->m_PanelDefaultCursor, wxEmptyString );
    }
}


/* Traces the outline of the search block structures
 * The entire block follows the cursor
 */
static void DrawMovingBlockOutlines( WinEDA_DrawPanel* panel, wxDC* DC, bool erase )
{
    BLOCK_SELECTOR* block = &panel->GetScreen()->m_BlockLocate;;
    BASE_SCREEN*    screen = panel->GetScreen();
    SCH_ITEM*       schitem;

    /* Erase old block contents. */
    if( erase )
    {
        block->Draw( panel, DC, block->m_MoveVector, g_XorMode, block->m_Color );

        for( unsigned ii = 0; ii < block->GetCount(); ii++ )
        {
            schitem = (SCH_ITEM*) block->m_ItemsSelection.GetPickedItem( ii );
            DrawStructsInGhost( panel, DC, schitem, block->m_MoveVector );
        }
    }

    /* Repaint new view. */
    block->m_MoveVector = screen->m_Curseur - block->m_BlockLastCursorPosition;

    block->Draw( panel, DC, block->m_MoveVector, g_XorMode, block->m_Color );

    for( unsigned ii = 0; ii < block->GetCount(); ii++ )
    {
        schitem = (SCH_ITEM*) block->m_ItemsSelection.GetPickedItem( ii );
        DrawStructsInGhost( panel, DC, schitem, block->m_MoveVector );
    }
}


/* Routine to Save an object from global drawing object list.
 *  This routine is the same as delete but:
 *  - the original list is NOT removed.
 *  - List is saved in g_BlockSaveDataList
 */
void SaveStructListForPaste( PICKED_ITEMS_LIST& aItemsList )
{
    g_BlockSaveDataList.ClearListAndDeleteItems();   // delete previous saved list, if exists

    /* save the new list: */
    ITEM_PICKER item;

    // In list the wrapper is owner of the shematic item, we can use the UR_DELETED
    // status for the picker because pickers with this status are owner of the picked item
    // (or TODO ?: create a new status like UR_DUPLICATE)
    item.m_UndoRedoStatus = UR_DELETED;
    for( unsigned ii = 0; ii < aItemsList.GetCount(); ii++ )
    {
        /* Make a copy of the original picked item. */
        SCH_ITEM* DrawStructCopy = DuplicateStruct( (SCH_ITEM*) aItemsList.GetPickedItem( ii ) );
        DrawStructCopy->SetParent( NULL );
        item.m_PickedItem = DrawStructCopy;
        g_BlockSaveDataList.PushItem( item );
    }
}


/*****************************************************************************
* Routine to paste a structure from the g_BlockSaveDataList stack.
*   This routine is the same as undelete but original list is NOT removed.
*****************************************************************************/
void SCH_EDIT_FRAME::PasteListOfItems( wxDC* DC )
{
    SCH_ITEM* Struct;

    if( g_BlockSaveDataList.GetCount() == 0 )
    {
        DisplayError( this, wxT( "No struct to paste" ) );
        return;
    }

    PICKED_ITEMS_LIST picklist;

    // Creates data, and push it as new data in undo item list buffer
    ITEM_PICKER       picker( NULL, UR_NEW );
    for( unsigned ii = 0; ii < g_BlockSaveDataList.GetCount(); ii++ )
    {
        Struct = DuplicateStruct(
            (SCH_ITEM*) g_BlockSaveDataList.m_ItemsSelection.GetPickedItem( ii ) );
        picker.m_PickedItem = Struct;
        picklist.PushItem( picker );

        // Clear annotation and init new time stamp for the new components:
        if( Struct->Type() == TYPE_SCH_COMPONENT )
        {
            ( (SCH_COMPONENT*) Struct )->m_TimeStamp = GetTimeStamp();
            ( (SCH_COMPONENT*) Struct )->ClearAnnotation( NULL );
        }
        SetaParent( Struct, GetScreen() );
        RedrawOneStruct( DrawPanel, DC, Struct, GR_DEFAULT_DRAWMODE );
        Struct->SetNext( GetScreen()->GetDrawItems() );
        GetScreen()->SetDrawItems( Struct );
    }

    SaveCopyInUndoList( picklist, UR_NEW );

    MoveItemsInList( picklist, GetScreen()->m_BlockLocate.m_MoveVector );

    /* clear .m_Flags member for all items */
    GetScreen()->ClearDrawingState();

    OnModify();

    return;
}


/* creates the list of items found when a drag block is initiated.
 * items are those selected in window block an some items outside this area but
 * connected to a selected item (connected wires to a component or an entry )
 */
static void CollectStructsToDrag( SCH_SCREEN* screen )
{
    SCH_ITEM*          Struct;
    SCH_LINE*          SegmStruct;

    PICKED_ITEMS_LIST* pickedlist = &screen->m_BlockLocate.m_ItemsSelection;

    if( pickedlist->GetCount() == 0 )
        return;

    screen->ClearDrawingState();

    for( unsigned ii = 0; ii < pickedlist->GetCount(); ii++ )
    {
        Struct = (SCH_ITEM*) pickedlist->GetPickedItem( ii );
        Struct->m_Flags = SELECTED;
    }

    if( screen->m_BlockLocate.m_Command != BLOCK_DRAG )
        return;


    /* Remove the displacement of segment and undo the selection. */
    for( unsigned ii = 0; ii < pickedlist->GetCount(); ii++ )
    {
        Struct = (SCH_ITEM*)(SCH_ITEM*) pickedlist->GetPickedItem( ii );
        if( Struct->Type() == DRAW_SEGMENT_STRUCT_TYPE )
        {
            SegmStruct = (SCH_LINE*) Struct;
            if( !screen->m_BlockLocate.Inside( SegmStruct->m_Start ) )
                SegmStruct->m_Flags |= STARTPOINT;

            if( !screen->m_BlockLocate.Inside( SegmStruct->m_End ) )
                SegmStruct->m_Flags |= ENDPOINT;

            // Save m_Flags for Undo/redo drag operations:
            pickedlist->SetPickerFlags( SegmStruct->m_Flags, ii );
        }
    }

    /* Search for other items to drag. They are end wires connected to selected
     * items
     */

    for( unsigned ii = 0; ii < pickedlist->GetCount(); ii++ )
    {
        Struct = (SCH_ITEM*)(SCH_ITEM*) pickedlist->GetPickedItem( ii );
        if( ( Struct->Type() == TYPE_SCH_LABEL )
           || ( Struct->Type() == TYPE_SCH_GLOBALLABEL )
           || ( Struct->Type() == TYPE_SCH_HIERLABEL ) )
        {
            #undef STRUCT
            #define STRUCT ( (SCH_TEXT*) Struct )
            if( !screen->m_BlockLocate.Inside( STRUCT->m_Pos ) )
            {
                AddPickedItem( screen, STRUCT->m_Pos );
            }
        }

        if( Struct->Type() == TYPE_SCH_COMPONENT )
        {
            // Add all pins of the selected component to list
            LIB_PIN* pin;
            wxPoint  pos;
            pin = GetNextPinPosition( (SCH_COMPONENT*) Struct, pos, true );
            while( pin )
            {
                if( !screen->m_BlockLocate.Inside( pos ) )
                {
                    // This pin is outside area,
                    // but because it it the pin of a selected component
                    // we must also select connected items to this pin
                    AddPickedItem( screen, pos );
                }

                pin = GetNextPinPosition( (SCH_COMPONENT*) Struct, pos, false );
            }
        }

        if( Struct->Type() == DRAW_SHEET_STRUCT_TYPE )
        {
            SCH_SHEET* sheet = (SCH_SHEET*) Struct;

            // Add all pins sheets of a selected hierarchical sheet to the list
            BOOST_FOREACH( SCH_SHEET_PIN label, sheet->GetSheetPins() )
            {
                AddPickedItem( screen, label.m_Pos );
            }
        }

        if( Struct->Type() == DRAW_BUSENTRY_STRUCT_TYPE )
        {
            SCH_BUS_ENTRY* item = (SCH_BUS_ENTRY*) Struct;
            AddPickedItem( screen, item->m_Pos );
            AddPickedItem( screen, item->m_End() );
        }
    }
}


/** AddPickedItem
 * add to the picked list in screen->m_BlockLocate items found at location
 * position
 * @param screen = the screen to consider
 * @param position = the wxPoint where items must be located to be select
 */
static void AddPickedItem( SCH_SCREEN* screen, wxPoint position )
{
    SCH_ITEM* Struct;

    /* Review the list of already selected elements. */
    PICKED_ITEMS_LIST* pickedlist = &screen->m_BlockLocate.m_ItemsSelection;

    for( unsigned ii = 0; ii < pickedlist->GetCount(); ii++ )
    {
        Struct = (SCH_ITEM*) pickedlist->GetPickedItem( ii );

        switch( Struct->Type() )
        {
        case DRAW_SEGMENT_STRUCT_TYPE:
                #undef STRUCT
                #define STRUCT ( (SCH_LINE*) Struct )
            if( STRUCT->m_Start == position )
                STRUCT->m_Flags &= ~STARTPOINT;

            if( STRUCT->m_End == position )
                STRUCT->m_Flags &= ~ENDPOINT;

            // Save m_Flags for Undo/redo drag operations:
            pickedlist->SetPickerFlags( STRUCT->m_Flags, ii );
            break;

        default:
            break;
        }
    }

    /* Review the list of elements not selected. */

    ITEM_PICKER picker;
    Struct = (SCH_ITEM*) screen->GetDrawItems();

    while( Struct )
    {
        picker.m_PickedItem     = Struct;
        picker.m_PickedItemType = Struct->Type();
        switch( Struct->Type() )
        {
        case TYPE_NOT_INIT:
            break;

        case DRAW_POLYLINE_STRUCT_TYPE:
            if( Struct->m_Flags & SELECTED )
                break;
            break;

        case DRAW_JUNCTION_STRUCT_TYPE:
                #undef STRUCT
                #define STRUCT ( (SCH_JUNCTION*) Struct )
            if( Struct->m_Flags & SELECTED )
                break;
            if( STRUCT->m_Pos != position )
                break;
            pickedlist->PushItem( picker );
            break;

        case DRAW_SEGMENT_STRUCT_TYPE:
                #undef STRUCT
                #define STRUCT ( (SCH_LINE*) Struct )
            if( Struct->m_Flags & SELECTED )
                break;
            if( STRUCT->m_Start == position )
            {
                Struct->m_Flags  = SELECTED | ENDPOINT | STARTPOINT;
                Struct->m_Flags &= ~STARTPOINT;

                // Save m_Flags for Undo/redo drag operations:
                picker.m_PickerFlags = Struct->m_Flags;
                pickedlist->PushItem( picker );
            }
            else if( STRUCT->m_End == position )
            {
                Struct->m_Flags  = SELECTED | ENDPOINT | STARTPOINT;
                Struct->m_Flags &= ~ENDPOINT;

                // Save m_Flags for Undo/redo drag operations:
                picker.m_PickerFlags = Struct->m_Flags;
                pickedlist->PushItem( picker );
            }
            break;

        case DRAW_BUSENTRY_STRUCT_TYPE:
            break;

        case TYPE_SCH_TEXT:
            break;

        case TYPE_SCH_LABEL:
                #undef STRUCT
                #define STRUCT ( (SCH_LABEL*) Struct )
            if( Struct->m_Flags & SELECTED )
                break;  /* Already in list */
            if( STRUCT->m_Pos != position )
                break;
            Struct->m_Flags |= SELECTED;
            pickedlist->PushItem( picker );
            break;

        case TYPE_SCH_HIERLABEL:
        case TYPE_SCH_GLOBALLABEL:
                #undef STRUCT
                #define STRUCT ( (SCH_LABEL*) Struct )
            if( Struct->m_Flags & SELECTED )
                break;  /* Already in list */
            if( STRUCT->m_Pos != position )
                break;
            Struct->m_Flags |= SELECTED;
            pickedlist->PushItem( picker );
            break;

        case TYPE_SCH_COMPONENT:
        case DRAW_SHEET_STRUCT_TYPE:
        case DRAW_HIERARCHICAL_PIN_SHEET_STRUCT_TYPE:
            break;

        case TYPE_SCH_MARKER:
                #undef STRUCT
                #define STRUCT ( (SCH_MARKER*) Struct )
            if( Struct->m_Flags & SELECTED )
                break;  /* Already in list */
            if( STRUCT->m_Pos != position )
                break;
            Struct->m_Flags |= SELECTED;
            pickedlist->PushItem( picker );
            break;

        case DRAW_NOCONNECT_STRUCT_TYPE:
                #undef STRUCT
                #define STRUCT ( (SCH_NO_CONNECT*) Struct )
            if( Struct->m_Flags & SELECTED )
                break;  /* Already in list */
            if( STRUCT->m_Pos != position )
                break;
            Struct->m_Flags |= SELECTED;
            pickedlist->PushItem( picker );
            break;

        default:
            break;
        }
        Struct = Struct->Next();
    }
}


/** GetNextPinPosition()
 * calculate position of the "next" pin of the aDrawLibItem component
 * @param aDrawLibItem = component to test.
 * @param aPosition = the calculated pin position, according to the component
 * orientation and position
 * @param aSearchFirst = if true, search for the first pin
 * @return a pointer to the pin
 */
static LIB_PIN* GetNextPinPosition( SCH_COMPONENT* aDrawLibItem,
                                    wxPoint&       aPosition,
                                    bool           aSearchFirst )
{
    static LIB_COMPONENT* Entry;
    static int Multi, convert;
    TRANSFORM transform;
    static wxPoint CmpPosition;
    static LIB_PIN* Pin;

    if( aSearchFirst )
    {
        Entry = CMP_LIBRARY::FindLibraryComponent( aDrawLibItem->m_ChipName );

        if( Entry == NULL )
            return NULL;

        Pin         = Entry->GetNextPin();
        Multi       = aDrawLibItem->m_Multi;
        convert     = aDrawLibItem->m_Convert;
        CmpPosition = aDrawLibItem->m_Pos;
        transform   = aDrawLibItem->m_Transform;
    }
    else
        Pin = Entry->GetNextPin( Pin );

    for( ; Pin != NULL; Pin = Entry->GetNextPin( Pin ) )
    {
        wxASSERT( Pin->Type() == COMPONENT_PIN_DRAW_TYPE );

        /* Skip items not used for this part */
        if( Multi && Pin->GetUnit() && ( Pin->GetUnit() != Multi ) )
            continue;
        if( convert && Pin->GetConvert() && ( Pin->GetConvert() != convert ) )
            continue;

        /* Calculate the pin position (according to the component orientation)
         */
        aPosition = DefaultTransform.TransformCoordinate( Pin->GetPosition() ) + CmpPosition;
        return Pin;
    }

    return NULL;
}
