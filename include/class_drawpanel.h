/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2004 Jean-Pierre Charras, jaen-pierre.charras@gipsa-lab.inpg.com
 * Copyright (C) 2011 Wayne Stambaugh <stambaughw@gmail.com>
 * Copyright (C) 1992-2018 KiCad Developers, see AUTHORS.txt for contributors.
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

/**
 * @file class_drawpanel.h:
 * @brief EDA_DRAW_PANEL class definition.
 */

#ifndef  PANEL_WXSTRUCT_H
#define  PANEL_WXSTRUCT_H

#include <base_struct.h>
#include <gr_basic.h>
#include <eda_rect.h>

#include <draw_panel_base.h>

class BASE_SCREEN;
class PCB_SCREEN;


class EDA_DRAW_PANEL : public DRAW_PANEL_BASE, public wxScrolledWindow
{
private:
    int m_defaultCursor;
    int m_currentCursor;

public:

    EDA_DRAW_PANEL( EDA_DRAW_FRAME* parent, int id, const wxPoint& pos, const wxSize& size );
    ~EDA_DRAW_PANEL();

    /**
     * Function GetDisplayOptions
     * A way to pass info to draw functions.
     * this is just an accessor to the GetDisplayOptions() parent frame function.
     */
    void* GetDisplayOptions() override;

    BASE_SCREEN* GetScreen() override;

    EDA_DRAW_FRAME* GetParent() const override;

    void OnPaint( wxPaintEvent& event );

    EDA_RECT* GetClipBox() override { return &m_ClipBox; }

    void SetClipBox( const EDA_RECT& aRect ) { m_ClipBox = aRect; }

    bool GetAbortRequest() const { return m_abortRequest; }

    void SetAbortRequest( bool aAbortRequest ) { m_abortRequest = aAbortRequest; }

    bool GetEnableMousewheelPan() const { return m_enableMousewheelPan; }

    void SetEnableMousewheelPan( bool aEnable ) override;

    bool GetEnableZoomNoCenter() const { return m_enableZoomNoCenter; }

    void SetEnableZoomNoCenter( bool aEnable ) override;

    bool GetEnableAutoPan() const { return m_enableAutoPan; }

    void SetEnableAutoPan( bool aEnable ) override;

    void SetAutoPanRequest( bool aEnable ) { m_requestAutoPan = aEnable; }

    void SetIgnoreMouseEvents( bool aIgnore ) { m_ignoreMouseEvents = aIgnore; }

    void SetIgnoreLeftButtonReleaseEvent( bool aIgnore ) { m_ignoreNextLeftButtonRelease = aIgnore; }

    void SetEnableBlockCommands( bool aEnable ) { m_enableBlockCommands = aEnable; }

    bool GetPrintMirrored() const               { return m_PrintIsMirrored; }
    void SetPrintMirrored( bool aMirror )       { m_PrintIsMirrored = aMirror; }

    void SetCanStartBlock( int aStartBlock ) { m_canStartBlock = aStartBlock; }

    /**
     * Function DrawBackGround
     * @param DC = current Device Context
     * Draws (if allowed) :
     * the grid
     * X and Y axis
     * X and Y auxiliary axis
     */
    void DrawBackGround( wxDC* DC ) override;

    /**
     * Function DrawGrid
     * draws a grid to \a aDC.
     * @see m_ClipBox to determine the damaged area of the drawing to draw the grid.
     * @see EDA_DRAW_FRAME::IsGridVisible() to determine if grid is shown.
     * @see EDA_DRAW_FRAME::GetGridColor() for the color of the grid.
     * @param aDC The device context to draw the grid.
     */
    void DrawGrid( wxDC* aDC ) override;

    /**
     * Function DrawAuxiliaryAxis
     * Draw the Auxiliary Axis, used in Pcbnew which as origin coordinates
     * for gerber and excellon files
     * @param aDC = current Device Context
     * @param aDrawMode = draw mode (GR_COPY, GR_OR ..)
     */
    void DrawAuxiliaryAxis( wxDC* aDC, GR_DRAWMODE aDrawMode ) override;

    /**
     * Function DrawGridAxis
     * Draw on auxiliary axis, used in Pcbnew to show grid origin, when
     * the grid origin is set by user, and is not (0,0)
     * @param aDC = current Device Context
     * @param aDrawMode = draw mode (GR_COPY, GR_OR ..)
     * @param aGridOrigin = the absolute coordinate of grid origin for snap.
     */
    void DrawGridAxis( wxDC* aDC, GR_DRAWMODE aDrawMode, const wxPoint& aGridOrigin ) override;

    void OnEraseBackground( wxEraseEvent& event ) { }

