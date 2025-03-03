#pragma once
#include <contrib/ydb/core/base/events.h>

#include <library/cpp/actors/core/events.h>

namespace NKikimr::NBackgroundTasks {

enum EEvents {
    EvAddTask = EventSpaceBegin(TKikimrEvents::ES_BACKGROUND_TASKS),
    EvStartAssign,
    EvAssignFinished,
    EvFetchingFinished,
    EvTaskFinished,
    EvTaskInterrupted,
    EvTaskFetched,
    EvTaskExecutorFinished,
    EvUpdateTaskEnabled,
    EvAddTaskResult,
    EvUpdateTaskEnabledResult,
    EvLockPingerStart,
    EvLockPingerFinished,
    EvEnd
};

static_assert(EEvents::EvEnd < EventSpaceEnd(TKikimrEvents::ES_BACKGROUND_TASKS), "expect EvEnd < EventSpaceEnd(TKikimrEvents::ES_BACKGROUND_TASKS)");

}
