#pragma once

#include "private.h"

#include <cloud/blockstore/libs/common/public.h>
#include <cloud/blockstore/libs/diagnostics/public.h>
#include <cloud/blockstore/libs/kikimr/public.h>
#include <cloud/blockstore/libs/nvme/public.h>
#include <cloud/blockstore/libs/rdma/iface/public.h>
#include <cloud/blockstore/libs/service/public.h>
#include <cloud/blockstore/libs/service_local/public.h>
#include <cloud/blockstore/libs/spdk/iface/public.h>
#include <cloud/blockstore/libs/storage/disk_agent/public.h>

#include <contrib/ydb/core/driver_lib/run/factories.h>

#include <library/cpp/actors/util/should_continue.h>
#include <library/cpp/logger/log.h>

namespace NCloud::NBlockStore::NServer {

////////////////////////////////////////////////////////////////////////////////

struct TConfigInitializer;

////////////////////////////////////////////////////////////////////////////////

struct TSpdkParts
{
    NSpdk::ISpdkEnvPtr Env;
    std::function<void(TLog& log)> LogInitializer;
};

struct TServerModuleFactories
{
    std::function<TSpdkParts(NSpdk::TSpdkEnvConfigPtr config)> SpdkFactory;
    std::function<NRdma::IServerPtr(
        ILoggingServicePtr logging,
        IMonitoringServicePtr monitoring,
        NRdma::TServerConfigPtr config)> RdmaServerFactory;
};

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
{
private:
    std::shared_ptr<NKikimr::TModuleFactories> ModuleFactories;
    std::shared_ptr<TServerModuleFactories> ServerModuleFactories;

    std::unique_ptr<TConfigInitializer> Configs;

    ILoggingServicePtr BootstrapLogging;
    TLog Log;

    ITimerPtr Timer;
    ISchedulerPtr Scheduler;
    IActorSystemPtr ActorSystem;
    ILoggingServicePtr Logging;
    IAsyncLoggerPtr AsyncLogger;
    IMonitoringServicePtr Monitoring;
    TVector<ITraceReaderPtr> TraceReaders;
    ITraceProcessorPtr TraceProcessor;
    IProfileLogPtr ProfileLog;
    IBlockDigestGeneratorPtr BlockDigestGenerator;
    IFileIOServicePtr FileIOService;
    NSpdk::ISpdkEnvPtr Spdk;
    std::function<void(TLog& log)> SpdkLogInitializer;
    ICachingAllocatorPtr Allocator;
    IStorageProviderPtr AioStorageProvider;
    NNvme::INvmeManagerPtr NvmeManager;
    NRdma::IServerPtr RdmaServer;

    TProgramShouldContinue ShouldContinue;
    TVector<TString> PostponedCriticalEvents;

public:
    TBootstrap(
        std::shared_ptr<NKikimr::TModuleFactories> moduleFactories,
        std::shared_ptr<TServerModuleFactories> serverModuleFactories);
    ~TBootstrap();

    void ParseOptions(int argc, char** argv);
    void Init();

    void Start();
    void Stop();

    TProgramShouldContinue& GetShouldContinue();

private:
    void InitLWTrace();
    void InitProfileLog();
    void InitKikimrService();

    void InitRdmaServer(NStorage::TDiskAgentConfig& config);
};

}   // namespace NCloud::NBlockStore::NServer
