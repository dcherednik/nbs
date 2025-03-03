#include "device_scanner.h"

#include <cloud/storage/core/libs/diagnostics/logging.h>

#include <cloud/blockstore/libs/storage/core/config.h>

#include <util/generic/algorithm.h>
#include <util/string/builder.h>

#include <sys/stat.h>

#include <filesystem>
#include <regex>

namespace NCloud::NBlockStore::NStorage {

namespace {

////////////////////////////////////////////////////////////////////////////////

ui32 GetBlockSize(const std::string& path)
{
    struct stat s {};

    if (int r = ::stat(path.c_str(), &s)) {
        const int ec = errno;
        ythrow TServiceError {MAKE_SYSTEM_ERROR(ec)}
            << "can't get information about a file " << path << ": "
            << ::strerror(ec);
    }

    return s.st_blksize;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

NProto::TError FindDevices(
    const NProto::TStorageDiscoveryConfig& config,
    TDeviceCallback cb)
{
    namespace NFs = std::filesystem;

    try {
        for (const auto& p: config.GetPathConfigs()) {
            const NFs::path pathRegExp = std::string {p.GetPathRegExp()};

            const std::regex re {
                pathRegExp.filename().string(),
                std::regex_constants::ECMAScript
            };

            for (const auto& entry: NFs::directory_iterator {pathRegExp.parent_path()}) {
                const auto& path = entry.path();
                const auto filename = path.filename().string();

                std::smatch match;
                if (!std::regex_match(filename, match, re)) {
                    continue;
                }

                if (!entry.is_regular_file() && !entry.is_block_file()) {
                    return MakeError(E_ARGUMENT, TStringBuilder()
                        << path << " is not a file or a block device ");
                }

                if (match.size() < 2) {
                    return MakeError(E_ARGUMENT, TStringBuilder()
                        << "path " << path << " accepted but regexp "
                        << p.GetPathRegExp() << " doesn't contain any group of digits");
                }

                const ui32 deviceNumber = std::stoul(match[1].str());
                if (!deviceNumber) {
                    return MakeError(E_ARGUMENT, TStringBuilder()
                        << path << ": the device number can't be zero");
                }

                const ui64 size = entry.file_size();
                auto* pool = FindIfPtr(p.GetPoolConfigs(), [&] (const auto& pool) {
                    return pool.GetMinSize() <= size && size <= pool.GetMaxSize();
                });

                if (!pool) {
                    return MakeError(E_NOT_FOUND, TStringBuilder()
                        << "unable to find the appropriate pool for " << path);
                }

                auto error = cb(
                    TString {path.string()},
                    *pool,
                    deviceNumber,
                    p.GetMaxDeviceCount(),
                    GetBlockSize(path),
                    size);
                if (HasError(error)) {
                    return error;
                }
            }
        }
    } catch (...) {
        return MakeError(E_FAIL, CurrentExceptionMessage());
    }

    return {};
}

}   // namespace NCloud::NBlockStore::NStorage
