#pragma once

#include "public.h"

#include "keyring.h"

#include <cloud/storage/core/libs/common/error.h>

#include <util/generic/string.h>

namespace NCloud {

////////////////////////////////////////////////////////////////////////////////

struct IMutableEndpointStorage
{
    virtual ~IMutableEndpointStorage() = default;

    virtual NProto::TError Init() = 0;
    virtual NProto::TError Remove() = 0;

    virtual TResultOrError<ui32> AddEndpoint(
        const TString& key,
        const TString& data) = 0;

    virtual NProto::TError RemoveEndpoint(
        const TString& key) = 0;
};

////////////////////////////////////////////////////////////////////////////////

IMutableEndpointStoragePtr CreateKeyringMutableEndpointStorage(
    TString rootKeyringDesc,
    TString endpointsKeyringDesc);

IMutableEndpointStoragePtr CreateFileMutableEndpointStorage(TString dirPath);

////////////////////////////////////////////////////////////////////////////////

template <typename TRequest>
TString SerializeEndpoint(const TRequest& request)
{
    auto data = TString::Uninitialized(request.ByteSize());

    if (!request.SerializeToArray(const_cast<char*>(data.data()), data.size())) {
        Y_ABORT("Could not serialize endpoint: %s",
            request.ShortDebugString().c_str());
    }

    return data;
}

}   // namespace NCloud
