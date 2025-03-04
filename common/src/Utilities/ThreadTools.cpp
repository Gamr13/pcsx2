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

#ifdef __linux__
#include <signal.h> // for pthread_kill, which is in pthread.h on w32-pthreads
#endif

#include "PersistentThread.h"
#include "ThreadingInternal.h"
#include "EventSource.inl"

using namespace Threading;

template class EventSource<EventListener_Thread>;

class StaticMutex : public Mutex
{
protected:
    bool &m_DeletedFlag;

public:
    StaticMutex(bool &deletedFlag)
        : m_DeletedFlag(deletedFlag)
    {
    }

    virtual ~StaticMutex()
    {
        m_DeletedFlag = true;
    }
};

static pthread_key_t curthread_key = 0;
static s32 total_key_count = 0;

static bool tkl_destructed = false;
static StaticMutex total_key_lock(tkl_destructed);

static void make_curthread_key(const pxThread *thr)
{
    ScopedLock lock(total_key_lock);
    if (total_key_count++ != 0)
        return;

    if (0 != pthread_key_create(&curthread_key, NULL)) {
        curthread_key = 0;
    }
}

static void unmake_curthread_key()
{
    ScopedLock lock;
    if (!tkl_destructed)
        lock.AssignAndLock(total_key_lock);

    if (--total_key_count > 0)
        return;

    if (curthread_key)
        pthread_key_delete(curthread_key);

    curthread_key = 0;
}

void Threading::pxThread::_pt_callback_cleanup(void *handle)
{
    ((pxThread *)handle)->_ThreadCleanup();
}


Threading::pxThread::pxThread(const wxString &name)
    : m_name(name)
    , m_thread()
    , m_detached(true) // start out with m_thread in detached/invalid state
    , m_running(false)
{
}

// This destructor performs basic "last chance" cleanup, which is a blocking join
// against the thread. Extending classes should almost always implement their own
// thread closure process, since any pxThread will, by design, not terminate
// unless it has been properly canceled (resulting in deadlock).
//
// Thread safety: This class must not be deleted from its own thread.  That would be
// like marrying your sister, and then cheating on her with your daughter.
Threading::pxThread::~pxThread()
{
    try {
        if (m_running) {
            m_mtx_InThread.Wait();
        }
        Threading::Sleep(1);
        Detach();
    }
    DESTRUCTOR_CATCHALL
}

bool Threading::pxThread::AffinityAssert_AllowFromSelf(const DiagnosticOrigin &origin) const
{
    return IsSelf();
}

bool Threading::pxThread::AffinityAssert_DisallowFromSelf(const DiagnosticOrigin &origin) const
{
    if (!IsSelf())
        return true;
    return false;
}

void Threading::pxThread::FrankenMutex(Mutex &mutex)
{
    if (mutex.RecreateIfLocked()) {
        // Our lock is bupkis, which means  the previous thread probably deadlocked.
        // Let's create a new mutex lock to replace it.
    }
}

