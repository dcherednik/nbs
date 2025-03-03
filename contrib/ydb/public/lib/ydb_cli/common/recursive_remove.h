#pragma once

#include <contrib/ydb/public/sdk/cpp/client/ydb_scheme/scheme.h>
#include <contrib/ydb/public/sdk/cpp/client/ydb_table/table.h>
#include <contrib/ydb/public/sdk/cpp/client/ydb_topic/topic.h>

namespace NYdb::NConsoleClient {

enum class ERecursiveRemovePrompt {
    Always,
    Once,
    Never,
};

bool Prompt(ERecursiveRemovePrompt mode, const TString& path, NScheme::ESchemeEntryType type, bool first = true);

TStatus RemoveDirectoryRecursive(
    NScheme::TSchemeClient& schemeClient,
    NTable::TTableClient& tableClient,
    const TString& path,
    const NScheme::TRemoveDirectorySettings& settings = {},
    bool removeSelf = true,
    bool createProgressBar = true);

TStatus RemoveDirectoryRecursive(
    NScheme::TSchemeClient& schemeClient,
    NTable::TTableClient& tableClient,
    NTopic::TTopicClient& topicClient,
    const TString& path,
    ERecursiveRemovePrompt prompt,
    const NScheme::TRemoveDirectorySettings& settings = {},
    bool removeSelf = true,
    bool createProgressBar = true);

}
