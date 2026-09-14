#pragma once

#include "CoreMinimal.h"
#include "SFPPlannerTypes.h"
#include "Widgets/SLeafWidget.h"

DECLARE_DELEGATE_TwoParams(FOnSFPNodeCompletionChanged, int32, bool);

/** Lightweight, zoomable production graph rendered entirely with Slate draw elements. */
class SSFPGraphPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSFPGraphPanel) {}
		SLATE_EVENT(FOnSFPNodeCompletionChanged, OnNodeCompletionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetPlan(TSharedPtr<const FSFPPlanResult> InPlan);
	void RefreshCompletionState();
	void ResetView();
	void FitGraph();
	void AutoArrange();

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

private:
	struct FPortDisplay
	{
		FString ItemClassPath;
		FText Label;
		FVector2D GraphAnchor = FVector2D::ZeroVector;
		TArray<int32> EdgeIndices;
	};

	struct FEdgeLayout
	{
		FVector2D SourceAnchor = FVector2D::ZeroVector;
		FVector2D TargetAnchor = FVector2D::ZeroVector;
		FVector2D LabelPosition = FVector2D::ZeroVector;
		FVector2D LabelSize = FVector2D::ZeroVector;
		TArray<FVector2D> RoutePoints;
	};

	void RebuildLayout();
	FLinearColor GetNodeColor(ESFPPlanNodeType Type) const;
	FLinearColor GetEdgeColor(const FSFPPlanEdge& Edge) const;
	FVector2D TransformGraphPoint(const FVector2D& GraphPoint) const;

	TSharedPtr<const FSFPPlanResult> Plan;
	TMap<int32, FVector2D> GraphPositions;
	TMap<int32, int32> NodeColumns;
	/** Cached edge membership keeps port/wiring rendering event-driven rather than rebuilding it every frame. */
	TMap<int32, TArray<int32>> OutgoingEdgesByNode;
	TMap<int32, TArray<int32>> IncomingEdgesByNode;
	TMap<int32, FText> NodeTitles;
	TMap<int32, FText> NodeDetails;
	TMap<int32, FText> NodeRecipeTitles;
	TMap<int32, FText> NodeMachineMeta;
	TMap<int32, FText> NodeFooter;
	TMap<int32, TArray<FPortDisplay>> InputPortsByNode;
	TMap<int32, TArray<FPortDisplay>> OutputPortsByNode;
	TSet<int32> RoutingNodeIds;
	TArray<FText> EdgeLabels;
	TArray<FEdgeLayout> EdgeLayouts;
	TArray<FText> ColumnTitles;
	FVector2D GraphExtent = FVector2D::ZeroVector;
	FVector2D Pan = FVector2D(32.0f, 48.0f);
	float Zoom = 1.0f;
	float RouteColumnSpacing = 1400.0f;
	int32 MaxLayoutColumn = 0;
	TMap<int32, FVector2D> ManualNodeOffsets;
	int32 HoveredNodeId = INDEX_NONE;
	int32 HoveredPortNodeId = INDEX_NONE;
	FString HoveredPortItemClassPath;
	int32 DraggedNodeId = INDEX_NONE;
	bool bDraggingNode = false;
	bool bPanning = false;
	FOnSFPNodeCompletionChanged NodeCompletionChanged;
};