// Main entry point for starting or e-starting a persistent thread.  This function performs necessary
// locks and checks for avoiding race conditions, and then calls OnStart() immediately before
// the actual thread creation.  Extending classes should generally not override Start(), and should
// instead override DoPrepStart instead.
//
// This function should not be called from the owner thread.
void Threading::pxThread::Start()
{
    // Prevents sudden parallel startup, and or parallel startup + cancel:
    ScopedLock startlock(m_mtx_start);
    if (m_running) {
        return;
    }

    Detach(); // clean up previous thread handle, if one exists.
    OnStart();

    m_except = NULL;

    if (pthread_create(&m_thread, NULL, _internal_callback, this) != 0)
        throw Exception::ThreadCreationError(this).SetDiagMsg(L"Thread creation error: " + wxString(std::strerror(errno)));

#ifdef ASAN_WORKAROUND
    // Recent Asan + libc6 do pretty bad stuff on the thread init => https://gcc.gnu.org/bugzilla/show_bug.cgi?id=77982
    //
    // In our case, the semaphore was posted (counter is 1) but thread is still
    // waiting...  So waits 100ms and checks the counter value manually
    if (!m_sem_startup.WaitWithoutYield(wxTimeSpan(0, 0, 0, 100))) {
        if (m_sem_startup.Count() == 0)
            throw Exception::ThreadCreationError(this).SetDiagMsg(L"Thread creation error: %s thread never posted startup semaphore.");
    }
#else
    if (!m_sem_startup.WaitWithoutYield(wxTimeSpan(0, 0, 3, 0))) {
        RethrowException();

        // And if the thread threw nothing of its own:
        throw Exception::ThreadCreationError(this).SetDiagMsg(L"Thread creation error: %s thread never posted startup semaphore.");
    }
#endif

    // Event Rationale (above): Performing this semaphore wait on the created thread is "slow" in the
    // sense that it stalls the calling thread completely until the new thread is created
    // (which may not always be desirable).  But too bad.  In order to safely use 'running' locks
    // and detachment management, this *has* to be done.  By rule, starting new threads shouldn't
    // be done very often anyway, hence the concept of Threadpooling for rapidly rotating tasks.
    // (and indeed, this semaphore wait might, in fact, be very swift compared to other kernel
    // overhead in starting threads).

    // (this could also be done using operating system specific calls, since any threaded OS has
    // functions that allow us to see if a thread is running or not, and to block against it even if
    // it's been detached -- removing the need for m_mtx_InThread and the semaphore wait above.  But
    // pthreads kinda lacks that stuff, since pthread_join() has no timeout option making it im-
    // possible to safely block against a running thread)
}

// Returns: TRUE if the detachment was performed, or FALSE if the thread was
// already detached or isn't running at all.
// This function should not be called from the owner thread.
bool Threading::pxThread::Detach()
{
    AffinityAssert_DisallowFromSelf(pxDiagSpot);

    if (m_detached.exchange(true))
        return false;
    pthread_detach(m_thread);
    return true;
}

bool Threading::pxThread::_basecancel()
{
    if (!m_running)
        return false;

    if (m_detached)
        return false;

    pthread_cancel(m_thread);
    return true;
}

// Remarks:
//   Provision of non-blocking Cancel() is probably academic, since destroying a pxThread
//   object performs a blocking Cancel regardless of if you explicitly do a non-blocking Cancel()
//   prior, since the ExecuteTaskInThread() method requires a valid object state.  If you really need
//   fire-and-forget behavior on threads, use pthreads directly for now.
//
// This function should not be called from the owner thread.
//
// Parameters:
//   isBlocking - indicates if the Cancel action should block for thread completion or not.
//
// Exceptions raised by the blocking thread will be re-thrown into the main thread.  If isBlocking
// is false then no exceptions will occur.
//
void Threading::pxThread::Cancel(bool isBlocking)
{
    AffinityAssert_DisallowFromSelf(pxDiagSpot);

    // Prevent simultaneous startup and cancel, necessary to avoid
    ScopedLock startlock(m_mtx_start);

    if (!_basecancel())
        return;

    if (isBlocking) {
        WaitOnSelf(m_mtx_InThread);
        Detach();
    }
}

bool Threading::pxThread::Cancel(const wxTimeSpan &timespan)
{
    AffinityAssert_DisallowFromSelf(pxDiagSpot);

    // Prevent simultaneous startup and cancel:
    ScopedLock startlock(m_mtx_start);

    if (!_basecancel())
        return true;

    if (!WaitOnSelf(m_mtx_InThread, timespan))
        return false;
    Detach();
    return true;
}

bool Threading::pxThread::IsSelf() const
{
    // Detached threads may have their pthread handles recycled as newer threads, causing
    // false IsSelf reports.
    return !m_detached && (pthread_self() == m_thread);
}

bool Threading::pxThread::IsRunning() const
{
    return m_running;
}

// Throws an exception if the thread encountered one.  Uses the BaseException's Rethrow() method,
// which ensures the exception type remains consistent.  Debuggable stacktraces will be lost, since
// the thread will have allowed itself to terminate properly.
void Threading::pxThread::RethrowException() const
{
    // Thread safety note: always detach the m_except pointer.  If we checked it for NULL, the
    // pointer might still be invalid after detachment, so might as well just detach and check
    // after.

    ScopedExcept ptr(const_cast<pxThread *>(this)->m_except.DetachPtr());
    if (ptr)
        ptr->Rethrow();
}

