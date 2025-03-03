#include "client_test.h"

#include <cloud/blockstore/libs/common/block_checksum.h>
#include <cloud/blockstore/libs/common/sglist.h>
#include <cloud/blockstore/libs/rdma/iface/error.h>
#include <cloud/blockstore/libs/rdma/iface/protobuf.h>
#include <cloud/blockstore/libs/rdma/iface/protocol.h>
#include <cloud/blockstore/libs/service_local/rdma_protocol.h>
#include <cloud/blockstore/libs/storage/protos/disk.pb.h>

#include <util/generic/deque.h>
#include <util/string/printf.h>

namespace NCloud::NBlockStore::NStorage {

namespace {

////////////////////////////////////////////////////////////////////////////////

class TRequest: public NRdma::TClientRequest
{
public:
    TRequest(
            NRdma::IClientHandlerPtr handler,
            std::unique_ptr<NRdma::TNullContext> context)
        : NRdma::TClientRequest(std::move(handler), std::move(context))
    {}

    ~TRequest() override
    {
        delete[] RequestBuffer.data();
        delete[] ResponseBuffer.data();
    }
};

////////////////////////////////////////////////////////////////////////////////

TString MakeKey(const TString& host, ui32 port)
{
    return Sprintf("%s:%u", host.c_str(), port);
}

////////////////////////////////////////////////////////////////////////////////

}   // namespace

struct TRdmaClientTest::TRdmaEndpointImpl
    : NRdma::IClientEndpoint
{
    TDeque<TString> Blocks;
    NProto::TError AllocationError;
    NProto::TError RdmaResponseError;
    NProto::TError ResponseError;
    TRdmaClientTest::TMessageObserver MessageObserver;

    TRdmaEndpointImpl() = default;

    TResultOrError<NRdma::TClientRequestPtr> AllocateRequest(
        NRdma::IClientHandlerPtr handler,
        std::unique_ptr<NRdma::TNullContext> context,
        size_t requestBytes,
        size_t responseBytes) override
    {
        if (HasError(AllocationError)) {
            return AllocationError;
        }

        auto req = std::make_unique<TRequest>(
            std::move(handler),
            std::move(context));
        req->RequestBuffer = {new char[requestBytes], requestBytes};
        req->ResponseBuffer = {new char[responseBytes], responseBytes};

        return NRdma::TClientRequestPtr(std::move(req));
    }

    void SendRequest(
        NRdma::TClientRequestPtr req,
        TCallContextPtr callContext) override
    {
        Y_UNUSED(callContext);

        auto* serializer = TBlockStoreProtocol::Serializer();
        auto [result, err] = serializer->Parse(req->RequestBuffer);
        Y_ENSURE_EX(!HasError(err), yexception() << err.GetMessage());

        if (MessageObserver) {
            MessageObserver(result);
        }

        if (HasError(RdmaResponseError)) {
            auto len = NRdma::SerializeError(
                RdmaResponseError.GetCode(),
                RdmaResponseError.GetMessage(),
                req->ResponseBuffer);

            auto* handler = req->Handler.get();
            handler->HandleResponse(
                std::move(req),
                NRdma::RDMA_PROTO_FAIL,
                len);

            return;
        }

        size_t responseBytes = 0;
        switch (result.MsgId) {
            case TBlockStoreProtocol::ReadDeviceBlocksRequest: {
                NProto::TReadDeviceBlocksResponse response;
                TSgList sglist;

                if (ResponseError.GetCode()) {
                    *response.MutableError() = ResponseError;
                }

                if (!HasError(ResponseError)) {
                    using TProto = NProto::TReadDeviceBlocksRequest;
                    auto* request = static_cast<TProto*>(result.Proto.get());
                    const size_t minSize =
                        request->GetStartIndex() + request->GetBlocksCount();
                    if (Blocks.size() < minSize) {
                        Blocks.resize(minSize, TString(4_KB, 0));
                    }
                    for (ui32 i = request->GetStartIndex(); i < minSize; ++i) {
                        sglist.emplace_back(Blocks[i].Data(), Blocks[i].Size());
                    }
                }

                responseBytes = serializer->Serialize(
                    req->ResponseBuffer,
                    TBlockStoreProtocol::ReadDeviceBlocksResponse,
                    response,
                    TContIOVector(
                        (IOutputStream::TPart*)sglist.begin(),
                        sglist.size()
                    ));

                break;
            }

            case TBlockStoreProtocol::WriteDeviceBlocksRequest: {
                NProto::TWriteDeviceBlocksResponse response;

                if (ResponseError.GetCode()) {
                    *response.MutableError() = ResponseError;
                }

                if (!HasError(ResponseError)) {
                    using TProto = NProto::TWriteDeviceBlocksRequest;
                    auto* request = static_cast<TProto*>(result.Proto.get());
                    const auto blockCount =
                        result.Data.Size() / request->GetBlockSize();
                    const size_t minSize = request->GetStartIndex() + blockCount;
                    if (Blocks.size() < minSize) {
                        Blocks.resize(minSize, TString(4_KB, 0));
                    }
                    ui64 offset = 0;
                    for (ui32 i = request->GetStartIndex(); i < minSize; ++i) {
                        Blocks[i] =
                            result.Data.substr(offset, request->GetBlockSize());
                        offset += request->GetBlockSize();
                    }
                }

                responseBytes = serializer->Serialize(
                    req->ResponseBuffer,
                    TBlockStoreProtocol::WriteDeviceBlocksResponse,
                    response,
                    TContIOVector(nullptr, 0));

                break;
            }

            case TBlockStoreProtocol::ZeroDeviceBlocksRequest: {
                NProto::TZeroDeviceBlocksResponse response;

                if (ResponseError.GetCode()) {
                    *response.MutableError() = ResponseError;
                }

                if (!HasError(ResponseError)) {
                    using TProto = NProto::TZeroDeviceBlocksRequest;
                    auto* request = static_cast<TProto*>(result.Proto.get());
                    const auto blockCount = request->GetBlocksCount();
                    const size_t minSize = request->GetStartIndex() + blockCount;
                    if (Blocks.size() < minSize) {
                        Blocks.resize(minSize, TString(4_KB, 0));
                    }
                    for (ui32 i = request->GetStartIndex(); i < minSize; ++i) {
                        Blocks[i] = TString(4_KB, 0);
                    }
                }

                responseBytes = serializer->Serialize(
                    req->ResponseBuffer,
                    TBlockStoreProtocol::ZeroDeviceBlocksResponse,
                    response,
                    TContIOVector(nullptr, 0));

                break;
            }

            case TBlockStoreProtocol::ChecksumDeviceBlocksRequest: {
                NProto::TChecksumDeviceBlocksResponse response;
                TSgList sglist;

                if (ResponseError.GetCode()) {
                    *response.MutableError() = ResponseError;
                }

                if (!HasError(ResponseError)) {
                    using TProto = NProto::TChecksumDeviceBlocksRequest;
                    auto* request = static_cast<TProto*>(result.Proto.get());
                    const size_t minSize =
                        request->GetStartIndex() + request->GetBlocksCount();
                    if (Blocks.size() < minSize) {
                        Blocks.resize(minSize, TString(4_KB, 0));
                    }

                    TBlockChecksum checksum;
                    for (ui32 i = request->GetStartIndex(); i < minSize; ++i) {
                        checksum.Extend(Blocks[i].Data(), Blocks[i].Size());
                    }
                    response.SetChecksum(checksum.GetValue());
                }

                responseBytes = serializer->Serialize(
                    req->ResponseBuffer,
                    TBlockStoreProtocol::ChecksumDeviceBlocksResponse,
                    response,
                    TContIOVector(nullptr, 0));

                break;
            }

            default: {
                Y_ABORT_UNLESS(false);
            }
        }

        auto* handler = req->Handler.get();
        handler->HandleResponse(
            std::move(req),
            NRdma::RDMA_PROTO_OK,
            responseBytes);
    }
};

////////////////////////////////////////////////////////////////////////////////

NThreading::TFuture<NRdma::IClientEndpointPtr> TRdmaClientTest::StartEndpoint(
    TString host,
    ui32 port)
{
    auto& ep = Endpoints[MakeKey(host, port)];
    if (!ep.Endpoint) {
        ep.Endpoint = std::make_shared<TRdmaEndpointImpl>();
        ep.Promise = NThreading::NewPromise<NRdma::IClientEndpointPtr>();
    }
    return ep.Promise;
}

void TRdmaClientTest::InjectErrors(
    NProto::TError allocationError,
    NProto::TError rdmaResponseError,
    NProto::TError responseError)
{
    for (auto& x: Endpoints) {
        auto& ep = static_cast<TRdmaEndpointImpl&>(*x.second.Endpoint);
        ep.AllocationError = allocationError;
        ep.RdmaResponseError = rdmaResponseError;
        ep.ResponseError = responseError;
    }
}

ui32 TRdmaClientTest::InitAllEndpoints()
{
    for (auto& x: Endpoints) {
        x.second.Promise.SetValue(x.second.Endpoint);
    }

    return Endpoints.size();
}

ui32 TRdmaClientTest::InitAllEndpointsWithError()
{
    for (auto& x: Endpoints) {
        x.second.Promise.SetException("init failure");
    }

    return Endpoints.size();
}

void TRdmaClientTest::SetMessageObserver(TMessageObserver messageObserver)
{
    MessageObserver = messageObserver;
    for (auto& [_, endpointInfo]: Endpoints) {
        endpointInfo.Endpoint->MessageObserver = MessageObserver;
    }
}

}   // namespace NCloud::NBlockStore::NStorage
