#pragma once

#include "options.h"
#include "stats.h"

#include <cloud/storage/core/libs/diagnostics/public.h>

#include <functional>
#include <memory>
#include <span>

namespace NCloud::NBlockStore::NVHostServer {

////////////////////////////////////////////////////////////////////////////////

struct IServer
{
    virtual ~IServer() = default;

    virtual void Start(const TOptions& options) = 0;
    virtual void Stop() = 0;
    virtual TSimpleStats GetStats() = 0;
};

////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<IServer> CreateServer(ILoggingServicePtr logging);

}   // namespace NCloud::NBlockStore::NVHostServer
