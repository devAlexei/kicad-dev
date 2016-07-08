/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016 CERN
 * @author Maciej Suminski <maciej.suminski@cern.ch>
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

#ifndef DIALOG_SIM_SETTINGS_BASE_H
#define DIALOG_SIM_SETTINGS_BASE_H

#include "dialog_sim_settings_base.h"
#include <wx/valnum.h>

class NETLIST_EXPORTER_PSPICE_SIM;

class DIALOG_SIM_SETTINGS : public DIALOG_SIM_SETTINGS_BASE
{
public:
    DIALOG_SIM_SETTINGS( wxWindow* aParent );

    const wxString& GetSimCommand() const
    {
        return m_simCommand;
    }

    void SetNetlistExporter( NETLIST_EXPORTER_PSPICE_SIM* aExporter )
    {
        m_exporter = aExporter;
    }

    bool TransferDataFromWindow() override;
    bool TransferDataToWindow() override;

    int ShowModal() override;

private:
    enum SCALE_TYPE
    {
        DECADE,
        OCTAVE,
        LINEAR
    };

    void onLoadDirectives( wxCommandEvent& event ) override;

    static wxString scaleToString( int aOption )
    {
        switch( aOption )
        {
            case DECADE:
                return wxString( "dec" );

            case OCTAVE:
                return wxString( "oct" );

            case LINEAR:
                return wxString( "lin" );
        }

        wxASSERT_MSG( false, "Unhandled scale type" );

        return wxEmptyString;
    }

    wxString m_simCommand;
    NETLIST_EXPORTER_PSPICE_SIM* m_exporter;

    wxFloatingPointValidator<double> m_posFloatValidator;
    wxIntegerValidator<int> m_posIntValidator;
};

#endif /* DIALOG_SIM_SETTINGS_BASE_H */
