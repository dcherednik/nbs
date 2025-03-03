#include "notify.h"

#include "config.h"
#include "https.h"

#include <cloud/storage/core/libs/diagnostics/logging.h>

#include <library/cpp/json/writer/json_value.h>

#include <util/string/builder.h>

namespace NCloud::NBlockStore::NNotify {

using namespace NThreading;

namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename T>
concept TDiskEvent = std::is_same_v<T, TDiskError>
                  || std::is_same_v<T, TDiskBackOnline>;

////////////////////////////////////////////////////////////////////////////////

TStringBuf GetTemplateId(const TDiskError&)
{
    return "nbs.nonrepl.error";
}

TStringBuf GetTemplateId(const TDiskBackOnline&)
{
    return "nbs.nonrepl.back-online";
}

////////////////////////////////////////////////////////////////////////////////

void OutputEvent(IOutputStream& out, const TDiskEvent auto& event)
{
    out << GetTemplateId(event) << " { " << event.DiskId << " }";
}

////////////////////////////////////////////////////////////////////////////////

void FillData(const TDiskEvent auto& event, NJson::TJsonValue& data)
{
    data["diskId"] = event.DiskId;
}

////////////////////////////////////////////////////////////////////////////////

class TServiceStub final
    : public IService
{
public:
    void Start() override
    {}

    void Stop() override
    {}

    TFuture<NProto::TError> Notify(const TNotification& data) override
    {
        Y_UNUSED(data);

        return MakeFuture(NProto::TError());
    }
};

////////////////////////////////////////////////////////////////////////////////

class TServiceNull final
    : public IService
{
private:
    const ILoggingServicePtr Logging;
    TLog Log;

public:
    explicit TServiceNull(ILoggingServicePtr logging)
        : Logging(std::move(logging))
    {}

    void Start() override
    {
        Log = Logging->CreateLog("BLOCKSTORE_NOTIFY");
    }

    void Stop() override
    {}

    TFuture<NProto::TError> Notify(const TNotification& data) override
    {
        STORAGE_WARN("Discard notification "
            << data.Event
            << " " << data.UserId.Quote()
            << " " << data.CloudId.Quote() << "/" << data.FolderId.Quote());

        return MakeFuture(NProto::TError());
    }
};

////////////////////////////////////////////////////////////////////////////////

class TService final
    : public IService
{
private:
    const TNotifyConfigPtr Config;
    THttpsClient HttpsClient;

public:
    explicit TService(TNotifyConfigPtr config)
        : Config(std::move(config))
    {}

    void Start() override
    {
        if (auto path = Config->GetCaCertFilename()) {
            HttpsClient.LoadCaCerts(path);
        }
    }

    void Stop() override
    {}

    TFuture<NProto::TError> Notify(const TNotification& data) override
    {
        // TODO: Add Timestamp when time formatting will be supported
        // by Cloud Notify service
        NJson::TJsonMap v {
            { "type", std::visit([] (const auto& event) {
                    return GetTemplateId(event);
                },
                data.Event)
            },
            { "data", NJson::TJsonMap {
                { "cloudId", data.CloudId },
                { "folderId", data.FolderId },
            }}
        };

        if (!data.UserId.empty()) {
            v["userId"] = data.UserId;
        } else {
            v["cloudId"] = data.CloudId;
        }

        std::visit([&v] (const auto& e) {
                FillData(e, v["data"]);
            },
            data.Event);

        auto p = NewPromise<NProto::TError>();

        HttpsClient.Post(
            Config->GetEndpoint(),
            v.GetStringRobust(),
            "application/json",
            [p, event = data.Event] (int code, const TString& message) mutable {
                const bool isSuccess = code >= 200 && code < 300;

                if (isSuccess) {
                    p.SetValue(MakeError(S_OK, TStringBuilder()
                        << "HTTP code: " << code));
                    return;
                }

                p.SetValue(MakeError(E_REJECTED, TStringBuilder()
                    << "Couldn't send notification " << event
                    << ". HTTP error: " << code << " " << message));
            });

        return p.GetFuture();
    }
};

}   // namespace

////////////////////////////////////////////////////////////////////////////////

IServicePtr CreateService(TNotifyConfigPtr config)
{
    return std::make_shared<TService>(std::move(config));
}

IServicePtr CreateServiceStub()
{
    return std::make_shared<TServiceStub>();
}

IServicePtr CreateNullService(ILoggingServicePtr logging)
{
    return std::make_shared<TServiceNull>(std::move(logging));
}

}   // namespace NCloud::NBlockStore::NNotify

////////////////////////////////////////////////////////////////////////////////

Y_DECLARE_OUT_SPEC(, NCloud::NBlockStore::NNotify::TEvent, out, event)
{
    using namespace NCloud::NBlockStore::NNotify;

    std::visit([&] (const auto& e) { OutputEvent(out, e); }, event);
}
