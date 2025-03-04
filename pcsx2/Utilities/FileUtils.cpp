/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#include <wx/dir.h>
#include <wx/string.h>

bool RemoveDirectory( const wxString& dirname ) {
		{
			wxDir dir( dirname );
			if ( !dir.IsOpened() ) {
				return false;
			}

			wxString filename;

			// delete subdirs recursively
			if ( dir.GetFirst( &filename, wxEmptyString, wxDIR_DIRS | wxDIR_HIDDEN ) ) {
				do {
					if ( !RemoveDirectory( wxFileName( dirname, filename ).GetFullPath() ) ) {
						return false;
					}
				} while ( dir.GetNext( &filename ) );
			}

			// delete files
			if ( dir.GetFirst( &filename, wxEmptyString, wxDIR_FILES | wxDIR_HIDDEN ) ) {
				do {
					if ( !wxRemoveFile( wxFileName( dirname, filename ).GetFullPath() ) ) {
						return false;
					}
				} while ( dir.GetNext( &filename ) );
			}
		}

	// oddly enough this has different results compared to the more sensible dirname.Rmdir(), don't change!
	return wxFileName::Rmdir( dirname );
}
