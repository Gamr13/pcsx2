///////////////////////////////////////////////////////////////////////////////
// Name:        src/msw/basemsw.cpp
// Purpose:     misc stuff only used in console applications under MSW
// Author:      Vadim Zeitlin
// Modified by:
// Created:     22.06.2003
// Copyright:   (c) 2003 Vadim Zeitlin <vadim@wxwindows.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/event.h"
#endif //WX_PRECOMP

#include "wx/apptrait.h"
#include "wx/evtloop.h"
#include "wx/crt.h"
#include "wx/msw/private.h"

// ============================================================================
// wxAppTraits implementation
// ============================================================================

#if wxUSE_THREADS
// ============================================================================
// wxConsoleAppTraits implementation
// ============================================================================
WXDWORD wxConsoleAppTraits::WaitForThread(WXHANDLE hThread, int WXUNUSED(flags))
{
    return ::WaitForSingleObject((HANDLE)hThread, INFINITE);
}
#endif // wxUSE_THREADS

wxEventLoopBase *wxConsoleAppTraits::CreateEventLoop()
{
#if wxUSE_CONSOLE_EVENTLOOP
    return new wxEventLoop();
#else // !wxUSE_CONSOLE_EVENTLOOP
    return NULL;
#endif // wxUSE_CONSOLE_EVENTLOOP/!wxUSE_CONSOLE_EVENTLOOP
}
