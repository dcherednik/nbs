#include "compaction_map.h"

#include "alloc.h"

#include <library/cpp/containers/intrusive_rb_tree/rb_tree.h>

#include <util/generic/algorithm.h>
#include <util/generic/intrlist.h>
#include <util/system/align.h>

#include <array>
#include <functional>

namespace NCloud::NFileStore::NStorage {

namespace {

////////////////////////////////////////////////////////////////////////////////

ui32 GetGroupIndex(ui32 rangeId)
{
    return AlignDown(rangeId, TCompactionMap::GroupSize);
}

ui32 GetCompactionScore(const TCompactionStats& stats)
{
    // TODO
    return stats.BlobsCount;
}

ui32 GetCleanupScore(const TCompactionStats& stats)
{
    // TODO
    return stats.DeletionsCount;
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
ui32 GetGroupIndex(const T& node);

struct TCompareByGroupIndex
{
    template <typename T1, typename T2>
    static bool Compare(const T1& l, const T2& r)
    {
        return GetGroupIndex(l) < GetGroupIndex(r);
    }
};

struct TTreeItemByGroupIndex
    : TRbTreeItem<TTreeItemByGroupIndex, TCompareByGroupIndex>
{};

using TTreeByGroupIndex = TRbTree<
    TTreeItemByGroupIndex,
    TCompareByGroupIndex>;

////////////////////////////////////////////////////////////////////////////////

template <typename T>
ui32 GetCompactionScore(const T& node);

struct TCompareByCompactionScore
{
    template <typename T1, typename T2>
    static bool Compare(const T1& l, const T2& r)
    {
        return GetCompactionScore(l) > GetCompactionScore(r);
    }
};

struct TTreeItemByCompactionScore
    : TRbTreeItem<TTreeItemByCompactionScore, TCompareByCompactionScore>
{};

using TTreeByCompactionScore = TRbTree<
    TTreeItemByCompactionScore,
    TCompareByCompactionScore>;

////////////////////////////////////////////////////////////////////////////////

template <typename T>
ui32 GetCleanupScore(const T& node);

struct TCompareByCleanupScore
{
    template <typename T1, typename T2>
    static bool Compare(const T1& l, const T2& r)
    {
        return GetCleanupScore(l) > GetCleanupScore(r);
    }
};

struct TTreeItemByCleanupScore
    : TRbTreeItem<TTreeItemByCleanupScore, TCompareByCleanupScore>
{};

using TTreeByCleanupScore = TRbTree<
    TTreeItemByCleanupScore,
    TCompareByCleanupScore>;

////////////////////////////////////////////////////////////////////////////////

struct TGroup
    : TIntrusiveListItem<TGroup>
    , TTreeItemByGroupIndex
    , TTreeItemByCompactionScore
    , TTreeItemByCleanupScore
{
    const ui32 GroupIndex;

    TCompactionCounter TopCompactionScore;
    TCompactionCounter TopCleanupScore;

    std::array<TCompactionStats, TCompactionMap::GroupSize> Stats = {};

    TGroup(ui32 groupIndex)
        : GroupIndex(groupIndex)
    {}

    const TCompactionStats& Get(ui32 rangeId) const
    {
        return Stats[rangeId - GroupIndex];
    }

    i32 Update(ui32 rangeId, ui32 blobsCount, ui32 deletionsCount)
    {
        auto& stats = Stats[rangeId - GroupIndex];

        bool wasEmpty = stats.BlobsCount == 0
            && stats.DeletionsCount == 0;

        bool nowEmpty = blobsCount == 0
            && deletionsCount == 0;

        i32 diff = 0;
        if (wasEmpty && !nowEmpty) {
            diff = 1;
        } else if (!wasEmpty && nowEmpty) {
            diff = -1;
        }

        stats.BlobsCount = blobsCount;
        stats.DeletionsCount = deletionsCount;

        ui32 compactionScore = GetCompactionScore(stats);
        if (TopCompactionScore.Score < compactionScore) {
            TopCompactionScore = { rangeId, compactionScore };
        } else if (TopCompactionScore.RangeId == rangeId) {
            ui32 i = GetTop<TCompareByCompactionScore>();
            TopCompactionScore = { GroupIndex + i, GetCompactionScore(Stats[i]) };
        }

        ui32 cleanupScore = GetCleanupScore(stats);
        if (TopCleanupScore.Score < cleanupScore) {
            TopCleanupScore = { rangeId, cleanupScore };
        } else if (TopCleanupScore.RangeId == rangeId) {
            ui32 i = GetTop<TCompareByCleanupScore>();
            TopCleanupScore = { GroupIndex + i, GetCleanupScore(Stats[i]) };
        }

        return diff;
    }

    template <typename TCompare>
    ui32 GetTop() const
    {
        auto top = std::max_element(Stats.begin(), Stats.end(), [] (const auto& top, const auto& next) {
                // true if lhs < rhs
                return TCompare::Compare(next, top);
            });

        return top - Stats.begin();
    }

    bool Empty() const
    {
        return TopCompactionScore.Score == 0 &&
             TopCleanupScore.Score == 0;
    }
};

using TGroupList = TIntrusiveList<TGroup>;

////////////////////////////////////////////////////////////////////////////////

template <typename T>
ui32 GetGroupIndex(const T& node)
{
    return static_cast<const TGroup&>(node).GroupIndex;
}

template <typename T>
ui32 GetCompactionScore(const T& node)
{
    return static_cast<const TGroup&>(node).TopCompactionScore.Score;
}

template <typename T>
ui32 GetCleanupScore(const T& node)
{
    return static_cast<const TGroup&>(node).TopCleanupScore.Score;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

struct TCompactionMap::TImpl
{
    IAllocator* Alloc;

    TGroupList Groups;

    TTreeByGroupIndex GroupByGroupIndex;
    TTreeByCompactionScore GroupByCompactionScore;
    TTreeByCleanupScore GroupByCleanupScore;

    ui32 UsedRangesCount = 0;

    TImpl(IAllocator* alloc)
        : Alloc(alloc)
    {}

    ~TImpl()
    {
        Groups.ForEach([&] (TGroup* group) {
            std::destroy_at(group);
            Alloc->Release({group, sizeof(*group)});
        });
    }

    TGroup* FindGroup(ui32 groupIndex) const
    {
        return static_cast<TGroup*>(GroupByGroupIndex.Find(groupIndex));
    }

    void UpdateGroup(ui32 rangeId, ui32 blobsCount, ui32 deletionsCount)
    {
        ui32 groupIndex = GetGroupIndex(rangeId);

        auto* group = FindGroup(groupIndex);
        if (!group) {
            group = LinkGroup(groupIndex);
        }

        UsedRangesCount += group->Update(rangeId, blobsCount, deletionsCount);

        if (group->Empty()) {
            UnLinkGroup(group);
        } else {
            GroupByCompactionScore.Insert(group);
            GroupByCleanupScore.Insert(group);
        }
    }

    const TGroup* GetTopCompactionScore() const
    {
        if (!GroupByCompactionScore.Empty()) {
            return static_cast<const TGroup*>(&*GroupByCompactionScore.Begin());
        }
        return nullptr;
    }

    const TGroup* GetTopCleanupScore() const
    {
        if (!GroupByCleanupScore.Empty()) {
            return static_cast<const TGroup*>(&*GroupByCleanupScore.Begin());
        }
        return nullptr;
    }

    using TGetScore = std::function<double(const TCompactionRangeInfo&)>;

    TVector<TCompactionRangeInfo> GetTopRanges(
        ui32 c,
        const TGetScore& getScore) const
    {
        if (!c) {
            return {};
        }

        TVector<TCompactionRangeInfo> ranges;

        for (const auto& g: Groups) {
            for (ui32 i = 0; i < GroupSize; ++i) {
                if (g.Stats[i].BlobsCount || g.Stats[i].DeletionsCount) {
                    ranges.push_back({g.GroupIndex + i, g.Stats[i]});
                }
            }
        }

        SortBy(ranges, getScore);

        ranges.crop(c);
        return ranges;
    }

    TVector<TCompactionRangeInfo> GetTopRangesByCompactionScore(ui32 c) const
    {
        // TODO efficient implementation for any c
        if (c == 1) {
            const auto* group = GetTopCompactionScore();
            if (group) {
                ui32 index = 0;
                TCompactionStats best = group->Stats[0];
                for (ui32 i = 1; i < GroupSize; ++i) {
                    auto stats = group->Stats[i];
                    if (stats.BlobsCount > best.BlobsCount) {
                        index = i;
                        best = stats;
                    }
                }

                return {{group->GroupIndex + index, best}};
            }

            return {};
        }

        return GetTopRanges(c, [] (const TCompactionRangeInfo& r) {
            return -static_cast<double>(r.Stats.BlobsCount);
        });
    }

    TVector<TCompactionRangeInfo> GetTopRangesByCleanupScore(ui32 c) const
    {
        // TODO efficient implementation for any c
        if (c == 1) {
            const auto* group = GetTopCleanupScore();
            if (group) {
                ui32 index = 0;
                TCompactionStats best = group->Stats[0];
                for (ui32 i = 1; i < GroupSize; ++i) {
                    auto stats = group->Stats[i];
                    if (stats.DeletionsCount > best.DeletionsCount) {
                        index = i;
                        best = stats;
                    }
                }

                return {{group->GroupIndex + index, best}};
            }

            return {};
        }

        return GetTopRanges(c, [] (const TCompactionRangeInfo& r) {
            return -static_cast<double>(r.Stats.DeletionsCount);
        });
    }

private:
    TGroup* LinkGroup(ui32 groupIndex)
    {
        auto block = Alloc->Allocate(sizeof(TGroup));
        auto group = new (block.Data) TGroup(groupIndex);
        Groups.PushBack(group);
        GroupByGroupIndex.Insert(group);

        return group;
    }

    void UnLinkGroup(TGroup* group)
    {
        group->Unlink();
        static_cast<TTreeItemByGroupIndex*>(group)->UnLink();
        static_cast<TTreeItemByCompactionScore*>(group)->UnLink();
        static_cast<TTreeItemByCleanupScore*>(group)->UnLink();

        std::destroy_at(group);
        Alloc->Release({group, sizeof(*group)});
    }
};

////////////////////////////////////////////////////////////////////////////////

TCompactionMap::TCompactionMap(IAllocator* alloc)
    : Impl(new TImpl(alloc))
{}

TCompactionMap::~TCompactionMap()
{}

void TCompactionMap::Update(ui32 rangeId, ui32 blobsCount, ui32 deletionsCount)
{
    Impl->UpdateGroup(rangeId, blobsCount, deletionsCount);
}

TCompactionStats TCompactionMap::Get(ui32 rangeId) const
{
    ui32 groupIndex = GetGroupIndex(rangeId);

    const auto* group = Impl->FindGroup(groupIndex);
    if (group) {
        return group->Get(rangeId);
    }

    return {};
}

TCompactionCounter TCompactionMap::GetTopCompactionScore() const
{
    const auto* group = Impl->GetTopCompactionScore();
    if (group) {
        return group->TopCompactionScore;
    }

    return {};
}

TCompactionCounter TCompactionMap::GetTopCleanupScore() const
{
    const auto* group = Impl->GetTopCleanupScore();
    if (group) {
        return group->TopCleanupScore;
    }

    return {};
}

TVector<ui32> TCompactionMap::GetNonEmptyCompactionRanges() const
{
    TVector<ui32> result(Reserve(Impl->Groups.Size() * GroupSize));
    for (const auto& group: Impl->Groups) {
        for (ui32 i = 0; i < group.Stats.size(); ++i) {
            if (group.Stats[i].BlobsCount > 0 || group.Stats[i].DeletionsCount > 0) {
                result.push_back(i + group.GroupIndex);
            }
        }
    }

    return result;
}

TVector<TCompactionRangeInfo> TCompactionMap::GetTopRangesByCompactionScore(ui32 topSize) const
{
    return Impl->GetTopRangesByCompactionScore(topSize);
}

TVector<TCompactionRangeInfo> TCompactionMap::GetTopRangesByCleanupScore(ui32 topSize) const
{
    return Impl->GetTopRangesByCleanupScore(topSize);
}

TCompactionMapStats TCompactionMap::GetStats(ui32 topSize) const
{
    TCompactionMapStats stats = {
        .UsedRangesCount = Impl->UsedRangesCount,
        .AllocatedRangesCount = Impl->Groups.Size() * TCompactionMap::GroupSize,
        .TopRangesByCleanupScore = GetTopRangesByCleanupScore(topSize),
        .TopRangesByCompactionScore = GetTopRangesByCompactionScore(topSize),
    };

    return stats;
}

}   // namespace NCloud::NFileStore::NStorage
