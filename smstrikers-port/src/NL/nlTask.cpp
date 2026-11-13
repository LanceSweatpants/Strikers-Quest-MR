#include "NL/nlTask.h"
#include "NL/nlMemory.h"
#include "NL/nlTicker.h"
#include "NL/nlDLRing.h"
#include "port/framerate.h" // PORT: PortTaskClockFrame
#include "port/xr_net.h"    // strikers-quest: the online match's fixed step
#include "port/host.h"      // strikers-quest r222: port_monotonic_ns (the frame probe's task times)
#include "port/xr_quest.h"  // strikers-quest r222: PortXrProbeTasks

#define assert(condition) ((condition) ? ((void)0) : ((void)0))

u8 g_StackWatermarkFiller = 0x78;
float g_fTaskTimeUpperBound = 0.1f;

nlTaskManager* nlTaskManager::m_pInstance = nullptr;
u8 g_DoStackWatermarkTests;
float g_fTaskTimeLowerBound;

/**
 * Offset/Address/Size: 0x0 | 0x801D28FC | size: 0xC
 */
void nlTaskManager::SetTimeDilation(float timeDilation)
{
    m_pInstance->m_TimeDilation = timeDilation;
}

/**
 * Offset/Address/Size: 0xC | 0x801D2908 | size: 0xC
 */
void nlTaskManager::SetNextState(unsigned int nextState)
{
    m_pInstance->m_PendingState = nextState;
}

/**
 * Offset/Address/Size: 0x18 | 0x801D2914 | size: 0x150
 */
void nlTaskManager::RunAllTasks()
{
    f32 tickerDifference;
    f32 deltaTime;
    f32 clampedDeltaTime;
    nlTask* currentTask;
    nlTask* taskIterator;
    s32 currentTicker;

    currentTask = nlDLRingGetStart<nlTask>(m_pInstance->m_lTaskList);
    if (currentTask != NULL)
    {
        // strikers-quest r222: the frame probe (xr_quest.cpp gp, PortXrProbeTasks) - the state change's time and the
        // slowest task's Run this frame, by name. Measures only: nothing here changes what runs or in which order.
        unsigned long long probeTransNs = 0;
        u32 probeFrom = 0, probeTo = 0;
        unsigned long long probeSlowNs = 0;
        const char* probeSlow = NULL;
        if ((u32)m_pInstance->m_CurrState != (u32)m_pInstance->m_PendingState)
        {
            const unsigned long long t0 = port_monotonic_ns();
            probeFrom = (u32)m_pInstance->m_CurrState;
            probeTo = (u32)m_pInstance->m_PendingState;
            for (;;)
            {
                currentTask->StateTransition(m_pInstance->m_CurrState, m_pInstance->m_PendingState);
                if (nlDLRingIsEnd<nlTask>(m_pInstance->m_lTaskList, currentTask) != 0)
                    break;
                currentTask = currentTask->m_next;
            }
            m_pInstance->m_PrevState = (u32)m_pInstance->m_CurrState;
            m_pInstance->m_CurrState = (u32)m_pInstance->m_PendingState;
            probeTransNs = port_monotonic_ns() - t0;
        }

        taskIterator = nlDLRingGetStart<nlTask>(m_pInstance->m_lTaskList);
        // PORT: tasks step by whole display periods while vsync paces the frame; when the clock changes, each steps by the clock it last recorded.
        u32 frameTicker = 0;
        const int clock = PortTaskClockFrame(&frameTicker);
        const bool stepByDisplay = clock == PORT_TASK_CLOCK_DISPLAY || clock == PORT_TASK_CLOCK_LEAVING;
        const bool recordDisplay = clock == PORT_TASK_CLOCK_DISPLAY || clock == PORT_TASK_CLOCK_ENTERING;
        for (;;)
        {
            const s32 hostTicker = nlGetTicker();
            currentTicker = stepByDisplay ? (s32)frameTicker : hostTicker;
            tickerDifference = nlGetTickerDifference(taskIterator->nPrevTicker, currentTicker);
            taskIterator->nPrevTicker = recordDisplay ? (s32)frameTicker : hostTicker;
            if (taskIterator->statesActive & m_pInstance->m_CurrState)
            {
                clampedDeltaTime = tickerDifference / 1000.f;
                // strikers-quest: online, every task steps exactly one 60 Hz frame on both headsets.
                if (PortNetOnline())
                    clampedDeltaTime = 1.0f / 60.0f;
                if (clampedDeltaTime < g_fTaskTimeLowerBound)
                {
                    clampedDeltaTime = g_fTaskTimeLowerBound;
                }
                else if (clampedDeltaTime > g_fTaskTimeUpperBound)
                {
                    clampedDeltaTime = g_fTaskTimeUpperBound;
                }
                deltaTime = clampedDeltaTime * m_pInstance->m_TimeDilation;
                m_pInstance->m_fCurrentTimeDelta = deltaTime;
                const unsigned long long r0 = port_monotonic_ns(); // strikers-quest r222 (the probe)
                taskIterator->Run(deltaTime);
                const unsigned long long rNs = port_monotonic_ns() - r0;
                if (rNs > probeSlowNs)
                {
                    probeSlowNs = rNs;
                    probeSlow = taskIterator->GetName(); // (each a string literal; read right after its own Run)
                }
            }
            if (taskIterator == m_pInstance->m_lTaskList)
                break;
            taskIterator = taskIterator->m_next;
        }
        PortXrProbeTasks(probeSlow, probeSlowNs, probeTransNs, probeFrom, probeTo); // strikers-quest r222
    }
}

/**
 * Offset/Address/Size: 0x168 | 0x801D2A64 | size: 0xC4
 */
void nlTaskManager::AddTask(nlTask* task, unsigned int priority, unsigned int statesActive)
{
    task->nPriority = priority;
    task->statesActive = statesActive;
    // PORT: the clock RunAllTasks recorded this frame, so a task added mid-frame starts on it.
    u32 ticker;
    if (!PortTaskClockCurrent(&ticker))
        ticker = nlGetTicker();
    task->nPrevTicker = ticker;

    if (m_pInstance->m_lTaskList == nullptr)
    {
        nlDLRingAddStart<nlTask>(&m_pInstance->m_lTaskList, task);
        return;
    }

    // Find the appropriate position to insert the task based on priority
    nlTask* currentTask = nlDLRingGetStart<nlTask>(m_pInstance->m_lTaskList);
    while (currentTask != nullptr)
    {
        if (currentTask->nPriority >= priority)
        {
            currentTask = currentTask->m_prev;
            break;
        }
        else if (!nlDLRingIsEnd<nlTask>(m_pInstance->m_lTaskList, currentTask))
        {
            currentTask = currentTask->m_next;
        }
        else
        {
            break;
        }
    }

    nlDLRingInsert<nlTask>(&m_pInstance->m_lTaskList, currentTask, task);
}

/**
 * Offset/Address/Size: 0x22C | 0x801D2B28 | size: 0x74
 */
void nlTaskManager::Startup(unsigned int initialState)
{
    m_pInstance = new (8, false) nlTaskManager;
    m_pInstance->m_PrevState = initialState;
    m_pInstance->m_CurrState = initialState;
    m_pInstance->m_PendingState = initialState;
    m_pInstance->m_lTaskList = nullptr; // Initialize task ring head to null
    m_pInstance->m_TimeDilation = 1.0f;
    m_pInstance->m_Locked = 0;
}
