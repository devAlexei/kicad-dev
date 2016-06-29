/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2016 CERN
 * @author Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
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

#include <schframe.h>
#include <kiway.h>

#include <netlist_exporter_kicad.h>
#include <netlist_exporters/netlist_exporter_pspice.h>

#include <reporter.h>

#include "sim_plot_frame.h"
#include "sim_plot_panel.h"
#include "spice_simulator.h"

#ifdef KICAD_SCRIPTING
 #include <python_scripting.h>
#endif


class SIM_THREAD_REPORTER : public REPORTER
{
public:
    SIM_THREAD_REPORTER( SIM_PLOT_FRAME* aParent )
        : m_parent( aParent )
    {
    }

    virtual REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_UNDEFINED )
    {
        wxThreadEvent* event = new wxThreadEvent( wxEVT_SIM_REPORT );
        event->SetPayload( aText );
        wxQueueEvent( m_parent, event );
        return *this;
    }

private:
    SIM_PLOT_FRAME* m_parent;
};


class SIM_THREAD : public wxThread
{
public:
    SIM_THREAD( SIM_PLOT_FRAME* aParent, SPICE_SIMULATOR* aSimulator )
        : m_parent( aParent ), m_sim( aSimulator )
    {}

    ~SIM_THREAD()
    {
        wxCriticalSectionLocker lock( m_parent->m_simThreadCS );

        // Let know the parent that the pointer is not valid anymore
        m_parent->m_simThread = NULL;
    }

private:
    // Thread routine
    ExitCode Entry()
    {
        assert( m_sim );

        m_sim->Run();
        wxQueueEvent( m_parent, new wxThreadEvent( wxEVT_SIM_FINISHED ) );

        return (ExitCode) 0;
    }

    SIM_PLOT_FRAME* m_parent;
    SPICE_SIMULATOR* m_sim;
};


SIM_PLOT_FRAME::SIM_PLOT_FRAME( KIWAY* aKiway, wxWindow* aParent )
    : SIM_PLOT_FRAME_BASE( aKiway, aParent )
{
    m_exporter = NULL;
    m_simulator = NULL;
    m_currentPlot = NULL;
    m_pyConsole = NULL;
    m_simThread = NULL;

    Connect( wxEVT_CLOSE_WINDOW, wxCloseEventHandler( SIM_PLOT_FRAME::onClose ), NULL, this );
    Connect( wxEVT_SIM_REPORT, wxThreadEventHandler( SIM_PLOT_FRAME::onSimReport ), NULL, this );
    Connect( wxEVT_SIM_FINISHED, wxThreadEventHandler( SIM_PLOT_FRAME::onSimFinished ), NULL, this );

    NewPlot();
}


SIM_PLOT_FRAME::~SIM_PLOT_FRAME()
{
    // m_simThread should be already destroyed by onClose()
    assert( m_simThread == NULL );

    delete m_exporter;
    delete m_simulator;
}


void SIM_PLOT_FRAME::StartSimulation()
{
    delete m_exporter;
    delete m_simulator;

    m_simConsole->Clear();

    m_simulator = SPICE_SIMULATOR::CreateInstance( "ngspice" );
    m_simulator->SetConsoleReporter( new SIM_THREAD_REPORTER( this ) );
    m_simulator->Init();

    NETLIST_OBJECT_LIST* net_atoms = m_schematicFrame->BuildNetListBase();
    m_exporter = new NETLIST_EXPORTER_PSPICE( net_atoms, Prj().SchLibs(), Prj().SchSearchS() );
    STRING_FORMATTER formatter;

    m_exporter->Format( &formatter, GNL_ALL );
    //m_plotPanel->DeleteTraces();

    m_simulator->LoadNetlist( formatter.GetString() );

    // Execute the simulation in a separate thread
    {
        wxCriticalSectionLocker lock( m_simThreadCS );

        assert( m_simThread == NULL );
        m_simThread = new SIM_THREAD( this, m_simulator );

        if( m_simThread->Run() != wxTHREAD_NO_ERROR )
        {
            wxLogError( "Can't create the simulator thread!" );
            delete m_simThread;
        }
    }
}


void SIM_PLOT_FRAME::PauseSimulation()
{
    wxCriticalSectionLocker lock( m_simThreadCS );

    if( m_simThread )
    {
        if( m_simThread->Pause() != wxTHREAD_NO_ERROR )
            wxLogError( "Cannot pause the simulation thread" );
    }
}


void SIM_PLOT_FRAME::ResumeSimulation()
{
    wxCriticalSectionLocker lock( m_simThreadCS );

    if( m_simThread )
    {
        if( m_simThread->Resume() != wxTHREAD_NO_ERROR )
            wxLogError( "Cannot resume the simulation thread" );
    }
}


void SIM_PLOT_FRAME::StopSimulation()
{
    wxCriticalSectionLocker lock( m_simThreadCS );

    if( m_simThread )
    {
        // we could use m_simThread->Delete() if there was a way to run the simulation
        // in parts, so the thread would be able to call TestDestroy()
        if( m_simThread->Kill() != wxTHREAD_NO_ERROR )
            wxLogError( "Cannot delete the simulation thread" );
    }
}


void SIM_PLOT_FRAME::NewPlot()
{
    SIM_PLOT_PANEL* plot = new SIM_PLOT_PANEL( this, wxID_ANY );
    m_plotNotebook->AddPage( plot, wxT( "Plot1" ), true );
    m_currentPlot = plot;
}


void SIM_PLOT_FRAME::onClose( wxCloseEvent& aEvent )
{
    {
        wxCriticalSectionLocker lock( m_simThreadCS );

        if( m_simThread )
        {
            if( m_simThread->Delete() != wxTHREAD_NO_ERROR )
                wxLogError( "Cannot delete the simulation thread" );
        }
    }

    int timeout = 10;

    while( 1 )
    {
        // Wait until the thread is finished
        {
            wxCriticalSectionLocker lock( m_simThreadCS );

            if( m_simThread == NULL )
                break;
        }

        wxThread::This()->Sleep( 1 );

        if( --timeout == 0 )
        {
            m_simThread->Kill();        // no mercy
            break;
        }
    }

    Destroy();
}


void SIM_PLOT_FRAME::onSimReport( wxThreadEvent& aEvent )
{
    m_simConsole->WriteText( aEvent.GetPayload<wxString>() );
    m_simConsole->Newline();
}


void SIM_PLOT_FRAME::onSimFinished( wxThreadEvent& aEvent )
{
    const auto& netMapping = m_exporter->GetNetIndexMap();

    // Fill the signals listbox
    m_signals->Clear();

    for( const auto& net : netMapping )
    {
        if( net.first != "GND" )
            m_signals->Append( net.first );
    }

    //    auto data_t = m_simulator->GetPlot( "time" );

    for( auto& name : m_exporter->GetProbeList() )
    {
        char spiceName[1024];

        snprintf( spiceName, sizeof( spiceName ), "V(%d)", netMapping.at( name ) );
        //wxLogDebug( "probe %s->%s\n", (const char *) name.c_str(), spiceName );
        //    auto data_y = m_simulator->GetPlot( spiceName );

        //wxLogDebug( "%d - %d data points\n", data_t.size(), data_y.size() );
        //    m_plotPanel->AddTrace(wxT("V(") + name + wxT(")"), data_t.size(), data_t.data(), data_y.data(), 0);
    }
}