// This helper function is a deadlock-safe method of waiting on a semaphore in a pxThread.  If the
// thread is terminated or canceled by another thread or a nested action prior to the semaphore being
// posted, this function will detect that and throw a CancelEvent exception is thrown.
//
// Note: Use of this function only applies to semaphores which are posted by the worker thread.  Calling
// this function from the context of the thread itself is an error, and a dev assertion will be generated.
//
// Exceptions:
//   This function will rethrow exceptions raised by the persistent thread, if it throws an error
//   while the calling thread is blocking (which also means the persistent thread has terminated).
//
void Threading::pxThread::WaitOnSelf(Semaphore &sem) const
{
    if (!AffinityAssert_DisallowFromSelf(pxDiagSpot))
        return;

    while (true) {
        if (sem.WaitWithoutYield(wxTimeSpan(0, 0, 0, 333)))
            return;
	if (HasPendingException())
		RethrowException();
    }
}

// This helper function is a deadlock-safe method of waiting on a mutex in a pxThread.
// If the thread is terminated or canceled by another thread or a nested action prior to the
// mutex being unlocked, this function will detect that and a CancelEvent exception is thrown.
//
// Note: Use of this function only applies to mutexes which are acquired by a worker thread.
// Calling this function from the context of the thread itself is an error, and a dev assertion
// will be generated.
//
// Exceptions:
//   This function will rethrow exceptions raised by the persistent thread, if it throws an
//   error while the calling thread is blocking (which also means the persistent thread has
//   terminated).
//
void Threading::pxThread::WaitOnSelf(Mutex &mutex) const
{
    if (!AffinityAssert_DisallowFromSelf(pxDiagSpot))
        return;

    while (true) {
        if (mutex.WaitWithoutYield(wxTimeSpan(0, 0, 0, 333)))
            return;
	if (HasPendingException())
		RethrowException();
    }
}

static const wxTimeSpan SelfWaitInterval(0, 0, 0, 333);

bool Threading::pxThread::WaitOnSelf(Semaphore &sem, const wxTimeSpan &timeout) const
{
    if (!AffinityAssert_DisallowFromSelf(pxDiagSpot))
        return true;

    wxTimeSpan runningout(timeout);

    while (runningout.GetMilliseconds() > 0) {
        const wxTimeSpan interval((SelfWaitInterval < runningout) ? SelfWaitInterval : runningout);
        if (sem.WaitWithoutYield(interval))
            return true;
	if (HasPendingException())
		RethrowException();
        runningout -= interval;
    }
    return false;
}

bool Threading::pxThread::WaitOnSelf(Mutex &mutex, const wxTimeSpan &timeout) const
{
    if (!AffinityAssert_DisallowFromSelf(pxDiagSpot))
        return true;

    wxTimeSpan runningout(timeout);

    while (runningout.GetMilliseconds() > 0) {
        const wxTimeSpan interval((SelfWaitInterval < runningout) ? SelfWaitInterval : runningout);
        if (mutex.WaitWithoutYield(interval))
            return true;
	if (HasPendingException())
		RethrowException();
        runningout -= interval;
    }
    return false;
}

// Inserts a thread cancellation point.  If the thread has received a cancel request, this
// function will throw an SEH exception designed to exit the thread (so make sure to use C++
// object encapsulation for anything that could leak resources, to ensure object unwinding
// and cleanup, or use the DoThreadCleanup() override to perform resource cleanup).
void Threading::pxThread::TestCancel() const
{
    AffinityAssert_AllowFromSelf(pxDiagSpot);
    pthread_testcancel();
}

