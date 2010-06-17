#ifndef __dialog_eeschema_options__
#define __dialog_eeschema_options__

/**
 * @file
 * Subclass of DIALOG_EESCHEMA_OPTIONS_BASE, which is generated by wxFormBuilder.
 */

#include "dialog_eeschema_options_base.h"

class DIALOG_EESCHEMA_OPTIONS : public DIALOG_EESCHEMA_OPTIONS_BASE
{
public:
    DIALOG_EESCHEMA_OPTIONS( wxWindow* parent );

    void SetUnits( const wxArrayString& units, int select = 0 );
    int GetUnitsSelection( void ) { return m_choiceUnits->GetSelection(); }

    void SetGridSelection( int select )
    {
        m_choiceGridSize->SetSelection( select );
    }
    int GetGridSelection( void ) { return m_choiceGridSize->GetSelection(); }
    void SetGridSizes( const GridArray& grid_sizes, int grid_id );

    void SetLineWidth( int line_width )
    {
        m_spinLineWidth->SetValue( line_width );
    }
    int GetLineWidth( void ) { return m_spinLineWidth->GetValue(); }

    void SetTextSize( int text_size )
    {
        m_spinTextSize->SetValue( text_size );
    }
    int GetTextSize( void ) { return m_spinTextSize->GetValue(); }

    void SetRepeatHorizontal( int displacement )
    {
        m_spinRepeatHorizontal->SetValue( displacement );
    }
    int GetRepeatHorizontal( void )
    {
        return m_spinRepeatHorizontal->GetValue();
    }

    void SetRepeatVertical( int displacement )
    {
        m_spinRepeatVertical->SetValue( displacement );
    }
    int GetRepeatVertical( void ) { return m_spinRepeatVertical->GetValue(); }

    void SetRepeatLabel( int increment )
    {
        m_spinRepeatLabel->SetValue( increment );
    }
    int GetRepeatLabel( void ) { return m_spinRepeatLabel->GetValue(); }

    void SetShowGrid( bool show ) { m_checkShowGrid->SetValue( show ); }
    bool GetShowGrid( void ) { return m_checkShowGrid->GetValue(); }

    void SetShowHiddenPins( bool show )
    {
        m_checkShowHiddenPins->SetValue( show );
    }
    bool GetShowHiddenPins( void )
    {
        return m_checkShowHiddenPins->GetValue();
    }

    void SetEnableAutoPan( bool enable )
    {
        m_checkAutoPan->SetValue( enable );
    }
    bool GetEnableAutoPan( void ) { return m_checkAutoPan->GetValue(); }

    void SetEnableHVBusOrientation( bool enable )
    {
        m_checkHVOrientation->SetValue( enable );
    }
    bool GetEnableHVBusOrientation( void )
    {
        return m_checkHVOrientation->GetValue();
    }

    void SetShowPageLimits( bool show )
    {
        m_checkPageLimits->SetValue( show );
    }
    bool GetShowPageLimits( void )
    {
        return m_checkPageLimits->GetValue();
    }


    /** Set the field \a aNdx textctrl to \a aName */
    void SetFieldName( int aNdx, wxString aName);

    /** Get the field \a aNdx name from the fields textctrl */
    wxString GetFieldName( int aNdx );
};

#endif // __dialog_eeschema_options__
