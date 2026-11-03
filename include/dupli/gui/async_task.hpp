#pragma once

#include <wx/event.h>
#include <wx/thread.h>

#include <functional>
#include <string>

namespace dupli {
namespace gui {

/// Posted once when an AsyncTask finishes (successfully or not). The event
/// carries no payload: the owning tab keeps the result in a shared_ptr the worker
/// filled in, which avoids copying large results through the event queue.
wxDECLARE_EVENT(EVT_DUPLI_ASYNC_DONE, wxThreadEvent);

/// Runs one callable on a joinable worker thread.
///
/// Rules that keep the UI safe:
///  * The callable must never touch a wxWidgets control. It may use wxImage or
///    other non-control classes, and it must write results into memory the tab
///    owns (normally a shared_ptr captured by value).
///  * Exactly one EVT_DUPLI_ASYNC_DONE is posted to the sink, even when the
///    callable throws, so the tab always leaves its "busy" state.
///  * The owner must Wait() on the thread before destroying widgets or the
///    object. The main frame refuses to close while any tab reports busy work.
class AsyncTask : public wxThread {
public:
    AsyncTask(wxEvtHandler* sink, std::function<void()> work)
        : wxThread(wxTHREAD_JOINABLE), m_sink(sink), m_work(std::move(work)) {}

    ~AsyncTask() override = default;

    ExitCode Entry() override;

    /// Error text from the callable, if it threw. Only read after Wait().
    const std::string& error() const { return m_error; }

private:
    wxEvtHandler* m_sink;
    std::function<void()> m_work;
    std::string m_error;
};

} // namespace gui
} // namespace dupli