    /**
     * Function OnActivate
     * handles window activation events.
     * <p>
     * The member m_canStartBlock is initialize to avoid a block start command on activation
     * (because a left mouse button can be pressed and no block command wanted.  This happens
     * when enter on a hierarchy sheet on double click.
     *</p>
     */
    void OnActivate( wxActivateEvent& event );

    /**
     * Function OnTimer
     * handle timer events
     * <p>
     * The class will start a timer when a mouse-up event is handled.  If a
     * double-click event is not handled inside of a specified interval,
     * the timer event will fire, causing the single-click event to be handled.
     * Otherwise, the system will process the double-click.
     */
    void OnTimer( wxTimerEvent& event );

    /**
     * Function DoPrepareDC
     * sets up the device context \a aDC for drawing.
     * <p>
     * This overrides wxScrolledWindow::DoPrepareDC() for setting up the the device context
     * used for drawing.   The scale factor and drawing logical offset are set and the base
     * method is called to set the DC device origin (scroll bar position).  This connects
     * everything together to achieve the appropriate coordinate manipulation using wxDC
     * LogicalToDeviceXXX and DeviceToLogixalXXX methods.  This gets called automatically
     * for a paint event.  If you do any drawing outside the paint event, you must call
     * DoPrepareDC manually.
     * </p>
     * @param aDC The device context to prepare.
     */
    virtual void DoPrepareDC( wxDC& aDC ) override;

    /**
     * Function DeviceToLogical
     * converts \a aRect from device to drawing (logical) coordinates.
     * <p>
     * \a aRect must be in scrolled device units.
     * </p>
     * @param aRect The rectangle to convert.
     * @param aDC The device context used for the conversion.
     * @return A rectangle converted to drawing units.
     */
    wxRect DeviceToLogical( const wxRect& aRect, wxDC& aDC ) override;

    /* Mouse and keys events */

    /**
     * Function OnMouseWheel
     * handles mouse wheel events.
     * <p>
     * The mouse wheel is used to provide support for zooming and panning.  This
     * is accomplished by converting mouse wheel events in pseudo menu command
     * events and sending them to the appropriate parent window event handler.
     *</p>
     */
    void OnMouseWheel( wxMouseEvent& event );
#if wxCHECK_VERSION( 3, 1, 0 ) || defined( USE_OSX_MAGNIFY_EVENT )
    void OnMagnify( wxMouseEvent& event );
#endif
    void OnMouseEvent( wxMouseEvent& event );
    void OnMouseEntering( wxMouseEvent& aEvent );
    void OnMouseLeaving( wxMouseEvent& event );
    void OnKeyEvent( wxKeyEvent& event );
    void OnCharHook( wxKeyEvent& event );

    void OnPan( wxCommandEvent& event );

    void EraseScreen( wxDC* DC ) override;
    void OnScrollWin( wxCommandEvent& event );
    void OnScroll( wxScrollWinEvent& event );

    void SetZoom( double mode ) override;
    double GetZoom() override;

    //void SetGrid( const wxRealPoint& size ) override;
    //wxRealPoint GetGrid() override;

    /**
     * Function OnRightClick
     * builds and displays a context menu on a right mouse button click.
     * @return true if the context menu is shown, or false
     */
    bool OnRightClick( wxMouseEvent& event );

    /**
     * Function IsPointOnDisplay
     * @param aPosition The position to test in logical (drawing) units.
     * @return true if \a aPosition is visible on the screen.
     *         false if \a aPosition is not visible on the screen.
     */
    bool IsPointOnDisplay( const wxPoint& aPosition ) override;

    /**
     * Function SetClipBox
     * sets the clip box in drawing (logical) units from \a aRect in device units.
     * <p>
     * If \a aRect is NULL, then the entire visible area of the screen is used as the clip
     * area.  The clip box is used when drawing to determine which objects are not visible
     * and do not need to be drawn.  Note that this is not the same as setting the device
     * context clipping with wxDC::SetClippingRegion().  This is the rectangle used by the
     * drawing functions in gr_basic.cpp used to determine if the item to draw is off screen
     * and therefore not drawn.
     * </p>
     * @param aDC The device context use for drawing with the correct scale and
     *            offsets already configured.  See DoPrepareDC().
     * @param aRect The clip rectangle in device units or NULL for the entire visible area
     *              of the screen.
     */
    void SetClipBox( wxDC& aDC, const wxRect* aRect = NULL ) override;

    void ReDraw( wxDC* aDC, bool aEraseBackground = true ) override;

    /**
     * Function RefreshDrawingRect
     * redraws the contents of \a aRect in drawing units.  \a aRect is converted to
     * screen coordinates and wxWindow::RefreshRect() is called to repaint the region.
     * @param aRect The rectangle to repaint.
     * @param aEraseBackground Erases the background if true.
     */
    void RefreshDrawingRect( const EDA_RECT& aRect, bool aEraseBackground = true ) override;

