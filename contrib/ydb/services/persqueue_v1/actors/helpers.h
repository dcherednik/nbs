#pragma once

#include <contrib/ydb/public/api/protos/ydb_persqueue_v1.pb.h>
#include <contrib/ydb/public/api/protos/ydb_topic.pb.h>
#include <contrib/ydb/core/persqueue/writer/source_id_encoding.h>
#include <contrib/ydb/services/lib/sharding/sharding.h>

namespace NKikimr::NGRpcProxy::V1 {

using namespace Ydb;

bool RemoveEmptyMessages(PersQueue::V1::MigrationStreamingReadServerMessage::DataBatch& data);

bool RemoveEmptyMessages(Topic::StreamReadMessage::ReadResponse& data);

TMaybe<ui32> GetPartitionFromConfigOptions(ui32 preferred, const NPQ::NSourceIdEncoding::TEncodedSourceId& encodedSrcId,
                                           ui32 partPerTablet, bool firstClass, bool useRoundRobin);
}
