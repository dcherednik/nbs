#pragma once

#include "histogram.h"

#include <util/datetime/base.h>
#include <util/system/types.h>

#include <array>
#include <atomic>
#include <span>

class IOutputStream;

namespace NCloud::NBlockStore::NVHostServer {

////////////////////////////////////////////////////////////////////////////////

using TTimeHistogram = THistogram<7>;
using TSizeHistogram = THistogram<3>;

template <typename T>
struct TRequestStats
{
    T Count = {};
    T Bytes = {};
    T Errors = {};
    T Unaligned = {};

    TRequestStats() = default;

    template <typename U>
    TRequestStats(const TRequestStats<U>& rhs) noexcept
        : Count {rhs.Count}
        , Bytes {rhs.Bytes}
        , Errors {rhs.Errors}
        , Unaligned {rhs.Unaligned}
    {}

    template <typename U>
    TRequestStats& operator = (const TRequestStats<U>& rhs) noexcept
    {
        Count = rhs.Count;
        Bytes = rhs.Bytes;
        Errors = rhs.Errors;
        Unaligned = rhs.Unaligned;

        return *this;
    }

    template <typename U>
    TRequestStats& operator += (const TRequestStats<U>& rhs) noexcept
    {
        Count += rhs.Count;
        Bytes += rhs.Bytes;
        Errors += rhs.Errors;
        Unaligned += rhs.Unaligned;

        return *this;
    }
};

template <typename T>
TRequestStats<T> operator - (TRequestStats<T> lhs, TRequestStats<T>& rhs) noexcept
{
    lhs.Count -= rhs.Count;
    lhs.Bytes -= rhs.Bytes;
    lhs.Errors -= rhs.Errors;
    lhs.Unaligned -= rhs.Unaligned;

    return lhs;
}

template <typename T>
struct TStats
{
    T Dequeued = {};
    T Submitted = {};
    T SubFailed = {};
    T Completed = {};
    T CompFailed = {};

    std::array<TRequestStats<T>, 2> Requests = {};
    std::array<TTimeHistogram, 2> Times = {};
    std::array<TSizeHistogram, 2> Sizes = {};

    TStats() = default;

    template <typename U>
    TStats(const TStats<U>& rhs) noexcept
        : Dequeued {rhs.Dequeued}
        , Submitted {rhs.Submitted}
        , SubFailed {rhs.SubFailed}
        , Completed {rhs.Completed}
        , CompFailed {rhs.CompFailed}
        , Requests {
            rhs.Requests[0],
            rhs.Requests[1]
        },
        Times {
            rhs.Times[0],
            rhs.Times[1]
        },
        Sizes {
            rhs.Sizes[0],
            rhs.Sizes[1],
        }
    {}

    template <typename U>
    TStats& operator = (const TStats<U>& rhs) noexcept
    {
        Dequeued = rhs.Dequeued;
        Submitted = rhs.Submitted;
        SubFailed = rhs.SubFailed;
        Completed = rhs.Completed;
        CompFailed = rhs.CompFailed;
        Requests = rhs.Requests;
        Times = rhs.Times;
        Sizes = rhs.Sizes;

        return *this;
    }

    template <typename U>
    TStats& operator += (const TStats<U>& rhs) noexcept
    {
        Dequeued += rhs.Dequeued;
        Submitted += rhs.Submitted;
        SubFailed += rhs.SubFailed;
        Completed += rhs.Completed;
        CompFailed += rhs.CompFailed;

        for (size_t i = 0; i != Requests.size(); ++i) {
            Requests[i] += rhs.Requests[i];
        }

        for (size_t i = 0; i != Times.size(); ++i) {
            Times[i] += rhs.Times[i];
        }

        for (size_t i = 0; i != Sizes.size(); ++i) {
            Sizes[i] += rhs.Sizes[i];
        }

        return *this;
    }
};

using TAtomicStats = TStats<std::atomic<ui64>>;
using TSimpleStats = TStats<ui64>;

////////////////////////////////////////////////////////////////////////////////

void DumpStats(
    const TSimpleStats& stats,
    TSimpleStats& old,
    TDuration elapsed,
    IOutputStream& stream,
    ui64 cyclesPerMs);

}   // namespace NCloud::NBlockStore::NVHostServer
