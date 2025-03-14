// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemTagNodeGroupingAndSorting.h"

// TraceInsightsCore
#include "InsightsCore/Table/ViewModels/TableColumn.h"

// TraceInsights
#include "Insights/MemoryProfiler/ViewModels/MemTagNodeHelper.h"

#define LOCTEXT_NAMESPACE "UE::Insights::MemoryProfiler::FMemTagNode"

#define INSIGHTS_ENSURE ensure
//#define INSIGHTS_ENSURE(...)

// Default pre-sorting (group nodes sorts above leaf nodes)
#define INSIGHTS_DEFAULT_PRESORTING_NODES(A, B) \
	{ \
		if (ShouldCancelSort()) \
		{ \
			return CancelSort(); \
		} \
		if (A->IsGroup() != B->IsGroup()) \
		{ \
			return A->IsGroup(); \
		} \
	}

// Sort by name (ascending).
#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetName().LexicalLess(B->GetName());
//#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetDefaultSortOrder() < B->GetDefaultSortOrder();

namespace UE::Insights::MemoryProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting by Type
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemTagNodeSortingByType::FMemTagNodeSortingByType(TSharedRef<FTableColumn> InColumnRef)
	: FTableCellValueSorter(
		FName(TEXT("ByType")),
		LOCTEXT("Sorting_ByType_Name", "By Type"),
		LOCTEXT("Sorting_ByType_Title", "Sort By Type"),
		LOCTEXT("Sorting_ByType_Desc", "Sort by type of tree nodes."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemTagNodeSortingByType::Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const
{
	if (SortMode == ESortMode::Ascending)
	{
		NodesToSort.Sort([this](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
		{
			INSIGHTS_DEFAULT_PRESORTING_NODES(A, B);

			INSIGHTS_ENSURE(A.IsValid() && A->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeA = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(A);

			INSIGHTS_ENSURE(B.IsValid() && B->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeB = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(B);

			if (MemTagNodeA->GetType() == MemTagNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by type (ascending).
				return MemTagNodeA->GetType() < MemTagNodeB->GetType();
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([this](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
		{
			INSIGHTS_DEFAULT_PRESORTING_NODES(A, B);

			INSIGHTS_ENSURE(A.IsValid() && A->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeA = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(A);

			INSIGHTS_ENSURE(B.IsValid() && B->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeB = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(B);

			if (MemTagNodeA->GetType() == MemTagNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by type (descending).
				return MemTagNodeB->GetType() < MemTagNodeA->GetType();
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting by Tracker(s)
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemTagNodeSortingByTracker::FMemTagNodeSortingByTracker(TSharedRef<FTableColumn> InColumnRef)
	: FTableCellValueSorter(
		FName(TEXT("ByTracker")),
		LOCTEXT("Sorting_ByTracker_Name", "By Tracker"),
		LOCTEXT("Sorting_ByTracker_Title", "Sort By Tracker"),
		LOCTEXT("Sorting_ByTracker_Desc", "Sort by memory tracker."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemTagNodeSortingByTracker::Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const
{
	if (SortMode == ESortMode::Ascending)
	{
		NodesToSort.Sort([this](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
		{
			INSIGHTS_DEFAULT_PRESORTING_NODES(A, B);

			INSIGHTS_ENSURE(A.IsValid() && A->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeA = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(A);

			INSIGHTS_ENSURE(B.IsValid() && B->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeB = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(B);

			if (MemTagNodeA->GetMemTrackerId() == MemTagNodeB->GetMemTrackerId())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by trackers (ascending).
				return int32(MemTagNodeA->GetMemTrackerId()) < int32(MemTagNodeB->GetMemTrackerId());
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([this](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
		{
			INSIGHTS_DEFAULT_PRESORTING_NODES(A, B);

			INSIGHTS_ENSURE(A.IsValid() && A->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeA = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(A);

			INSIGHTS_ENSURE(B.IsValid() && B->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeB = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(B);

			if (MemTagNodeA->GetMemTrackerId() == MemTagNodeB->GetMemTrackerId())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by trackers (descending).
				return int32(MemTagNodeB->GetMemTrackerId()) < int32(MemTagNodeA->GetMemTrackerId());
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort by Instance Count
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemTagNodeSortingByInstanceCount::FMemTagNodeSortingByInstanceCount(TSharedRef<FTableColumn> InColumnRef)
	: FTableCellValueSorter(
		FName(TEXT("ByInstanceCount")),
		LOCTEXT("Sorting_ByInstanceCount_Name", "By Instance Count"),
		LOCTEXT("Sorting_ByInstanceCount_Title", "Sort By Instance Count"),
		LOCTEXT("Sorting_ByInstanceCount_Desc", "Sort by aggregated instance count."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemTagNodeSortingByInstanceCount::Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const
{
	if (SortMode == ESortMode::Ascending)
	{
		NodesToSort.Sort([this](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
		{
			INSIGHTS_DEFAULT_PRESORTING_NODES(A, B);

			INSIGHTS_ENSURE(A.IsValid() && A->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeA = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(A);
			const uint64 ValueA = MemTagNodeA->GetAggregatedStats().InstanceCount;

			INSIGHTS_ENSURE(B.IsValid() && B->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeB = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(B);
			const uint64 ValueB = MemTagNodeB->GetAggregatedStats().InstanceCount;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by instance count (ascending).
				return ValueA < ValueB;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([this](const FBaseTreeNodePtr& A, const FBaseTreeNodePtr& B) -> bool
		{
			INSIGHTS_DEFAULT_PRESORTING_NODES(A, B);

			INSIGHTS_ENSURE(A.IsValid() && A->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeA = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(A);
			const uint64 ValueA = MemTagNodeA->GetAggregatedStats().InstanceCount;

			INSIGHTS_ENSURE(B.IsValid() && B->Is<FMemTagNode>());
			const FMemTagNodePtr MemTagNodeB = StaticCastSharedPtr<FMemTagNode, FBaseTreeNode>(B);
			const uint64 ValueB = MemTagNodeB->GetAggregatedStats().InstanceCount;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by instance count (descending).
				return ValueB < ValueA;
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::MemoryProfiler

#undef INSIGHTS_DEFAULT_SORTING_NODES
#undef INSIGHTS_DEFAULT_PRESORTING_NODES
#undef INSIGHTS_ENSURE
#undef LOCTEXT_NAMESPACE