// Executes the virtual member method
void Threading::pxThread::_try_virtual_invoke(void (pxThread::*method)())
{
    try {
        (this->*method)();
    }

    // ----------------------------------------------------------------------------
    // Neat repackaging for STL Runtime errors...
    //
    catch (std::runtime_error &ex) {
        m_except = new Exception::RuntimeError(ex, WX_STR(GetName()));
    }

    // ----------------------------------------------------------------------------
    catch (Exception::RuntimeError &ex) {
        BaseException *woot = ex.Clone();
        woot->DiagMsg() += pxsFmt(L"(thread:%s)", WX_STR(GetName()));
        m_except = woot;
    }
    // BaseException --  same deal as LogicErrors.
    //
    catch (BaseException &ex) {
        BaseException *woot = ex.Clone();
        woot->DiagMsg() += pxsFmt(L"(thread:%s)", WX_STR(GetName()));
        m_except = woot;
    }
}

// invoked internally when canceling or exiting the thread.  Extending classes should implement
// OnCleanupInThread() to extend cleanup functionality.
void Threading::pxThread::_ThreadCleanup()
{
    AffinityAssert_AllowFromSelf(pxDiagSpot);
    _try_virtual_invoke(&pxThread::OnCleanupInThread);
    m_mtx_InThread.Release();

    // Must set m_running LAST, as thread destructors depend on this value (it is used
    // to avoid destruction of the thread until all internal data use has stopped.
    m_running = false;
}

wxString Threading::pxThread::GetName() const
{
    ScopedLock lock(m_mtx_ThreadName);
    return m_name;
}

// This override is called by PeristentThread when the thread is first created, prior to
// calling ExecuteTaskInThread, and after the initial InThread lock has been claimed.
// This code is also executed within a "safe" environment, where the creating thread is
// blocked against m_sem_event.  Make sure to do any necessary variable setup here, without
// worry that the calling thread might attempt to test the status of those variables
// before initialization has completed.
//
void Threading::pxThread::OnStartInThread()
{
    m_detached = false;
    m_running = true;
}

void Threading::pxThread::_internal_execute()
{
    m_mtx_InThread.Acquire();

    make_curthread_key(this);
    if (curthread_key)
        pthread_setspecific(curthread_key, this);

    OnStartInThread();
    m_sem_startup.Post();

    _try_virtual_invoke(&pxThread::ExecuteTaskInThread);
}

// Called by Start, prior to actual starting of the thread, and after any previous
// running thread has been canceled or detached.
void Threading::pxThread::OnStart()
{
    FrankenMutex(m_mtx_InThread);
    m_sem_event.Reset();
    m_sem_startup.Reset();
}

// Extending classes that override this method should always call it last from their
// personal implementations.
void Threading::pxThread::OnCleanupInThread()
{
    if (curthread_key)
        pthread_setspecific(curthread_key, NULL);

    unmake_curthread_key();
    m_evtsrc_OnDelete.Dispatch(0);
}

// passed into pthread_create, and is used to dispatch the thread's object oriented
// callback function
void *Threading::pxThread::_internal_callback(void *itsme)
{
    if (!pxAssertDev(itsme != NULL))
        return NULL;

    internal_callback_helper(itsme);
    return nullptr;
}

// __try is used in pthread_cleanup_push when CLEANUP_SEH is used as the cleanup model.
// That can't be used in a function that has objects that require unwinding (compile
// error C2712), so move it into a separate function.
void Threading::pxThread::internal_callback_helper(void *itsme)
{
    pxThread &owner = *static_cast<pxThread *>(itsme);

    pthread_cleanup_push(_pt_callback_cleanup, itsme);
    owner._internal_execute();
    pthread_cleanup_pop(true);
}

// --------------------------------------------------------------------------------------
//  BaseThreadError
// --------------------------------------------------------------------------------------

wxString Exception::BaseThreadError::FormatDiagnosticMessage() const
{
    wxString null_str(L"Null Thread Object");
    return pxsFmt(m_message_diag, (m_thread == NULL) ? WX_STR(null_str) : WX_STR(m_thread->GetName()));
}

pxThread &Exception::BaseThreadError::Thread()
{
    pxAssertDev(m_thread != NULL);
    return *m_thread;
}
const pxThread &Exception::BaseThreadError::Thread() const
{
    pxAssertDev(m_thread != NULL);
    return *m_thread;
}
