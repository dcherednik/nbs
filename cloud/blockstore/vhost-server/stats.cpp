#include "stats.h"

#include <library/cpp/json/json_writer.h>

#include <util/datetime/cputimer.h>
#include <util/stream/output.h>

namespace NCloud::NBlockStore::NVHostServer {
namespace {

////////////////////////////////////////////////////////////////////////////////

void WriteTimes(
    NJsonWriter::TBuf& buf,
    const auto& hist,
    const auto& prevHist,
    ui64 cyclesPerMs)
{
    buf.WriteKey("times");
    buf.BeginList();

    auto bucket = [&] (ui64 time, ui64 count) {
        buf.BeginList();
        buf.WriteULongLong(time);
        buf.WriteULongLong(count);
        buf.EndList();
    };

    ui64 prevTime = 0;
    ui64 prevCount = 0;

    hist.IterateDiffBuckets(prevHist, [&] (ui64 start, ui64 end, ui64 count) {
        const ui64 m = end + (start - end) / 2;
        const ui64 v = m * 1000 / cyclesPerMs;

        if (v == prevTime) {
            prevCount += count;
        } else {
            if (prevTime) {
                bucket(prevTime, prevCount);
            }

            prevTime = v;
            prevCount = count;
        }
    });

    if (prevTime) {
        bucket(prevTime, prevCount);
    }

    buf.EndList();
}

void WriteSizes(NJsonWriter::TBuf& buf, const auto& hist, const auto& prevHist)
{
    buf.WriteKey("sizes");
    buf.BeginList();

    hist.IterateDiffBuckets(prevHist, [&] (ui64 start, ui64 end, ui64 count) {
        Y_UNUSED(end);

        buf.BeginList();
        buf.WriteULongLong(start);
        buf.WriteULongLong(count);
        buf.EndList();
    });

    buf.EndList();
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

void DumpStats(
    const TSimpleStats& stats,
    TSimpleStats& old,
    TDuration elapsed,
    IOutputStream& stream,
    ui64 cyclesPerMs)
{
    NJsonWriter::TBuf buf {NJsonWriter::HEM_DONT_ESCAPE_HTML, &stream};

    auto write = [&] (TStringBuf key, ui64 value) {
        buf.WriteKey(key);
        buf.WriteULongLong(value);
    };

    auto request = [&] (int kind, TStringBuf key) {
        const auto r = stats.Requests[kind] - old.Requests[kind];

        buf.WriteKey(key);
        buf.BeginObject();
        write("count", r.Count);
        write("bytes", r.Bytes);
        write("errors", r.Errors);
        write("unaligned", r.Unaligned);

        WriteTimes(buf, stats.Times[kind], old.Times[kind], cyclesPerMs);
        WriteSizes(buf, stats.Sizes[kind], old.Sizes[kind]);

        buf.EndObject();
    };

    buf.BeginObject();

    write("elapsed_ms", elapsed.MilliSeconds());
    write("dequeued", stats.Dequeued - old.Dequeued);
    write("submitted", stats.Submitted - old.Submitted);
    write("submission_failed", stats.SubFailed - old.SubFailed);
    write("completed", stats.Completed - old.Completed);
    write("failed", stats.CompFailed - old.CompFailed);

    request(0, "read");
    request(1, "write");

    buf.EndObject();
    stream << Endl;

    old = stats;
}

}   // namespace NCloud::NBlockStore::NVHostServer
