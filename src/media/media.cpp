#include "media.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt::Windows::Media::Control;

namespace {

using Manager = GlobalSystemMediaTransportControlsSessionManager;
using Session = GlobalSystemMediaTransportControlsSession;
using PlaybackStatus = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

std::thread g_thread;
std::atomic<bool> g_stopping;
/* Set by WinRT events (on pool threads); the worker then re-reads everything.
 * Doing the reads on one thread keeps blocking calls out of event callbacks. */
HANDLE g_wake;
HWND g_notify;
UINT g_message;

std::mutex g_lock;
mora_media_t g_media;

std::atomic<int> g_command{ -1 };
std::atomic<bool> g_sessions_changed;

/* Players report a command's effect after it returns, and not always with an
 * event, so the worker re-reads this many times, this far apart, after one. */
constexpr int SETTLE_READS = 8;
constexpr DWORD SETTLE_INTERVAL_MS = 125;

void copy(wchar_t (&out)[MORA_MEDIA_TEXT_CAPACITY], const winrt::hstring& text)
{
    wcsncpy_s(out, text.c_str(), _TRUNCATE);
}

/* Prefers the session Windows considers current, if it is playing, then any
 * other playing session, then the current one if paused. `chosen` is set to
 * the session described, so commands reach what the bar shows. */
mora_media_t read(const Manager& manager, Session& chosen)
{
    mora_media_t media = {};

    chosen = nullptr;
    Session current = manager.GetCurrentSession();

    std::vector<Session> candidates;

    if (current) {
        candidates.push_back(current);
    }

    for (const Session& session : manager.GetSessions()) {
        candidates.push_back(session);
    }

    if (current) {
        candidates.push_back(current);
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        const Session& session = candidates[i];
        bool last = current && i + 1 == candidates.size();
        auto info = session.GetPlaybackInfo();

        if (!info) {
            continue;
        }

        auto status = info.PlaybackStatus();

        if (status != PlaybackStatus::Playing && !(last && status == PlaybackStatus::Paused)) {
            continue;
        }

        auto properties = session.TryGetMediaPropertiesAsync().get();

        if (!properties || properties.Title().empty()) {
            continue;
        }

        media.active = TRUE;
        media.playing = status == PlaybackStatus::Playing;
        copy(media.title, properties.Title());
        copy(media.artist, properties.Artist());
        chosen = session;
        break;
    }

    return media;
}

void run_command(const Session& session, int command)
{
    if (!session) {
        return;
    }

    switch (command) {
    case MORA_MEDIA_PREVIOUS: session.TrySkipPreviousAsync().get(); break;

    case MORA_MEDIA_TOGGLE: session.TryTogglePlayPauseAsync().get(); break;

    case MORA_MEDIA_NEXT: session.TrySkipNextAsync().get(); break;
    }
}

void run()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
        Manager manager = Manager::RequestAsync().get();
        auto wake = [](auto&&...) { SetEvent(g_wake); };

        /* Sessions come and go, so their subscriptions are rebuilt when the
         * list changes; the revokers unsubscribe when replaced. Rebuilding on
         * every wake would drop events raised while unsubscribed. */
        std::vector<Session::MediaPropertiesChanged_revoker> properties;
        std::vector<Session::PlaybackInfoChanged_revoker> playback;
        g_sessions_changed = true;
        auto sessions_changed = manager.SessionsChanged(winrt::auto_revoke, [](auto&&...) {
            g_sessions_changed = true;
            SetEvent(g_wake);
        });
        auto current_changed = manager.CurrentSessionChanged(winrt::auto_revoke, wake);

        Session chosen = nullptr;
        int settle = 0;

        while (!g_stopping) {
            int command = g_command.exchange(-1);

            if (command >= 0) {
                try {
                    run_command(chosen, command);
                } catch (const winrt::hresult_error&) {
                    /* The player refused or went away; nothing to undo. */
                }

                settle = SETTLE_READS;
            }

            if (g_sessions_changed.exchange(false)) {
                properties.clear();
                playback.clear();

                for (const Session& session : manager.GetSessions()) {
                    properties.push_back(session.MediaPropertiesChanged(winrt::auto_revoke, wake));
                    playback.push_back(session.PlaybackInfoChanged(winrt::auto_revoke, wake));
                }
            }

            mora_media_t media = {};

            try {
                media = read(manager, chosen);
            } catch (const winrt::hresult_error&) {
                /* A session closed mid-read; the next event reads again. */
            }

            bool changed;

            {
                std::lock_guard<std::mutex> guard(g_lock);
                changed = memcmp(&media, &g_media, sizeof(media)) != 0;
                g_media = media;
            }

            if (changed) {
                PostMessageW(g_notify, g_message, 0, 0);
            }

            if (settle > 0) {
                --settle;
                WaitForSingleObject(g_wake, SETTLE_INTERVAL_MS);
            } else {
                WaitForSingleObject(g_wake, INFINITE);
            }
        }
    } catch (const winrt::hresult_error&) {
        /* No media controls on this system; nothing is ever shown as playing. */
    }

    winrt::uninit_apartment();
}

} // namespace

extern "C" BOOL mora_media_start(HWND notify, UINT message)
{
    if (g_thread.joinable()) {
        return FALSE;
    }

    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (!g_wake) {
        return FALSE;
    }

    g_notify = notify;
    g_message = message;
    g_stopping = false;
    g_thread = std::thread(run);

    return TRUE;
}

extern "C" void mora_media_get(mora_media_t* media)
{
    std::lock_guard<std::mutex> guard(g_lock);
    *media = g_media;
}

extern "C" void mora_media_command(mora_media_command_t command)
{
    if (!g_thread.joinable()) {
        return;
    }

    g_command = command;
    SetEvent(g_wake);
}

extern "C" void mora_media_stop(void)
{
    if (!g_thread.joinable()) {
        return;
    }

    g_stopping = true;
    SetEvent(g_wake);
    g_thread.join();
    CloseHandle(g_wake);
    g_wake = nullptr;
    std::lock_guard<std::mutex> guard(g_lock);
    g_media = {};
}
