#include "EventManager.hpp"
#include "../Compositor.hpp"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

CEventManager::~CEventManager() { stop(); }

int fdHandleWrite(int fd, uint32_t mask, void* data) {
    if (mask & WL_EVENT_ERROR || mask & WL_EVENT_HANGUP) {
	Debug::log(ERR, "FD: %d, mask: %lu, ERROR=%lu, HANGUP=%lu", fd, mask, WL_EVENT_ERROR, WL_EVENT_HANGUP);
        // remove, hanged up
        const auto ACCEPTEDFDS = (std::deque<std::pair<int, wl_event_source*>>*)data;
        for (auto it = ACCEPTEDFDS->begin(); it != ACCEPTEDFDS->end();) {
            if (it->first == fd) {
		    // abort inside!
                wl_event_source_remove(it->second); // remove this fd listener
                it = ACCEPTEDFDS->erase(it);
            } else {
                it++;
            }
        }
    }

    return 0;
}

void CEventManager::start() {
	m_bIsRunning = true;
m_tQueueThread = std::thread( &CEventManager::flushEvents, this);

    m_tServerThread = std::thread([&]() {
        m_sEvent = socket(AF_UNIX, SOCK_STREAM, 0);

        if (m_sEvent < 0) {
            Debug::log(ERR, "Couldn't start the Hyprland Socket 2. (1) IPC will not work.");
            return;
        }

	fcntl(m_sEvent, F_SETFD, FD_CLOEXEC);
        sockaddr_un SERVERADDRESS = {.sun_family = AF_UNIX};
        std::string socketPath    = "/tmp/hypr/" + g_pCompositor->m_szInstanceSignature + "/.socket2.sock";
        strcpy(SERVERADDRESS.sun_path, socketPath.c_str());

        bind(m_sEvent, (sockaddr*)&SERVERADDRESS, SUN_LEN(&SERVERADDRESS));

        // 10 max queued.
        listen(m_sEvent, 10);

        sockaddr_in clientAddress;
        socklen_t   clientSize = sizeof(clientAddress);

        Debug::log(LOG, "Hypr socket 2 started at %s", socketPath.c_str());

        while (m_bIsRunning) {
            const int sConnection = accept4(m_sEvent, (sockaddr*)&clientAddress, &clientSize, SOCK_CLOEXEC);

            if (sConnection > 0) {
                // new connection!

                int flagsNew = fcntl(sConnection, F_GETFL, 0);
                fcntl(sConnection, F_SETFL, flagsNew | O_NONBLOCK);
		fcntl(sConnection, F_SETFD, FD_CLOEXEC);

                Debug::log(LOG, "Socket 2 accepted a new client at FD %d", sConnection);

                // add to event loop so we can close it when we need to
                m_dAcceptedSocketFDs.push_back(
                    {sConnection, wl_event_loop_add_fd(g_pCompositor->m_sWLEventLoop, sConnection, WL_EVENT_READABLE, fdHandleWrite, &m_dAcceptedSocketFDs)});
            }
        }

        close(m_sEvent);

	Debug::log(LOG, "Event thread terminated");
    });
}

void CEventManager::flushEvents() {
	// notify subscribers
	while (m_bIsRunning)
	{
std::deque<SHyprIPCEvent> dQueuedEvents;
	{
    std::unique_lock ul(m_mEventQueue);
		m_cvEvents.wait(ul, [&](){return !(m_dQueuedEvents.empty() && m_bIsRunning);});
    dQueuedEvents.swap(m_dQueuedEvents);
	}

    for (auto& ev : dQueuedEvents) {
        std::string eventString = (ev.event + ">>" + ev.data).substr(0, 1022) + "\n";
        for (auto& fd : m_dAcceptedSocketFDs) {
            write(fd.first, eventString.c_str(), eventString.length());
        }
    }
	}
}

void CEventManager::postEvent(const SHyprIPCEvent event, bool force) {

    if ((m_bIgnoreEvents && !force) || g_pCompositor->m_bIsShuttingDown) {
        Debug::log(WARN, "Suppressed (ignoreevents true / shutting down) event of type %s, content: %s", event.event.c_str(), event.data.c_str());
        return;
    }

    {
	std::unique_lock _(m_mEventQueue);
	m_dQueuedEvents.emplace_back(event);
    }

    m_cvEvents.notify_one();
}

void CEventManager::stop() {
	shutdown(m_sEvent, SHUT_RD);
	m_bIsRunning = false;
	m_cvEvents.notify_all();

	if (m_tServerThread.joinable())
	{
		m_tServerThread.join();
	}

	if (m_tQueueThread.joinable())
	{
		m_tQueueThread.join();
	}
}
