#pragma once
#include <deque>
#include <fstream>
#include <mutex>
#include <condition_variable>

#include "../defines.hpp"
#include "../helpers/MiscFunctions.hpp"

struct SHyprIPCEvent {
    std::string event;
    std::string data;
};

class CEventManager {
  public:
    CEventManager() = default;
    ~CEventManager();

    void        postEvent(const SHyprIPCEvent event, bool force = false);

    void        start();

    bool        m_bIgnoreEvents = false;

    void        stop();

  private:
    void                                         flushEvents();

    mutable std::mutex                           m_mEventQueue;
    std::condition_variable m_cvEvents;
    std::deque<SHyprIPCEvent>                    m_dQueuedEvents;

    std::deque<std::pair<int, wl_event_source*>> m_dAcceptedSocketFDs;
    int m_sEvent;
    std::thread m_tServerThread;
    std::thread m_tQueueThread;
    bool m_bIsRunning = false;
};

inline std::unique_ptr<CEventManager> g_pEventManager;