    /// @copydoc wxWindow::Refresh()
    virtual void Refresh( bool eraseBackground = true, const wxRect* rect = NULL ) override;

    /**
     * Function GetScreenCenterLogicalPosition
     * @return The current screen center position in logical (drawing) units.
     */
    wxPoint GetScreenCenterLogicalPosition() override;

    /**
     * Function MoveCursorToCrossHair
     * warps the cursor to the current cross hair position.
     */
    void MoveCursorToCrossHair() override;

    /**
     * Function ToDeviceXY
     * transforms logical to device coordinates
     */
    wxPoint ToDeviceXY( const wxPoint& pos ) override;

    /**
     * Function ToLogicalXY
     * transforms device to logical coordinates
     */
    wxPoint ToLogicalXY( const wxPoint& pos ) override;

    /**
     * Function MoveCursor
     * moves the mouse pointer to \a aPosition in logical (drawing) units.
     * @param aPosition The position in logical units to move the cursor.
     */
    void MoveCursor( const wxPoint& aPosition ) override;

    /* Cursor functions */
    /**
     * Function DrawCrossHair
     * draws the user cross hair.
     * <p>
     * The user cross hair is not the mouse cursor although they may be at the same screen
     * position.  The mouse cursor is still render by the OS.  This is a drawn cross hair
     * that is used to snap to grid when grid snapping is enabled.  This is as an indicator
     * to where the next user action will take place.
     * </p>
     * @param aDC - the device context to draw the cursor
     * @param aColor - the color to draw the cursor
     */
    void DrawCrossHair( wxDC* aDC, COLOR4D aColor = COLOR4D::WHITE ) override;

    // Hide the cross hair.
    void CrossHairOff( wxDC* DC ) override;

    // Show the cross hair.
    void CrossHairOn( wxDC* DC ) override;

    /**
     * Function EndMouseCapture
     * ends mouse a capture.
     *
     * Check to see if the cursor is being managed for block or editing commands and release it.
     * @param aId The command ID to restore or -1 to keep the current command ID.
     * @param aCursorId The wxWidgets stock cursor ID to set the cursor to or -1 to keep the
     *                  current cursor.
     * @param aTitle The tool message to display in the status bar or wxEmptyString to clear
     *               the message.
     * @param aCallEndFunc Call the abort mouse capture callback if true.
     */
    void EndMouseCapture( int aId = -1, int aCursorId = -1,
                          const wxString& aTitle = wxEmptyString,
                          bool aCallEndFunc = true ) override;

    inline bool IsMouseCaptured() const { return m_mouseCaptureCallback != NULL; }

    /**
     * Function CallMouseCapture
     * calls the mouse capture callback.
     *
     * @param aDC A point to a wxDC object to perform any drawing upon.
     * @param aPosition A referecnce to a wxPoint object containing the current cursor
     *                  position.
     * @param aErase True indicates the item being drawn should be erase before drawing
     *               it a \a aPosition.
     */
    void CallMouseCapture( wxDC* aDC, const wxPoint& aPosition, bool aErase )override;

    /**
     * Function CallEndMouseCapture
     * calls the end mouse capture callback.
     *
     * @param aDC A point to a wxDC object to perform any drawing upon.
     */
    void CallEndMouseCapture( wxDC* aDC )override;

    /**
     * Function SetCurrentCursor
     * Set the current cursor shape for drawpanel
     */
    void SetCurrentCursor( int aCursor )override
    {
        m_currentCursor = aCursor;
        SetCursor( (wxStockCursor) m_currentCursor );
    }

    virtual wxWindow* GetWindow() override { return this; };

    virtual int GetDefaultCursor() const override { return m_defaultCursor; }

    virtual int GetCurrentCursor() const override { return m_currentCursor; }

    DECLARE_EVENT_TABLE()
};


/**
 * Class EDA_CROSS_HAIR_MANAGER
 * is used to hide the cross hair and restore it when the class goes out of scope.
 */
class EDA_CROSS_HAIR_MANAGER
{
public:
    EDA_CROSS_HAIR_MANAGER( EDA_DRAW_PANEL* aPanel, wxDC* aDC ) :
        m_panel( aPanel ),
        m_dc( aDC )
    {
        if( aPanel && aDC )
            aPanel->CrossHairOff( aDC );
    }

    ~EDA_CROSS_HAIR_MANAGER()
    {
        if( m_panel && m_dc )
            m_panel->CrossHairOn( m_dc );
    }

private:
    EDA_DRAW_PANEL* m_panel;
    wxDC* m_dc;

    DECLARE_NO_COPY_CLASS( EDA_CROSS_HAIR_MANAGER )
};


#endif  /* #ifndef PANEL_WXSTRUCT_H */
