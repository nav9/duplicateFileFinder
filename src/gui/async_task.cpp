#include "dupli/gui/async_task.hpp"

#include <exception>
#include <string>

namespace dupli {
namespace gui {

wxDEFINE_EVENT(EVT_DUPLI_ASYNC_DONE, wxThreadEvent);

wxThread::ExitCode AsyncTask::Entry() {
    try {
        if (m_work) m_work();
    } catch (const std::exception& error) {
        // A worker must never let an exception escape into wxWidgets' C code.
        m_error = std::string("Unexpected failure: ") + error.what();
    } catch (...) {
        m_error = "Unexpected failure";
    }

    // The event is queued even on failure so the tab can reset its controls.
    wxQueueEvent(m_sink, new wxThreadEvent(EVT_DUPLI_ASYNC_DONE));
    return static_cast<wxThread::ExitCode>(nullptr);
}

} // namespace gui
} // namespace dupli
