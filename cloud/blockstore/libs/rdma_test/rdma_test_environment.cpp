#include "rdma_test_environment.h"

#include <cloud/blockstore/libs/common/block_checksum.h>
#include <cloud/blockstore/libs/storage/disk_agent/model/device_client.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/hash.h>

namespace NCloud::NBlockStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

TRdmaTestEnvironment::TRdmaTestEnvironment(size_t deviceSize, ui32 poolSize)
    : Storage(std::make_shared<TMemoryTestStorage>(deviceSize))
{
    THashMap<TString, TStorageAdapterPtr> devices;
    devices[Device_1] =
        std::make_shared<TStorageAdapter>(Storage, 4_KB, true, 0);
    TVector<TString> uuids;
    for (const auto& [key, value]: devices) {
        uuids.push_back(key);
    }
    auto deviceClient = std::make_shared<TDeviceClient>(
        TDuration::MilliSeconds(100),
        uuids,
        Logging->CreateLog("BLOCKSTORE_DISK_AGENT"));
    deviceClient->AcquireDevices(
        uuids,
        ClientId,
        TInstant::Now(),
        NProto::VOLUME_ACCESS_READ_WRITE,
        0,
        "vol0",
        0);

    NProto::TRdmaEndpoint config;
    config.SetHost(Host);
    config.SetPort(Port);

    TRdmaTargetConfig rdmaTargetConfig{
        true,
        TOldRequestCounters{
            Counters->GetCounter("Delayed"),
            Counters->GetCounter("Rejected"),
            Counters->GetCounter("Already")},
        poolSize};

    RdmaTaget = CreateRdmaTarget(
        std::move(config),
        std::move(rdmaTargetConfig),
        Logging,
        Server,
        std::move(deviceClient),
        std::move(devices));

    RdmaTaget->Start();
}

TRdmaTestEnvironment::~TRdmaTestEnvironment()
{
    RdmaTaget->Stop();
}

// static
ui64 TRdmaTestEnvironment::CalcChecksum(ui32 size, char fill)
{
    TString data(size, fill);
    TBlockChecksum checksum;
    checksum.Extend(data.Data(), data.Size());
    return checksum.GetValue();
}

// static
void TRdmaTestEnvironment::CheckResponse(
    const NProto::TReadDeviceBlocksResponse& response,
    const TBlockRange64& blockRange,
    char fill)
{
    UNIT_ASSERT_VALUES_EQUAL_C(
        S_OK,
        response.GetError().GetCode(),
        response.GetError().GetMessage());

    UNIT_ASSERT_VALUES_EQUAL(
        blockRange.Size(),
        response.GetBlocks().BuffersSize());

    for (const auto& buffer: response.GetBlocks().GetBuffers()) {
        for (auto byte: buffer) {
            if (byte != fill) {
                UNIT_ASSERT_C(false, "!!!!");
            }
        }
    }
}

NProto::TWriteDeviceBlocksRequest TRdmaTestEnvironment::MakeWriteRequest(
    const TBlockRange64& blockRange,
    char fill,
    ui64 volumeRequestId,
    bool isMultideviceRequest) const
{
    NProto::TWriteDeviceBlocksRequest result;
    result.SetDeviceUUID(Device_1);
    result.SetBlockSize(4_KB);
    result.SetStartIndex(blockRange.Start);
    result.MutableHeaders()->SetClientId(ClientId);
    result.SetVolumeRequestId(volumeRequestId);
    result.SetMultideviceRequest(isMultideviceRequest);

    for (ui32 i = 0; i < blockRange.Size(); ++i) {
        *result.MutableBlocks()->AddBuffers() = TString(4_KB, fill);
    }
    return result;
}

NProto::TReadDeviceBlocksRequest TRdmaTestEnvironment::MakeReadRequest(
    const TBlockRange64& blockRange) const
{
    NProto::TReadDeviceBlocksRequest result;
    result.SetDeviceUUID(Device_1);
    result.SetBlockSize(4_KB);
    result.SetStartIndex(blockRange.Start);
    result.SetBlocksCount(blockRange.Size());
    result.MutableHeaders()->SetClientId(ClientId);
    return result;
}

NProto::TChecksumDeviceBlocksRequest TRdmaTestEnvironment::MakeChecksumRequest(
    const TBlockRange64& blockRange) const
{
    NProto::TChecksumDeviceBlocksRequest result;
    result.SetDeviceUUID(Device_1);
    result.SetBlockSize(4_KB);
    result.SetStartIndex(blockRange.Start);
    result.SetBlocksCount(blockRange.Size());
    result.MutableHeaders()->SetClientId(ClientId);
    return result;
}

NProto::TZeroDeviceBlocksRequest TRdmaTestEnvironment::MakeZeroRequest(
    const TBlockRange64& blockRange,
    ui64 volumeRequestId,
    bool isMultideviceRequest) const
{
    NProto::TZeroDeviceBlocksRequest result;
    result.SetDeviceUUID(Device_1);
    result.SetBlockSize(4_KB);
    result.SetStartIndex(blockRange.Start);
    result.SetBlocksCount(blockRange.Size());
    result.MutableHeaders()->SetClientId(ClientId);
    result.SetVolumeRequestId(volumeRequestId);
    result.SetMultideviceRequest(isMultideviceRequest);
    return result;
}

}   // namespace NCloud::NBlockStore::NStorage
