#pragma once

#include <library/cpp/grpc/client/grpc_client_low.h>
#include <library/cpp/threading/future/core/future.h>
#include <contrib/ydb/library/yql/providers/common/proto/gateways_config.pb.h>
#include <contrib/ydb/library/yql/providers/generic/connector/api/service/connector.grpc.pb.h>
#include <contrib/ydb/library/yql/providers/generic/connector/api/service/protos/connector.pb.h>

namespace NYql::NConnector {
    template <typename TResponse>
    struct TResult {
        NGrpc::TGrpcStatus Status;
        std::optional<TResponse> Response;
    };

    template <class TResponse>
    using TAsyncResult = NThreading::TFuture<TResult<TResponse>>;

    using TDescribeTableAsyncResult = TAsyncResult<NApi::TDescribeTableResponse>;
    // using TListSplitsAsyncResult = TAsyncResult<NApi::TListSplitsResponse>;
    // using TReadSplitsAsyncResult = TAsyncResult<NApi::TReadSplitsResponse>;

    template <class TResponse>
    class TStreamer {
    public:
        using TSelf = TStreamer;

        using TStreamProcessorPtr = typename NGrpc::IStreamRequestReadProcessor<TResponse>::TPtr;

        TStreamer(TStreamProcessorPtr streamProcessor)
            : StreamProcessor_(streamProcessor)
            , Finished_(false)
                  {};

        TAsyncResult<TResponse> ReadNext(std::shared_ptr<TSelf> self) {
            auto promise = NThreading::NewPromise<TResult<TResponse>>();
            auto readCallback = [self = std::move(self), promise](NGrpc::TGrpcStatus&& status) mutable {
                if (!status.Ok()) {
                    promise.SetValue({std::move(status), std::nullopt});
                    self->Finished_ = true;
                } else {
                    promise.SetValue({std::move(status), std::move(self->Response_)});
                }
            };

            StreamProcessor_->Read(&Response_, readCallback);
            return promise.GetFuture();
        }

        ~TStreamer() {
            StreamProcessor_->Cancel();
        }

        bool IsFinished() const {
            return Finished_;
        }

    private:
        TStreamProcessorPtr StreamProcessor_;
        TResponse Response_;
        bool Finished_;
    };

    template <class TResponse>
    class IStreamIterator {
    public:
        using TPtr = std::shared_ptr<IStreamIterator<TResponse>>;
        using TResult = TAsyncResult<TResponse>;

        virtual TAsyncResult<TResponse> ReadNext() = 0;

        virtual ~IStreamIterator(){};
    };

    using IListSplitsStreamIterator = IStreamIterator<NApi::TListSplitsResponse>;
    using IReadSplitsStreamIterator = IStreamIterator<NApi::TReadSplitsResponse>;

    template <class TIterator>
    struct TIteratorResult {
        NGrpc::TGrpcStatus Status;
        typename TIterator::TPtr Iterator;
    };

    template <class TIterator>
    using TIteratorAsyncResult = NThreading::TFuture<TIteratorResult<TIterator>>;

    using TListSplitsStreamIteratorAsyncResult = TIteratorAsyncResult<IListSplitsStreamIterator>;
    using TReadSplitsStreamIteratorAsyncResult = TIteratorAsyncResult<IReadSplitsStreamIterator>;

    class IClient {
    public:
        using TPtr = std::shared_ptr<IClient>;

        virtual TDescribeTableAsyncResult DescribeTable(const NApi::TDescribeTableRequest& request) = 0;
        virtual TListSplitsStreamIteratorAsyncResult ListSplits(const NApi::TListSplitsRequest& request) = 0;
        virtual TReadSplitsStreamIteratorAsyncResult ReadSplits(const NApi::TReadSplitsRequest& request) = 0;
        virtual ~IClient() = default;
    };

    IClient::TPtr MakeClientGRPC(const NYql::TGenericConnectorConfig& cfg);
}
