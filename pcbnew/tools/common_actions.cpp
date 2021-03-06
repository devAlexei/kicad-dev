/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013 CERN
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

#include "common_actions.h"
#include <tool/action_manager.h>

// Selection tool actions
TOOL_ACTION COMMON_ACTIONS::selectionActivate( "pcbnew.InteractiveSelection",
        AS_GLOBAL, 'S',
        "Selection tool", "Allows to select items" );

// Move tool actions
TOOL_ACTION COMMON_ACTIONS::moveActivate( "pcbnew.InteractiveMove",
        AS_GLOBAL, 'M',
        "Move", "Moves the selected item(s)" );

TOOL_ACTION COMMON_ACTIONS::rotate( "pcbnew.InteractiveMove.rotate",
        AS_CONTEXT, ' ',
        "Rotate", "Rotates selected item(s)" );

TOOL_ACTION COMMON_ACTIONS::flip( "pcbnew.InteractiveMove.flip",
        AS_CONTEXT, 'F',
        "Flip", "Flips selected item(s)" );
