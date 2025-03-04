///////////////////////////////////////////////////////////////////////////////
// Name:        src/common/selectdispatcher.cpp
// Purpose:     implements dispatcher for select() call
// Author:      Lukasz Michalski and Vadim Zeitlin
// Created:     December 2006
// Copyright:   (c) 2006 Lukasz Michalski
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

#if wxUSE_SELECT_DISPATCHER

#include "wx/private/selectdispatcher.h"
#include "wx/unix/private.h"

#ifndef WX_PRECOMP
    #include "wx/hash.h"
#endif

#include <errno.h>

#define wxSelectDispatcher_Trace wxT("selectdispatcher")

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxSelectSets
// ----------------------------------------------------------------------------

int wxSelectSets::ms_flags[wxSelectSets::Max] =
{
    wxFDIO_INPUT,
    wxFDIO_OUTPUT,
    wxFDIO_EXCEPTION,
};

const char *wxSelectSets::ms_names[wxSelectSets::Max] =
{
    "input",
    "output",
    "exceptional",
};

wxSelectSets::Callback wxSelectSets::ms_handlers[wxSelectSets::Max] =
{
    &wxFDIOHandler::OnReadWaiting,
    &wxFDIOHandler::OnWriteWaiting,
    &wxFDIOHandler::OnExceptionWaiting,
};

wxSelectSets::wxSelectSets()
{
    for ( int n = 0; n < Max; n++ )
    {
        wxFD_ZERO(&m_fds[n]);
    }
}

bool wxSelectSets::HasFD(int fd) const
{
    for ( int n = 0; n < Max; n++ )
    {
        if ( wxFD_ISSET(fd, (fd_set*) &m_fds[n]) )
            return true;
    }

    return false;
}

bool wxSelectSets::SetFD(int fd, int flags)
{
    for ( int n = 0; n < Max; n++ )
    {
        if ( flags & ms_flags[n] )
            wxFD_SET(fd, &m_fds[n]);
        else if ( wxFD_ISSET(fd,  (fd_set*) &m_fds[n]) )
            wxFD_CLR(fd, &m_fds[n]);
    }

    return true;
}

int wxSelectSets::Select(int nfds, struct timeval *tv)
{
    return select(nfds, &m_fds[Read], &m_fds[Write], &m_fds[Except], tv);
}

bool wxSelectSets::Handle(int fd, wxFDIOHandler& handler) const
{
    for ( int n = 0; n < Max; n++ )
    {
        if ( wxFD_ISSET(fd, (fd_set*) &m_fds[n]) )
        {
            (handler.*ms_handlers[n])();
            // callback can modify sets and destroy handler
            // this forces that one event can be processed at one time
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------------------------------
// wxSelectDispatcher
// ----------------------------------------------------------------------------

bool wxSelectDispatcher::RegisterFD(int fd, wxFDIOHandler *handler, int flags)
{
    if ( !wxMappedFDIODispatcher::RegisterFD(fd, handler, flags) )
        return false;

    if ( !m_sets.SetFD(fd, flags) )
       return false;

    if ( fd > m_maxFD )
      m_maxFD = fd;

    return true;
}

bool wxSelectDispatcher::UnregisterFD(int fd)
{
    m_sets.ClearFD(fd);

    if ( !wxMappedFDIODispatcher::UnregisterFD(fd) )
        return false;

    // remove the handler if we don't need it any more
    if ( !m_sets.HasFD(fd) )
    {
        if ( fd == m_maxFD )
        {
            // need to find new max fd
            m_maxFD = -1;
            for ( wxFDIOHandlerMap::const_iterator it = m_handlers.begin();
                  it != m_handlers.end();
                  ++it )
            {
                if ( it->first > m_maxFD )
                {
                    m_maxFD = it->first;
                }
            }
        }
    }

    return true;
}

int wxSelectDispatcher::ProcessSets(const wxSelectSets& sets)
{
    int numEvents = 0;
    for ( int fd = 0; fd <= m_maxFD; fd++ )
    {
        if ( !sets.HasFD(fd) )
            continue;

        wxFDIOHandler * const handler = FindHandler(fd);
        if ( !handler )
            continue;

        if ( sets.Handle(fd, *handler) )
            numEvents++;
    }

    return numEvents;
}

int wxSelectDispatcher::DoSelect(wxSelectSets& sets, int timeout) const
{
    struct timeval tv,
                  *ptv;
    if ( timeout != TIMEOUT_INFINITE )
    {
        ptv = &tv;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000)*1000;
    }
    else // no timeout
    {
        ptv = NULL;
    }

    int ret = sets.Select(m_maxFD + 1, ptv);

    // TODO: we need to restart select() in this case but for now just return
    //       as if timeout expired
    if ( ret == -1 && errno == EINTR )
        ret = 0;

    return ret;
}

bool wxSelectDispatcher::HasPending() const
{
    wxSelectSets sets(m_sets);
    return DoSelect(sets, 0) > 0;
}

int wxSelectDispatcher::Dispatch(int timeout)
{
    wxSelectSets sets(m_sets);
    switch ( DoSelect(sets, timeout) )
    {
        case -1:
		break;

        case 0:
            // timeout expired without anything happening
            return 0;

        default:
            return ProcessSets(sets);
    }

    return -1;
}

#endif // wxUSE_SELECT_DISPATCHER
