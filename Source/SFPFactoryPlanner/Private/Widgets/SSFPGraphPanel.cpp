#include "Widgets/SSFPGraphPanel.h"

#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/DrawElements.h"
#include "SFPLocalization.h"
#include "SFPNumberFormatting.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FVector2D NodeSize(560.0f, 280.0f);
	constexpr float HorizontalSpacing = 1580.0f;
	constexpr float VerticalSpacing = 500.0f;
	constexpr float ColumnHeaderHeight = 104.0f;
	constexpr float EdgeLabelWidth = 650.0f;
	constexpr float EdgeLabelHeight = 96.0f;
	constexpr float EdgeLabelGap = 40.0f;
	constexpr float EdgeLabelRightMargin = 120.0f;
	constexpr float EdgeRouteMargin = 30.0f;
	constexpr float EdgeEndpointStubLength = 78.0f;
	constexpr float WireTrackGap = 24.0f;
	constexpr float ReverseEdgeOffset = 120.0f;
	constexpr float StandardPortTop = 122.0f;
	constexpr float StandardPortBottom = 224.0f;
	constexpr float RoutingPortTop = 170.0f;
	constexpr float RoutingPortBottom = 232.0f;
	constexpr float MinimumReadableZoom = 0.46f;
	constexpr float MinimumGraphZoom = 0.05f;
	constexpr float MaximumGraphZoom = 2.0f;
	constexpr float MouseWheelZoomStep = 0.05f;
	constexpr float CompletionBoxSize = 22.0f;
	constexpr float CompletionBoxMargin = 8.0f;

	struct FRoutingWire
	{
		FVector2D Anchor = FVector2D::ZeroVector;
		FLinearColor Color = FLinearColor::White;
	};

	FSlateRect CompletionBoxForNode(
		const FVector2D& Position,
		const FVector2D& PaintedSize,
		const float Zoom)
	{
		const float Size = FMath::Clamp(CompletionBoxSize * Zoom, 12.0f, 28.0f);
		const float Margin = FMath::Clamp(CompletionBoxMargin * Zoom, 3.0f, 10.0f);
		const float Left = Position.X + PaintedSize.X - Size - Margin;
		const float Top = Position.Y + Margin;
		return FSlateRect(Left, Top, Left + Size, Top + Size);
	}

	bool ContainsPoint(const FSlateRect& Rect, const FVector2D& Point)
	{
		return Point.X >= Rect.Left && Point.X <= Rect.Right
			&& Point.Y >= Rect.Top && Point.Y <= Rect.Bottom;
	}

	bool IsRoutingNodeType(const ESFPPlanNodeType Type)
	{
		return Type == ESFPPlanNodeType::Splitter || Type == ESFPPlanNodeType::Merger;
	}

	bool IsStorageNodeType(const ESFPPlanNodeType Type)
	{
		return Type == ESFPPlanNodeType::Target || Type == ESFPPlanNodeType::Byproduct;
	}

	FString GraphNodeTitle(const FSFPPlanNode& Node)
	{
		FString Title = Node.Title;
		if (Node.Type == ESFPPlanNodeType::Target && !Node.ClassPath.Equals(TEXT("SFP.PowerGrid")))
		{
			Title = FString::Printf(TEXT("Lagercontainer: %s"), *Node.Title);
		}
		else if (Node.Type == ESFPPlanNodeType::Byproduct)
		{
			Title = FString::Printf(TEXT("Nebenprodukt-Lager: %s"), *Node.Title);
		}
		return Node.bCompleted ? FString::Printf(TEXT("✓ %s"), *Title) : Title;
	}

	FString CompactGraphNodeTitle(const FSFPPlanNode& Node)
	{
		FString Title = Node.Title;
		if (Node.Type == ESFPPlanNodeType::Target || Node.Type == ESFPPlanNodeType::Byproduct)
		{
			// The storage-card prefix is useful at detail zoom but wastes nearly all
			// of the width in the compact overview.
			Title = Node.Title;
		}
		else if (Node.Type == ESFPPlanNodeType::Source)
		{
			const int32 ResourceMarker = Title.Find(TEXT("-Rohstoffknoten"));
			if (ResourceMarker > 0)
			{
				Title = Title.Left(ResourceMarker);
			}
		}
		return Node.bCompleted ? FString::Printf(TEXT("✓ %s"), *Title) : Title;
	}

	FText WrapNodeHeaderText(const FString& InText)
	{
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);
		const float MaxWidth = 500.0f;

		FString Source = InText;
		Source.ReplaceInline(TEXT("\n"), TEXT(" "));
		Source = Source.TrimStartAndEnd();
		if (Source.IsEmpty())
		{
			return SFPLocalization::Text(TEXT(""));
		}

		auto MeasureWidth = [&](const FString& Value) -> float
		{
			return FontMeasure->Measure(Value, Font).X;
		};

		auto TruncateToWidth = [&](const FString& Value, const bool bForceEllipsis) -> FString
		{
			FString Trimmed = Value.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				return bForceEllipsis ? FString(TEXT("...")) : Trimmed;
			}

			const FString Ellipsis = TEXT("...");
			if (!bForceEllipsis && MeasureWidth(Trimmed) <= MaxWidth)
			{
				return Trimmed;
			}

			while (!Trimmed.IsEmpty() && MeasureWidth(Trimmed + Ellipsis) > MaxWidth)
			{
				Trimmed.LeftChopInline(1, false);
				Trimmed = Trimmed.TrimEnd();
			}

			return Trimmed.IsEmpty() ? Ellipsis : (Trimmed + Ellipsis);
		};

		if (MeasureWidth(Source) <= MaxWidth)
		{
			return SFPLocalization::Text(Source);
		}

		TArray<FString> Words;
		Source.ParseIntoArrayWS(Words);
		if (Words.Num() == 0)
		{
			return SFPLocalization::Text(TruncateToWidth(Source, false));
		}

		FString FirstLine;
		int32 WordIndex = 0;
		for (; WordIndex < Words.Num(); ++WordIndex)
		{
			const FString Candidate = FirstLine.IsEmpty() ? Words[WordIndex] : FString::Printf(TEXT("%s %s"), *FirstLine, *Words[WordIndex]);
			if (!FirstLine.IsEmpty() && MeasureWidth(Candidate) > MaxWidth)
			{
				break;
			}
			FirstLine = Candidate;
		}

		if (FirstLine.IsEmpty())
		{
			return SFPLocalization::Text(TruncateToWidth(Source, false));
		}

		FString Remaining;
		for (int32 RemainingIndex = WordIndex; RemainingIndex < Words.Num(); ++RemainingIndex)
		{
			Remaining = Remaining.IsEmpty() ? Words[RemainingIndex] : FString::Printf(TEXT("%s %s"), *Remaining, *Words[RemainingIndex]);
		}

		if (Remaining.IsEmpty())
		{
			return SFPLocalization::Text(TruncateToWidth(FirstLine, false));
		}

		const bool bNeedsEllipsis = MeasureWidth(Remaining) > MaxWidth;
		return SFPLocalization::Text(FirstLine + TEXT("\n") + TruncateToWidth(Remaining, bNeedsEllipsis));
	}

	float PortYForNode(const bool bRoutingNode, const int32 PortIndex, const int32 PortCount)
	{
		const float Top = bRoutingNode ? RoutingPortTop : StandardPortTop;
		const float Bottom = bRoutingNode ? RoutingPortBottom : StandardPortBottom;
		return PortCount > 0
			? Top + (Bottom - Top) * static_cast<float>(PortIndex + 1)
				/ static_cast<float>(PortCount + 1)
			: (Top + Bottom) * 0.5f;
	}

	// The routing schematic is the actual internal wiring. Every external input
	// and output continues from its coloured side port to the central junction,
	// so the splitter/merger can no longer look like a decorative icon detached
	// from the machine and storage connections.
	void DrawRoutingWiring(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& Geometry,
		const int32 Layer,
		const ESFPPlanNodeType Type,
		const FVector2D& Position,
		const float Zoom,
		const TArray<FRoutingWire>& Inputs,
		const TArray<FRoutingWire>& Outputs)
	{
		const bool bSplitter = Type == ESFPPlanNodeType::Splitter;
		const FLinearColor JunctionColor = bSplitter
			? FLinearColor(1.0f, 0.78f, 0.34f, 1.0f)
			: FLinearColor(0.45f, 1.0f, 0.90f, 1.0f);
		const float CenterY = Position.Y + (NodeSize.Y - 38.0f) * Zoom;
		const float HubX = Position.X + NodeSize.X * 0.5f * Zoom;
		const float LeftBusX = Position.X + 116.0f * Zoom;
		const float RightBusX = Position.X + (NodeSize.X - 116.0f) * Zoom;
		const float HubOffset = 24.0f * Zoom;
		const float LineWidth = FMath::Max(1.4f, 2.2f * Zoom);

		auto DrawWire = [&](const TArray<FVector2D>& Points, const FLinearColor& Color)
		{
			TArray<FVector2f> PaintedPoints;
			PaintedPoints.Reserve(Points.Num());
			for (const FVector2D& Point : Points)
			{
				PaintedPoints.Add(FVector2f(static_cast<float>(Point.X), static_cast<float>(Point.Y)));
			}
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				Layer,
				Geometry.ToPaintGeometry(),
				PaintedPoints,
				ESlateDrawEffect::None,
				FLinearColor(0.004f, 0.008f, 0.012f, 0.98f),
				true,
				FMath::Max(3.6f, 5.0f * Zoom));
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				Layer + 1,
				Geometry.ToPaintGeometry(),
				PaintedPoints,
				ESlateDrawEffect::None,
				Color,
				true,
				LineWidth);
		};

		const FVector2D Hub(HubX, CenterY);
		for (const FRoutingWire& Input : Inputs)
		{
			DrawWire({
				Input.Anchor,
				FVector2D(LeftBusX, Input.Anchor.Y),
				FVector2D(HubX - HubOffset, Input.Anchor.Y),
				FVector2D(HubX - HubOffset, CenterY),
				Hub
			}, Input.Color);
		}
		for (const FRoutingWire& Output : Outputs)
		{
			DrawWire({
				Hub,
				FVector2D(HubX + HubOffset, CenterY),
				FVector2D(HubX + HubOffset, Output.Anchor.Y),
				FVector2D(RightBusX, Output.Anchor.Y),
				Output.Anchor
			}, Output.Color);

			const float ArrowSize = FMath::Max(3.0f, 5.5f * Zoom);
			const float ArrowX = Output.Anchor.X - FMath::Max(7.0f, 13.0f * Zoom);
			const TArray<FVector2f> DirectionArrow = {
				FVector2f(ArrowX - ArrowSize, Output.Anchor.Y - ArrowSize),
				FVector2f(ArrowX, Output.Anchor.Y),
				FVector2f(ArrowX - ArrowSize, Output.Anchor.Y + ArrowSize)
			};
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				Layer + 2,
				Geometry.ToPaintGeometry(),
				DirectionArrow,
				ESlateDrawEffect::None,
				Output.Color,
				true,
				LineWidth);
		}

		const float DiamondRadius = FMath::Max(3.0f, 5.0f * Zoom);
		const TArray<FVector2f> Diamond = {
			FVector2f(Hub.X, Hub.Y - DiamondRadius),
			FVector2f(Hub.X + DiamondRadius, Hub.Y),
			FVector2f(Hub.X, Hub.Y + DiamondRadius),
			FVector2f(Hub.X - DiamondRadius, Hub.Y),
			FVector2f(Hub.X, Hub.Y - DiamondRadius)
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			Layer + 1,
			Geometry.ToPaintGeometry(),
			Diamond,
			ESlateDrawEffect::None,
			JunctionColor,
			true,
			LineWidth);
	}

	void DrawStorageSymbol(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& Geometry,
		const int32 Layer,
		const FVector2D& Position,
		const float Zoom,
		const bool bByproduct)
	{
		const FLinearColor Color = bByproduct
			? FLinearColor(0.94f, 0.67f, 0.22f, 0.88f)
			: FLinearColor(1.0f, 0.82f, 0.28f, 0.92f);
		const FVector2D TopLeft = Position
			+ FVector2D(NodeSize.X - 78.0f, NodeSize.Y - 62.0f) * Zoom;
		const FVector2D Size(58.0f * Zoom, 42.0f * Zoom);
		const float LineWidth = FMath::Max(1.2f, 2.0f * Zoom);
		TArray<FVector2f> Outline = {
			FVector2f(TopLeft.X, TopLeft.Y),
			FVector2f(TopLeft.X + Size.X, TopLeft.Y),
			FVector2f(TopLeft.X + Size.X, TopLeft.Y + Size.Y),
			FVector2f(TopLeft.X, TopLeft.Y + Size.Y),
			FVector2f(TopLeft.X, TopLeft.Y)
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			Layer,
			Geometry.ToPaintGeometry(),
			Outline,
			ESlateDrawEffect::None,
			Color,
			true,
			LineWidth);
		for (int32 Rib = 1; Rib <= 3; ++Rib)
		{
			const float X = TopLeft.X + Size.X * static_cast<float>(Rib) / 4.0f;
			const TArray<FVector2f> RibLine = {
				FVector2f(X, TopLeft.Y),
				FVector2f(X, TopLeft.Y + Size.Y)
			};
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				Layer,
				Geometry.ToPaintGeometry(),
				RibLine,
				ESlateDrawEffect::None,
				Color,
				true,
				FMath::Max(1.0f, 1.3f * Zoom));
		}
	}
}

void SSFPGraphPanel::Construct(const FArguments& InArgs)
{
	NodeCompletionChanged = InArgs._OnNodeCompletionChanged;
	SetCanTick(false);
}

void SSFPGraphPanel::SetPlan(TSharedPtr<const FSFPPlanResult> InPlan)
{
	Plan = MoveTemp(InPlan);
	ManualNodeOffsets.Reset();
	HoveredNodeId = INDEX_NONE;
	HoveredPortNodeId = INDEX_NONE;
	HoveredPortItemClassPath.Reset();
	RebuildLayout();
	ResetView();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSFPGraphPanel::RefreshCompletionState()
{
	if (Plan.IsValid())
	{
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			NodeTitles.FindOrAdd(Node.Id) = SFPLocalization::Text(CompactGraphNodeTitle(Node));
		}
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSFPGraphPanel::RebuildLayout()
{
	GraphPositions.Reset();
	NodeColumns.Reset();
	OutgoingEdgesByNode.Reset();
	IncomingEdgesByNode.Reset();
	NodeTitles.Reset();
	NodeDetails.Reset();
	NodeRecipeTitles.Reset();
	NodeMachineMeta.Reset();
	NodeFooter.Reset();
	InputPortsByNode.Reset();
	OutputPortsByNode.Reset();
	RoutingNodeIds.Reset();
	EdgeLabels.Reset();
	EdgeLayouts.Reset();
	ColumnTitles.Reset();
	GraphExtent = FVector2D::ZeroVector;
	MaxLayoutColumn = 0;
	if (!Plan.IsValid())
	{
		return;
	}

	GraphPositions.Reserve(Plan->Nodes.Num());
	NodeColumns.Reserve(Plan->Nodes.Num());
	NodeTitles.Reserve(Plan->Nodes.Num());
	NodeDetails.Reserve(Plan->Nodes.Num());
	NodeRecipeTitles.Reserve(Plan->Nodes.Num());
	NodeMachineMeta.Reserve(Plan->Nodes.Num());
	NodeFooter.Reserve(Plan->Nodes.Num());

	TMap<int32, TArray<int32>> NodesByColumn;
	TMap<int32, TArray<int32>> NeighboursByNode;
	TMap<int32, int32> InDegreeByNode;
	TMap<int32, TArray<int32>> OutgoingNodesByNode;
	TSet<int32> RootNodeIds;
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		NodeColumns.Add(Node.Id, 0);
		InDegreeByNode.Add(Node.Id, 0);
		NodeTitles.Add(Node.Id, SFPLocalization::Text(CompactGraphNodeTitle(Node)));
		NodeDetails.Add(Node.Id, SFPLocalization::Text(Node.Detail));

		FString RecipeTitle = GraphNodeTitle(Node);
		if (Node.Type == ESFPPlanNodeType::Machine && !Node.Detail.IsEmpty())
		{
			FString FirstLine;
			FString Remainder;
			if (Node.Detail.Split(TEXT("\n"), &FirstLine, &Remainder))
			{
				RecipeTitle = FirstLine;
			}
		}
		NodeRecipeTitles.Add(Node.Id, WrapNodeHeaderText(RecipeTitle));

		if (Node.Type == ESFPPlanNodeType::Machine && Node.MachineCount > KINDA_SMALL_NUMBER)
		{
			const int32 BuiltMachines = FMath::Max(1, FMath::CeilToInt(Node.MachineCount));
			const int32 FullMachines = FMath::FloorToInt(Node.MachineCount + KINDA_SMALL_NUMBER);
			const double PartialPercent = FMath::Max(0.0, (Node.MachineCount - static_cast<double>(FullMachines)) * 100.0);
			const FString ClockText = PartialPercent > 0.05
				? (FullMachines > 0
					? FString::Printf(TEXT("%d × 100 %% + 1 × %s %%"), FullMachines, *FSFPNumberFormatting::Decimal(PartialPercent, 1))
					: FString::Printf(TEXT("1 × %s %%"), *FSFPNumberFormatting::Decimal(PartialPercent, 1)))
				: FString::Printf(TEXT("%d × 100 %%"), BuiltMachines);
			NodeMachineMeta.Add(Node.Id, SFPLocalization::Text(FString::Printf(
				TEXT("%s  ·  %d Maschinen"), *Node.Title, BuiltMachines)));
			NodeFooter.Add(Node.Id, SFPLocalization::Text(FString::Printf(
				TEXT("⚡ %s MW  |  %s"), *FSFPNumberFormatting::Decimal(Node.PowerMW, 2), *ClockText)));
		}
		else
		{
			NodeMachineMeta.Add(Node.Id, SFPLocalization::Text(Node.Detail));
			NodeFooter.Add(Node.Id, FText::GetEmpty());
		}
		if (IsRoutingNodeType(Node.Type))
		{
			RoutingNodeIds.Add(Node.Id);
		}
	}

	for (const FSFPPlanEdge& Edge : Plan->Edges)
	{
		if (NodeColumns.Contains(Edge.SourceNodeId) && NodeColumns.Contains(Edge.TargetNodeId))
		{
			NeighboursByNode.FindOrAdd(Edge.SourceNodeId).AddUnique(Edge.TargetNodeId);
			NeighboursByNode.FindOrAdd(Edge.TargetNodeId).AddUnique(Edge.SourceNodeId);
			OutgoingNodesByNode.FindOrAdd(Edge.SourceNodeId).Add(Edge.TargetNodeId);
			++InDegreeByNode.FindChecked(Edge.TargetNodeId);
		}
	}

	// Compute columns from the actual material-flow topology. Depth values are
	// accumulated while shared recipes are solved and can place a routing node
	// in the same column as one of its consumers. The former fallback then drew
	// that perfectly normal connection to the right of the end-product column.
	// A longest-path topological rank guarantees left-to-right flow for every
	// acyclic edge and gives splitters/mergers their own real intermediate stage.
	TArray<int32> ReadyNodes;
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		if (InDegreeByNode.FindRef(Node.Id) == 0)
		{
			ReadyNodes.Add(Node.Id);
			RootNodeIds.Add(Node.Id);
		}
	}
	ReadyNodes.Sort();
	TSet<int32> TopologicallyPlacedNodes;
	for (int32 ReadyIndex = 0; ReadyIndex < ReadyNodes.Num(); ++ReadyIndex)
	{
		const int32 SourceNodeId = ReadyNodes[ReadyIndex];
		TopologicallyPlacedNodes.Add(SourceNodeId);
		const int32 SourceColumn = NodeColumns.FindRef(SourceNodeId);
		if (const TArray<int32>* Targets = OutgoingNodesByNode.Find(SourceNodeId))
		{
			for (const int32 TargetNodeId : *Targets)
			{
				int32& TargetColumn = NodeColumns.FindChecked(TargetNodeId);
				TargetColumn = FMath::Max(TargetColumn, SourceColumn + 1);
				int32& TargetInDegree = InDegreeByNode.FindChecked(TargetNodeId);
				--TargetInDegree;
				if (TargetInDegree == 0)
				{
					ReadyNodes.Add(TargetNodeId);
				}
			}
		}
	}

	// A genuine recipe cycle cannot be topologically ordered. Keep the previous
	// depth placement only for those explicitly unresolved nodes; their edges use
	// the separate reverse-route fallback instead of distorting ordinary plans.
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		if (!TopologicallyPlacedNodes.Contains(Node.Id))
		{
			NodeColumns.FindChecked(Node.Id) = FMath::Max(0, Plan->MaxDepth - Node.Depth);
		}
		MaxLayoutColumn = FMath::Max(MaxLayoutColumn, NodeColumns.FindRef(Node.Id));
	}

	// All requested products and ordinary byproduct leaves share one terminal
	// column, even when their recipe chains have different lengths. This keeps
	// the right edge visually final and gives shorter feeder branches room to
	// move towards the machines that actually consume them.
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		const TArray<int32>* Targets = OutgoingNodesByNode.Find(Node.Id);
		const bool bTerminalLeaf = Targets == nullptr || Targets->IsEmpty();
		if (TopologicallyPlacedNodes.Contains(Node.Id)
			&& (Node.Type == ESFPPlanNodeType::Target
				|| Node.Type == ESFPPlanNodeType::Byproduct
				|| bTerminalLeaf))
		{
			NodeColumns.FindChecked(Node.Id) = MaxLayoutColumn;
		}
	}

	// Right-align every acyclic feeder branch against its first consumer. A
	// short water or sand chain feeding a late recipe is therefore drawn directly
	// before that recipe instead of starting in the global leftmost column and
	// crossing half the plan. FMath::Min preserves left-to-right flow when one
	// provider genuinely feeds consumers in different stages.
	for (int32 OrderIndex = ReadyNodes.Num() - 1; OrderIndex >= 0; --OrderIndex)
	{
		const int32 NodeId = ReadyNodes[OrderIndex];
		const TArray<int32>* Targets = OutgoingNodesByNode.Find(NodeId);
		if (Targets == nullptr || Targets->IsEmpty())
		{
			continue;
		}
		int32 FirstConsumerColumn = TNumericLimits<int32>::Max();
		for (const int32 TargetNodeId : *Targets)
		{
			if (TopologicallyPlacedNodes.Contains(TargetNodeId))
			{
				FirstConsumerColumn = FMath::Min(
					FirstConsumerColumn,
					NodeColumns.FindRef(TargetNodeId));
			}
		}
		if (FirstConsumerColumn != TNumericLimits<int32>::Max())
		{
			int32& NodeColumn = NodeColumns.FindChecked(NodeId);
			NodeColumn = FMath::Max(NodeColumn, FirstConsumerColumn - 1);
		}
	}
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		NodesByColumn.FindOrAdd(NodeColumns.FindRef(Node.Id)).Add(Node.Id);
	}

	// Reorder every production stage around the average row of its connected
	// neighbours. Alternating left-to-right/right-to-left sweeps greatly reduce
	// crossings while keeping the solver's stage/depth semantics unchanged.
	TMap<int32, int32> RowByNode;
	auto RefreshRowLookup = [&]()
	{
		RowByNode.Reset();
		for (int32 Column = 0; Column <= MaxLayoutColumn; ++Column)
		{
			if (const TArray<int32>* NodeIds = NodesByColumn.Find(Column))
			{
				for (int32 Row = 0; Row < NodeIds->Num(); ++Row)
				{
					RowByNode.Add((*NodeIds)[Row], Row);
				}
			}
		}
	};

	auto SortColumnByNeighbours = [&](const int32 Column, const bool bUseEarlierColumns)
	{
		TArray<int32>* NodeIds = NodesByColumn.Find(Column);
		if (NodeIds == nullptr || NodeIds->Num() < 2)
		{
			return;
		}

		TMap<int32, float> SortKeys;
		TMap<int32, int32> PreviousRows;
		for (const int32 NodeId : *NodeIds)
		{
			const int32 PreviousRow = RowByNode.FindRef(NodeId);
			PreviousRows.Add(NodeId, PreviousRow);
			float RowSum = 0.0f;
			int32 MatchingNeighbours = 0;
			if (const TArray<int32>* Neighbours = NeighboursByNode.Find(NodeId))
			{
				for (const int32 NeighbourId : *Neighbours)
				{
					const int32* NeighbourColumn = NodeColumns.Find(NeighbourId);
					const int32* NeighbourRow = RowByNode.Find(NeighbourId);
					if (NeighbourColumn != nullptr && NeighbourRow != nullptr
						&& (bUseEarlierColumns ? *NeighbourColumn < Column : *NeighbourColumn > Column))
					{
						RowSum += static_cast<float>(*NeighbourRow);
						++MatchingNeighbours;
					}
				}
			}
			SortKeys.Add(NodeId, MatchingNeighbours > 0
				? RowSum / static_cast<float>(MatchingNeighbours)
				: static_cast<float>(PreviousRow));
		}

		NodeIds->Sort([&](const int32 LeftId, const int32 RightId)
		{
			const float LeftKey = SortKeys.FindRef(LeftId);
			const float RightKey = SortKeys.FindRef(RightId);
			if (!FMath::IsNearlyEqual(LeftKey, RightKey))
			{
				return LeftKey < RightKey;
			}
			const int32 LeftRow = PreviousRows.FindRef(LeftId);
			const int32 RightRow = PreviousRows.FindRef(RightId);
			return LeftRow == RightRow ? LeftId < RightId : LeftRow < RightRow;
		});
		RefreshRowLookup();
	};

	RefreshRowLookup();
	for (int32 Sweep = 0; Sweep < 5; ++Sweep)
	{
		for (int32 Column = 1; Column <= MaxLayoutColumn; ++Column)
		{
			SortColumnByNeighbours(Column, true);
		}
		for (int32 Column = MaxLayoutColumn - 1; Column >= 0; --Column)
		{
			SortColumnByNeighbours(Column, false);
		}
	}

	// Reserve enough independent gutters for the busiest column, rather than
	// collapsing all vertical connections onto two shared x coordinates.
	TMap<int32, int32> SourceTrackCounts, TargetTrackCounts, LabelTrackCounts;
	TArray<int32> SourceTracks, TargetTracks;
	SourceTracks.Init(0, Plan->Edges.Num());
	TargetTracks.Init(0, Plan->Edges.Num());
	int32 MaxTracks = 1;
	for (int32 I = 0; I < Plan->Edges.Num(); ++I)
	{
		const int32* From = NodeColumns.Find(Plan->Edges[I].SourceNodeId);
		const int32* To = NodeColumns.Find(Plan->Edges[I].TargetNodeId);
		if (!From || !To) continue;
		SourceTracks[I] = SourceTrackCounts.FindOrAdd(*From)++;
		TargetTracks[I] = TargetTrackCounts.FindOrAdd(*To)++;
		const int32 LabelLane = *To > *From ? *To - 1 : MaxLayoutColumn + 1;
		MaxTracks = FMath::Max(MaxTracks, ++LabelTrackCounts.FindOrAdd(LabelLane));
		MaxTracks = FMath::Max(MaxTracks, SourceTracks[I] + 1);
		MaxTracks = FMath::Max(MaxTracks, TargetTracks[I] + 1);
	}
	RouteColumnSpacing = FMath::Max(HorizontalSpacing,
		static_cast<float>(NodeSize.X) + EdgeLabelWidth + 4.0f * MaxTracks * WireTrackGap
		+ 2.0f * EdgeEndpointStubLength + EdgeLabelRightMargin);
	int32 MaximumRows = 1;
	for (const TPair<int32, TArray<int32>>& Pair : NodesByColumn)
	{
		MaximumRows = FMath::Max(MaximumRows, Pair.Value.Num());
	}
	for (int32 Column = 0; Column <= MaxLayoutColumn; ++Column)
	{
		const TArray<int32>* NodeIds = NodesByColumn.Find(Column);
		if (NodeIds == nullptr)
		{
			continue;
		}
		for (int32 Row = 0; Row < NodeIds->Num(); ++Row)
		{
			// Use the full height of the busiest column. Sparse branches therefore get
			// their own generous lanes instead of collapsing into one central bundle.
			const float DistributedRowSlot = NodeIds->Num() == MaximumRows
				? static_cast<float>(Row)
				: (static_cast<float>(Row + 1) * static_cast<float>(MaximumRows + 1)
					/ static_cast<float>(NodeIds->Num() + 1)) - 1.0f;
			FVector2D Position(
				static_cast<float>(Column) * RouteColumnSpacing,
				ColumnHeaderHeight + DistributedRowSlot * VerticalSpacing);
			Position += ManualNodeOffsets.FindRef((*NodeIds)[Row]);
			GraphPositions.Add((*NodeIds)[Row], Position);
			GraphExtent.X = FMath::Max(GraphExtent.X, Position.X + NodeSize.X);
			GraphExtent.Y = FMath::Max(GraphExtent.Y, Position.Y + NodeSize.Y);
		}
	}

	ColumnTitles.SetNum(MaxLayoutColumn + 1);
	for (int32 Column = 0; Column <= MaxLayoutColumn; ++Column)
	{
		FString Title;
		bool bHasDirectSupply = false;
		bool bHasProduction = false;
		bool bOnlyRouting = true;
		if (const TArray<int32>* NodeIds = NodesByColumn.Find(Column))
		{
			for (const int32 NodeId : *NodeIds)
			{
				bHasDirectSupply |= RootNodeIds.Contains(NodeId);
				if (const FSFPPlanNode* Node = Plan->Nodes.FindByPredicate([NodeId](const FSFPPlanNode& Candidate)
				{
					return Candidate.Id == NodeId;
				}))
				{
					bHasProduction |= Node->Type == ESFPPlanNodeType::Machine
						|| Node->Type == ESFPPlanNodeType::Generator;
					bOnlyRouting &= IsRoutingNodeType(Node->Type);
				}
			}
		}
		if (MaxLayoutColumn == 0)
		{
			Title = TEXT("PRODUKTIONSPLAN");
		}
		else if (Column == MaxLayoutColumn)
		{
			Title = Plan->bPowerProductionPlan
				? TEXT("NETTO-STROMZIEL")
				: TEXT("LAGER / NEBENSTRÖME");
		}
		else if (bOnlyRouting)
		{
			Title = TEXT("VERTEILUNG");
		}
		else if (Column == 0)
		{
			Title = TEXT("ROHSTOFFE / START");
		}
		else if (bHasDirectSupply && bHasProduction)
		{
			Title = FString::Printf(TEXT("ZUFUHR + PRODUKTIONSSTUFE %d"), Column);
		}
		else if (bHasDirectSupply)
		{
			Title = TEXT("DIREKTE ZUFUHR");
		}
		else
		{
			Title = FString::Printf(TEXT("PRODUKTIONSSTUFE %d"), Column);
		}
		ColumnTitles[Column] = SFPLocalization::Text(Title);
	}

	EdgeLabels.Reserve(Plan->Edges.Num());
	for (const FSFPPlanEdge& Edge : Plan->Edges)
	{
		if (Edge.Form == TEXT("power"))
		{
			EdgeLabels.Add(SFPLocalization::Text(FString::Printf(
				TEXT("%s · %s MW gesamt\n%s"),
				*Edge.ItemName,
				*FSFPNumberFormatting::Decimal(Edge.RatePerMinute, 2),
				*Edge.TransportLabel)));
		}
		else
		{
			EdgeLabels.Add(SFPLocalization::Text(FString::Printf(
				TEXT("%s · %s/min gesamt\n%s"),
				*Edge.ItemName,
				*FSFPNumberFormatting::Decimal(Edge.RatePerMinute),
				*Edge.TransportLabel)));
		}
	}

	EdgeLayouts.SetNum(Plan->Edges.Num());
	for (int32 EdgeIndex = 0; EdgeIndex < Plan->Edges.Num(); ++EdgeIndex)
	{
		const FSFPPlanEdge& Edge = Plan->Edges[EdgeIndex];
		if (GraphPositions.Contains(Edge.SourceNodeId) && GraphPositions.Contains(Edge.TargetNodeId))
		{
			OutgoingEdgesByNode.FindOrAdd(Edge.SourceNodeId).Add(EdgeIndex);
			IncomingEdgesByNode.FindOrAdd(Edge.TargetNodeId).Add(EdgeIndex);
		}
	}


	// Cache product ports once per structural rebuild. Machine/source/storage cards
	// expose one port per material, not one port per physical machine or per draw
	// call. Splitter/merger nodes intentionally keep branch ports separate.
	auto BuildPortGroups = [&](const int32 NodeId, const bool bOutgoing)
	{
		TArray<FPortDisplay>& Displays = bOutgoing
			? OutputPortsByNode.FindOrAdd(NodeId)
			: InputPortsByNode.FindOrAdd(NodeId);
		const TArray<int32>* EdgeIndices = bOutgoing
			? OutgoingEdgesByNode.Find(NodeId)
			: IncomingEdgesByNode.Find(NodeId);
		if (EdgeIndices == nullptr)
		{
			return;
		}
		const bool bRouting = RoutingNodeIds.Contains(NodeId);
		TMap<FString, int32> DisplayIndexByKey;
		for (const int32 EdgeIndex : *EdgeIndices)
		{
			if (!Plan->Edges.IsValidIndex(EdgeIndex)) continue;
			const FSFPPlanEdge& Edge = Plan->Edges[EdgeIndex];
			const FString ProductKey = Edge.ItemClassPath.IsEmpty() ? Edge.ItemName : Edge.ItemClassPath;
			const FString Key = bRouting
				? FString::Printf(TEXT("%s|%d"), *ProductKey, EdgeIndex)
				: ProductKey;
			int32* ExistingIndex = DisplayIndexByKey.Find(Key);
			if (ExistingIndex == nullptr)
			{
				FPortDisplay& Display = Displays.AddDefaulted_GetRef();
				Display.ItemClassPath = ProductKey;
				DisplayIndexByKey.Add(Key, Displays.Num() - 1);
				ExistingIndex = DisplayIndexByKey.Find(Key);
			}
			Displays[*ExistingIndex].EdgeIndices.Add(EdgeIndex);
		}
		Displays.Sort([&](const FPortDisplay& A, const FPortDisplay& B)
		{
			const int32 AEdge = A.EdgeIndices.IsEmpty() ? INDEX_NONE : A.EdgeIndices[0];
			const int32 BEdge = B.EdgeIndices.IsEmpty() ? INDEX_NONE : B.EdgeIndices[0];
			const FString AName = Plan->Edges.IsValidIndex(AEdge) ? Plan->Edges[AEdge].ItemName : A.ItemClassPath;
			const FString BName = Plan->Edges.IsValidIndex(BEdge) ? Plan->Edges[BEdge].ItemName : B.ItemClassPath;
			return AName == BName ? AEdge < BEdge : AName < BName;
		});
	};
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		BuildPortGroups(Node.Id, false);
		BuildPortGroups(Node.Id, true);
	}

	// Every connection receives a separate port along the node edge. Routing
	// buildings reserve their lower wiring bay for those ports, while machines
	// and storage endpoints use the full connection area. No two logical edges
	// are collapsed onto an invisible shared centre point.
	for (TPair<int32, TArray<int32>>& Pair : OutgoingEdgesByNode)
	{
		Pair.Value.Sort([&](const int32 LeftIndex, const int32 RightIndex)
		{
			const FVector2D* LeftTarget = GraphPositions.Find(Plan->Edges[LeftIndex].TargetNodeId);
			const FVector2D* RightTarget = GraphPositions.Find(Plan->Edges[RightIndex].TargetNodeId);
			const float LeftY = LeftTarget != nullptr ? LeftTarget->Y : 0.0f;
			const float RightY = RightTarget != nullptr ? RightTarget->Y : 0.0f;
			return FMath::IsNearlyEqual(LeftY, RightY) ? LeftIndex < RightIndex : LeftY < RightY;
		});
		const FVector2D* SourcePosition = GraphPositions.Find(Pair.Key);
		if (SourcePosition == nullptr)
		{
			continue;
		}
		for (int32 PortIndex = 0; PortIndex < Pair.Value.Num(); ++PortIndex)
		{
			const float PortY = PortYForNode(
				RoutingNodeIds.Contains(Pair.Key),
				PortIndex,
				Pair.Value.Num());
			EdgeLayouts[Pair.Value[PortIndex]].SourceAnchor =
				*SourcePosition + FVector2D(NodeSize.X, PortY);
		}
	}
	for (TPair<int32, TArray<int32>>& Pair : IncomingEdgesByNode)
	{
		Pair.Value.Sort([&](const int32 LeftIndex, const int32 RightIndex)
		{
			const FVector2D* LeftSource = GraphPositions.Find(Plan->Edges[LeftIndex].SourceNodeId);
			const FVector2D* RightSource = GraphPositions.Find(Plan->Edges[RightIndex].SourceNodeId);
			const float LeftY = LeftSource != nullptr ? LeftSource->Y : 0.0f;
			const float RightY = RightSource != nullptr ? RightSource->Y : 0.0f;
			return FMath::IsNearlyEqual(LeftY, RightY) ? LeftIndex < RightIndex : LeftY < RightY;
		});
		const FVector2D* TargetPosition = GraphPositions.Find(Pair.Key);
		if (TargetPosition == nullptr)
		{
			continue;
		}
		for (int32 PortIndex = 0; PortIndex < Pair.Value.Num(); ++PortIndex)
		{
			const float PortY = PortYForNode(
				RoutingNodeIds.Contains(Pair.Key),
				PortIndex,
				Pair.Value.Num());
			EdgeLayouts[Pair.Value[PortIndex]].TargetAnchor =
				*TargetPosition + FVector2D(0.0f, PortY);
		}
	}


	// Reassign non-routing endpoints so every distinct material owns one stable
	// left/right port. All edges of the same material share that product port.
	auto AssignProductPorts = [&](const int32 NodeId, TArray<FPortDisplay>& Ports, const bool bOutgoing)
	{
		const FVector2D* NodePosition = GraphPositions.Find(NodeId);
		if (NodePosition == nullptr) return;
		for (int32 PortIndex = 0; PortIndex < Ports.Num(); ++PortIndex)
		{
			FPortDisplay& Port = Ports[PortIndex];
			const float PortY = PortYForNode(false, PortIndex, Ports.Num());
			Port.GraphAnchor = *NodePosition + FVector2D(bOutgoing ? NodeSize.X : 0.0f, PortY);
			double TotalRate = 0.0;
			int32 TotalLines = 0;
			FString ItemName;
			FString Form;
			FString Transport;
			for (const int32 EdgeIndex : Port.EdgeIndices)
			{
				if (!Plan->Edges.IsValidIndex(EdgeIndex)) continue;
				const FSFPPlanEdge& Edge = Plan->Edges[EdgeIndex];
				TotalRate += Edge.RatePerMinute;
				TotalLines += FMath::Max(0, Edge.RequiredLines);
				if (ItemName.IsEmpty()) ItemName = Edge.ItemName;
				if (Form.IsEmpty()) Form = Edge.Form;
				if (Transport.IsEmpty()) Transport = Edge.TransportLabel;
				if (EdgeLayouts.IsValidIndex(EdgeIndex))
				{
					if (bOutgoing) EdgeLayouts[EdgeIndex].SourceAnchor = Port.GraphAnchor;
					else EdgeLayouts[EdgeIndex].TargetAnchor = Port.GraphAnchor;
				}
			}
			const bool bFluid = Form.Equals(TEXT("LIQUID"), ESearchCase::IgnoreCase)
				|| Form.Equals(TEXT("GAS"), ESearchCase::IgnoreCase);
			const FString Unit = bFluid ? TEXT(" m³/min") : TEXT("/min");
			const FString Lines = TotalLines > 1 ? FString::Printf(TEXT(" · %d Linien"), TotalLines) : FString();
			Port.Label = SFPLocalization::Text(FString::Printf(TEXT("%s  %s%s%s"),
				*ItemName, *FSFPNumberFormatting::Decimal(TotalRate), *Unit, *Lines));
		}
	};
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		if (RoutingNodeIds.Contains(Node.Id)) continue;
		if (TArray<FPortDisplay>* Inputs = InputPortsByNode.Find(Node.Id)) AssignProductPorts(Node.Id, *Inputs, false);
		if (TArray<FPortDisplay>* Outputs = OutputPortsByNode.Find(Node.Id)) AssignProductPorts(Node.Id, *Outputs, true);
	}

	TMap<int32, TArray<int32>> EdgesByLabelLane;
	TArray<float> DesiredLabelTops;
	TArray<int32> LabelLanes;
	DesiredLabelTops.SetNumZeroed(Plan->Edges.Num());
	LabelLanes.Init(INDEX_NONE, Plan->Edges.Num());
	for (int32 EdgeIndex = 0; EdgeIndex < Plan->Edges.Num(); ++EdgeIndex)
	{
		const FSFPPlanEdge& Edge = Plan->Edges[EdgeIndex];
		const int32* SourceColumn = NodeColumns.Find(Edge.SourceNodeId);
		const int32* TargetColumn = NodeColumns.Find(Edge.TargetNodeId);
		if (SourceColumn == nullptr || TargetColumn == nullptr)
		{
			continue;
		}
		const int32 Lane = *TargetColumn > *SourceColumn
			? *TargetColumn - 1
			: MaxLayoutColumn + 1;
		LabelLanes[EdgeIndex] = Lane;
		DesiredLabelTops[EdgeIndex] =
			(EdgeLayouts[EdgeIndex].SourceAnchor.Y + EdgeLayouts[EdgeIndex].TargetAnchor.Y) * 0.5f
			- EdgeLabelHeight * 0.5f;
		EdgesByLabelLane.FindOrAdd(Lane).Add(EdgeIndex);
	}

	struct FHorizontalTrack { float Left; float Right; float Y; };
	TArray<FHorizontalTrack> HorizontalTracks;
	// Protect the short, fixed port stubs before allocating long travel lanes.
	for (int32 I = 0; I < EdgeLayouts.Num(); ++I)
	{
		const FEdgeLayout& L = EdgeLayouts[I];
		HorizontalTracks.Add({static_cast<float>(L.SourceAnchor.X), static_cast<float>(L.SourceAnchor.X) + EdgeEndpointStubLength + SourceTracks[I] * WireTrackGap, static_cast<float>(L.SourceAnchor.Y)});
		HorizontalTracks.Add({static_cast<float>(L.TargetAnchor.X) - EdgeEndpointStubLength - TargetTracks[I] * WireTrackGap, static_cast<float>(L.TargetAnchor.X), static_cast<float>(L.TargetAnchor.Y)});
	}
	bool bAvoidLabels = false;
	auto ReserveHorizontal = [&](float X1, float X2, float PreferredY)
	{
		const float Left = FMath::Min(X1, X2), Right = FMath::Max(X1, X2);
		float Y = FMath::Max(ColumnHeaderHeight, PreferredY);
		for (;; Y += WireTrackGap)
		{
			bool bBlocked = HorizontalTracks.ContainsByPredicate([&](const FHorizontalTrack& T)
			{
				return Left < T.Right && Right > T.Left && FMath::Abs(Y - T.Y) < WireTrackGap - 0.1f;
			});
			for (const auto& NodePosition : GraphPositions)
			{
				const FVector2D& P = NodePosition.Value;
				bBlocked |= Left < P.X + NodeSize.X + 12.0f && Right > P.X - 12.0f
					&& Y > P.Y - 12.0f && Y < P.Y + NodeSize.Y + 12.0f;
			}
			if (bAvoidLabels)
			{
				for (const FEdgeLayout& Other : EdgeLayouts)
				{
					if (Other.LabelSize.X <= 0.0) continue;
					bBlocked |= Left < Other.LabelPosition.X + Other.LabelSize.X + 12.0f
						&& Right > Other.LabelPosition.X - 12.0f
						&& Y > Other.LabelPosition.Y - 12.0f
						&& Y < Other.LabelPosition.Y + Other.LabelSize.Y + 12.0f;
				}
			}
			if (!bBlocked) break;
		}
		HorizontalTracks.Add({Left, Right, Y});
		GraphExtent.Y = FMath::Max(GraphExtent.Y, static_cast<double>(Y + WireTrackGap));
		return Y;
	};
	const float NodeExtentX = static_cast<float>(MaxLayoutColumn) * RouteColumnSpacing + NodeSize.X;
	for (TPair<int32, TArray<int32>>& Pair : EdgesByLabelLane)
	{
		Pair.Value.Sort([&](const int32 LeftIndex, const int32 RightIndex)
		{
			const float LeftTop = DesiredLabelTops[LeftIndex];
			const float RightTop = DesiredLabelTops[RightIndex];
			return FMath::IsNearlyEqual(LeftTop, RightTop)
				? LeftIndex < RightIndex
				: LeftTop < RightTop;
		});

		float NextLabelTop = ColumnHeaderHeight;
		int32 LabelTrack = 0;
		for (const int32 EdgeIndex : Pair.Value)
		{
			FEdgeLayout& Layout = EdgeLayouts[EdgeIndex];
			float LabelTop = FMath::Max(DesiredLabelTops[EdgeIndex], NextLabelTop);
			const bool bForward = LabelLanes[EdgeIndex] <= MaxLayoutColumn;
			const float LabelLeft = bForward
				? static_cast<float>(LabelLanes[EdgeIndex] + 1) * RouteColumnSpacing
					- EdgeLabelWidth - (2 * MaxTracks + 2) * WireTrackGap - EdgeEndpointStubLength
				: NodeExtentX + ReverseEdgeOffset + (2 * MaxTracks + 4) * WireTrackGap;
			Layout.LabelPosition = FVector2D(LabelLeft, LabelTop);
			Layout.LabelSize = FVector2D(EdgeLabelWidth, EdgeLabelHeight);
			const float SourceStubX = Layout.SourceAnchor.X + EdgeEndpointStubLength + SourceTracks[EdgeIndex] * WireTrackGap;
			const float TargetStubX = Layout.TargetAnchor.X - EdgeEndpointStubLength - TargetTracks[EdgeIndex] * WireTrackGap;
			const float RouteLeftX = LabelLeft - EdgeRouteMargin - LabelTrack * WireTrackGap;
			const float RouteRightX = LabelLeft + EdgeLabelWidth + EdgeRouteMargin + LabelTrack * WireTrackGap;
			++LabelTrack;
			const float LabelCenterY = ReserveHorizontal(RouteLeftX, RouteRightX, LabelTop + EdgeLabelHeight * 0.5f);
			LabelTop = LabelCenterY - EdgeLabelHeight * 0.5f;
			Layout.LabelPosition.Y = LabelTop;
			const float SourceTravelY = Layout.SourceAnchor.Y;
			const float TargetTravelY = Layout.TargetAnchor.Y;
			Layout.RoutePoints = {
				Layout.SourceAnchor,
				FVector2D(SourceStubX, Layout.SourceAnchor.Y),
				FVector2D(SourceStubX, SourceTravelY),
				FVector2D(RouteLeftX, SourceTravelY),
				FVector2D(RouteLeftX, LabelCenterY),
				FVector2D(RouteRightX, LabelCenterY),
				FVector2D(RouteRightX, TargetTravelY),
				FVector2D(TargetStubX, TargetTravelY),
				FVector2D(TargetStubX, Layout.TargetAnchor.Y),
				Layout.TargetAnchor
			};
			GraphExtent.X = FMath::Max(GraphExtent.X, static_cast<double>(RouteRightX + WireTrackGap));
			GraphExtent.Y = FMath::Max(GraphExtent.Y, LabelTop + EdgeLabelHeight);
			NextLabelTop = LabelTop + EdgeLabelHeight + EdgeLabelGap;
		}
	}
	// All labels are now known: travel lanes avoid their rectangles as well
	// as nodes and already reserved wires, so captions cannot hide a wire.
	bAvoidLabels = true;
	for (FEdgeLayout& Layout : EdgeLayouts)
	{
		if (Layout.RoutePoints.Num() != 10) continue;
		const float SourceY = ReserveHorizontal(Layout.RoutePoints[1].X, Layout.RoutePoints[3].X, Layout.SourceAnchor.Y);
		const float TargetY = ReserveHorizontal(Layout.RoutePoints[6].X, Layout.RoutePoints[7].X, Layout.TargetAnchor.Y);
		Layout.RoutePoints[2].Y = SourceY;
		Layout.RoutePoints[3].Y = SourceY;
		Layout.RoutePoints[6].Y = TargetY;
		Layout.RoutePoints[7].Y = TargetY;
	}
}

void SSFPGraphPanel::FitGraph()
{
	FVector2D ReferenceViewport = GetCachedGeometry().GetLocalSize();
	if (ReferenceViewport.X < 100.0f || ReferenceViewport.Y < 100.0f)
	{
		ReferenceViewport = FVector2D(900.0f, 650.0f);
	}

	if (GraphExtent.X > 0.0f && GraphExtent.Y > 0.0f)
	{
		const float FitZoom = FMath::Min(
			(ReferenceViewport.X - 64.0f) / GraphExtent.X,
			(ReferenceViewport.Y - 80.0f) / GraphExtent.Y);
		Zoom = FMath::Clamp(FitZoom, MinimumGraphZoom, 1.0f);
		const float PaintedWidth = GraphExtent.X * Zoom;
		const float PaintedHeight = GraphExtent.Y * Zoom;
		Pan = FVector2D(
			(ReferenceViewport.X - PaintedWidth) * 0.5f,
			(ReferenceViewport.Y - PaintedHeight) * 0.5f);
	}
	else
	{
		Pan = FVector2D(32.0f, 48.0f);
		Zoom = 1.0f;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SSFPGraphPanel::ResetView()
{
	FVector2D ReferenceViewport = GetCachedGeometry().GetLocalSize();
	if (ReferenceViewport.X < 100.0f || ReferenceViewport.Y < 100.0f)
	{
		ReferenceViewport = FVector2D(900.0f, 650.0f);
	}
	if (GraphExtent.X > 0.0f && GraphExtent.Y > 0.0f)
	{
		const float FitZoom = FMath::Clamp(FMath::Min(
			(ReferenceViewport.X - 64.0f) / GraphExtent.X,
			(ReferenceViewport.Y - 80.0f) / GraphExtent.Y), MinimumGraphZoom, 1.0f);
		// A complete 10-column fit reduced the r31 example to roughly 18 percent,
		// which made otherwise generous graph spacing look cramped. Start at a
		// readable scale instead; panning is intentional for wide factory plans.
		Zoom = FMath::Clamp(FMath::Max(FitZoom, MinimumReadableZoom), MinimumGraphZoom, 1.0f);
		const float PaintedWidth = GraphExtent.X * Zoom;
		const float PaintedHeight = GraphExtent.Y * Zoom;
		Pan = FVector2D(
			PaintedWidth <= ReferenceViewport.X - 64.0f
				? (ReferenceViewport.X - PaintedWidth) * 0.5f
				: 42.0f,
			(ReferenceViewport.Y - PaintedHeight) * 0.5f);
	}
	else
	{
		Pan = FVector2D(32.0f, 48.0f);
		Zoom = 1.0f;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SSFPGraphPanel::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(900.0f, 650.0f);
}

void SSFPGraphPanel::AutoArrange()
{
	ManualNodeOffsets.Reset();
	RebuildLayout();
	ResetView();
}


FLinearColor SSFPGraphPanel::GetNodeColor(const ESFPPlanNodeType Type) const
{
	switch (Type)
	{
	case ESFPPlanNodeType::Generator:
		return FLinearColor(0.66f, 0.48f, 0.035f, 0.99f);
	case ESFPPlanNodeType::Machine:
		return FLinearColor(0.075f, 0.235f, 0.29f, 0.99f);
	case ESFPPlanNodeType::Target:
		return FLinearColor(0.82f, 0.31f, 0.035f, 0.99f);
	case ESFPPlanNodeType::Byproduct:
		return FLinearColor(0.55f, 0.35f, 0.045f, 0.99f);
	case ESFPPlanNodeType::Cycle:
		return FLinearColor(0.52f, 0.055f, 0.045f, 0.99f);
	case ESFPPlanNodeType::Splitter:
		return FLinearColor(0.68f, 0.245f, 0.035f, 0.99f);
	case ESFPPlanNodeType::Merger:
		return FLinearColor(0.075f, 0.38f, 0.36f, 0.99f);
	default:
		return FLinearColor(0.20f, 0.22f, 0.225f, 0.99f);
	}
}

FLinearColor SSFPGraphPanel::GetEdgeColor(const FSFPPlanEdge& Edge) const
{
	// Power is intentionally kept outside the material palette. It is not a
	// transported item and should remain instantly recognisable in every plan.
	if (Edge.Form == TEXT("power"))
	{
		return FLinearColor(1.0f, 0.82f, 0.16f, 1.0f);
	}

	// r56: colour material flow by the actual product, not by its physical form.
	// Previously every liquid was blue and every gas purple, which made large
	// Satisfactory Plus graphs collapse into a handful of visually identical
	// routes. A material now keeps one deterministic colour through machines,
	// splitters, mergers, resource links and storage endpoints.
	static const FLinearColor MaterialPalette[] = {
		FLinearColor(1.00f, 0.42f, 0.18f, 1.0f), // vivid orange-red
		FLinearColor(0.37f, 0.86f, 0.32f, 1.0f), // green
		FLinearColor(0.13f, 0.78f, 0.96f, 1.0f), // cyan
		FLinearColor(1.00f, 0.35f, 0.68f, 1.0f), // pink
		FLinearColor(0.39f, 0.56f, 1.00f, 1.0f), // blue
		FLinearColor(1.00f, 0.78f, 0.18f, 1.0f), // amber
		FLinearColor(0.70f, 0.43f, 1.00f, 1.0f), // violet
		FLinearColor(1.00f, 0.55f, 0.34f, 1.0f), // coral
		FLinearColor(0.24f, 0.91f, 0.68f, 1.0f), // mint
		FLinearColor(0.24f, 0.68f, 1.00f, 1.0f), // sky blue
		FLinearColor(0.89f, 0.47f, 0.95f, 1.0f), // orchid
		FLinearColor(0.77f, 0.88f, 0.24f, 1.0f), // lime
		FLinearColor(1.00f, 0.30f, 0.30f, 1.0f), // red
		FLinearColor(0.14f, 0.88f, 0.82f, 1.0f), // turquoise
		FLinearColor(0.78f, 0.68f, 1.00f, 1.0f), // lavender
		FLinearColor(0.95f, 0.66f, 0.12f, 1.0f), // gold
		FLinearColor(1.00f, 0.48f, 0.52f, 1.0f), // salmon
		FLinearColor(0.25f, 0.88f, 0.92f, 1.0f), // aqua
		FLinearColor(0.56f, 0.72f, 1.00f, 1.0f), // periwinkle
		FLinearColor(0.94f, 0.35f, 0.86f, 1.0f), // fuchsia
		FLinearColor(0.58f, 0.92f, 0.24f, 1.0f), // chartreuse
		FLinearColor(1.00f, 0.68f, 0.26f, 1.0f), // light orange
		FLinearColor(0.58f, 0.37f, 0.96f, 1.0f), // purple
		FLinearColor(0.26f, 0.82f, 0.58f, 1.0f), // sea green
		FLinearColor(1.00f, 0.38f, 0.22f, 1.0f), // vermilion
		FLinearColor(0.16f, 0.72f, 0.70f, 1.0f), // teal
		FLinearColor(0.88f, 0.56f, 1.00f, 1.0f), // light purple
		FLinearColor(0.88f, 0.84f, 0.20f, 1.0f), // yellow-green
		FLinearColor(1.00f, 0.64f, 0.48f, 1.0f), // peach
		FLinearColor(0.20f, 0.52f, 1.00f, 1.0f), // electric blue
		FLinearColor(1.00f, 0.42f, 0.58f, 1.0f), // rose
		FLinearColor(0.22f, 0.76f, 0.43f, 1.0f), // jade
		FLinearColor(0.98f, 0.52f, 0.08f, 1.0f), // deep orange
		FLinearColor(0.48f, 0.90f, 0.88f, 1.0f), // pale cyan
		FLinearColor(0.64f, 0.82f, 1.00f, 1.0f), // ice blue
		FLinearColor(0.92f, 0.72f, 0.42f, 1.0f)  // sand
	};

	const FString& ColourKey = Edge.ItemClassPath.IsEmpty() ? Edge.ItemName : Edge.ItemClassPath;
	if (!ColourKey.IsEmpty())
	{
		return MaterialPalette[GetTypeHash(ColourKey) % UE_ARRAY_COUNT(MaterialPalette)];
	}

	// Only malformed/metadata-only edges without any material identity fall back
	// to a form colour. Normal production links should never need this path.
	if (Edge.Form == TEXT("liquid"))
	{
		return FLinearColor(0.12f, 0.62f, 1.0f, 1.0f);
	}
	if (Edge.Form == TEXT("gas"))
	{
		return FLinearColor(0.80f, 0.38f, 1.0f, 1.0f);
	}
	if (Edge.bLocalRoutingLink || Edge.TransportKind == TEXT("resource_node"))
	{
		return FLinearColor(0.18f, 0.86f, 0.73f, 1.0f);
	}
	return FLinearColor(0.72f, 0.76f, 0.82f, 1.0f);
}

FVector2D SSFPGraphPanel::TransformGraphPoint(const FVector2D& GraphPoint) const
{
	return GraphPoint * Zoom + Pan;
}

int32 SSFPGraphPanel::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush,
		ESlateDrawEffect::None,
		FLinearColor(0.018f, 0.026f, 0.032f, 1.0f));

	if (!Plan.IsValid() || Plan->Nodes.IsEmpty())
	{
		static const FText EmptyGraphText = SFPLocalization::Text(TEXT("Produkt wählen und auf 'Berechnen' klicken"));
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(700.0f, 40.0f),
				FSlateLayoutTransform(FVector2D(32.0f, 32.0f))),
			EmptyGraphText,
			FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16),
			ESlateDrawEffect::None,
			FLinearColor(0.62f, 0.66f, 0.72f, 1.0f));
		return LayerId + 1;
	}
	const FVector2D ViewSize = AllottedGeometry.GetLocalSize();
	const bool bShowEdgeLabels = Zoom >= 0.30f;
	const bool bShowNodeDetails = Zoom >= 0.30f;
	const bool bShowCompactNodeTitles = Zoom >= 0.15f;
	const bool bShowColumnHeaders = Zoom >= 0.20f;
	const bool bShowRoutingSymbols = Zoom >= 0.18f;

	// Alternating stage bands and explicit headings make the left-to-right
	// production order visible before individual labels are read.
	const int32 ColumnLayer = LayerId + 1;
	const float HalfColumnGap = (RouteColumnSpacing - NodeSize.X) * 0.5f;
	for (int32 Column = 0; Column <= MaxLayoutColumn; ++Column)
	{
		const float NodeGraphX = static_cast<float>(Column) * RouteColumnSpacing;
		const float BandGraphLeft = Column == 0 ? 0.0f : NodeGraphX - HalfColumnGap;
		const float BandGraphRight = Column == MaxLayoutColumn
			? NodeGraphX + NodeSize.X
			: NodeGraphX + NodeSize.X + HalfColumnGap;
		const float ColumnLeft = TransformGraphPoint(FVector2D(BandGraphLeft, 0.0f)).X;
		const float ColumnRight = TransformGraphPoint(FVector2D(BandGraphRight, 0.0f)).X;
		const float ColumnWidth = ColumnRight - ColumnLeft;
		if (ColumnRight < 0.0f || ColumnLeft > ViewSize.X)
		{
			continue;
		}
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			ColumnLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(ColumnWidth, ViewSize.Y),
				FSlateLayoutTransform(FVector2D(ColumnLeft, 0.0f))),
			WhiteBrush,
			ESlateDrawEffect::None,
			Column % 2 == 0
				? FLinearColor(0.08f, 0.12f, 0.15f, 0.24f)
				: FLinearColor(0.10f, 0.14f, 0.17f, 0.15f));

		if (Column > 0)
		{
			const TArray<FVector2f> Separator = {
				FVector2f(ColumnLeft, 0.0f),
				FVector2f(ColumnLeft, static_cast<float>(ViewSize.Y))
			};
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				ColumnLayer + 1,
				AllottedGeometry.ToPaintGeometry(),
				Separator,
				ESlateDrawEffect::None,
				FLinearColor(0.22f, 0.31f, 0.36f, 0.32f),
				true,
				1.0f);
		}

		if (bShowColumnHeaders && ColumnTitles.IsValidIndex(Column))
		{
			const FVector2D HeaderPosition = TransformGraphPoint(
				FVector2D(NodeGraphX, 12.0f));
			const FVector2D HeaderSize(NodeSize.X * Zoom, 34.0f * Zoom);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				ColumnLayer + 1,
				AllottedGeometry.ToPaintGeometry(HeaderSize, FSlateLayoutTransform(HeaderPosition)),
				WhiteBrush,
				ESlateDrawEffect::None,
				FLinearColor(0.055f, 0.09f, 0.115f, 0.96f));
			FSlateDrawElement::MakeText(
				OutDrawElements,
				ColumnLayer + 2,
				AllottedGeometry.ToPaintGeometry(
					HeaderSize - FVector2D(16.0f, 4.0f) * Zoom,
					FSlateLayoutTransform(HeaderPosition + FVector2D(8.0f, 2.0f) * Zoom)),
				ColumnTitles[Column],
				FCoreStyle::GetDefaultFontStyle(
					TEXT("Bold"), FMath::Max(7, FMath::RoundToInt(10.0f * Zoom))),
				ESlateDrawEffect::None,
				FLinearColor(0.96f, 0.67f, 0.18f, 1.0f));
		}
	}

	const int32 EdgeLayer = ColumnLayer + 3;
	// Compact route captions remain readable independently of the large
	// transport cards. Reserve visible geometry to avoid hiding other wires.
	TArray<FSlateRect> CaptionObstacles;
	auto ReserveCaptionRect = [&](const FVector2D& P, const FVector2D& Size, float Margin)
	{
		CaptionObstacles.Add(FSlateRect(P.X - Margin, P.Y - Margin,
			P.X + Size.X + Margin, P.Y + Size.Y + Margin));
	};
	for (const auto& Position : GraphPositions)
		ReserveCaptionRect(TransformGraphPoint(Position.Value), NodeSize * Zoom, 6.0f);
	for (const FEdgeLayout& Route : EdgeLayouts)
	{
		if (bShowEdgeLabels) ReserveCaptionRect(TransformGraphPoint(Route.LabelPosition), Route.LabelSize * Zoom, 6.0f);
		for (int32 I = 1; I < Route.RoutePoints.Num(); ++I)
		{
			const FVector2D A = TransformGraphPoint(Route.RoutePoints[I - 1]);
			const FVector2D B = TransformGraphPoint(Route.RoutePoints[I]);
			ReserveCaptionRect(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)),
				FVector2D(FMath::Abs(B.X - A.X), FMath::Abs(B.Y - A.Y)), 4.0f);
		}
	}
	const FSlateFontInfo RouteCaptionFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
	const TSharedRef<FSlateFontMeasure> CaptionMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	for (int32 EdgeIndex = 0; EdgeIndex < Plan->Edges.Num(); ++EdgeIndex)
	{
		if (!EdgeLayouts.IsValidIndex(EdgeIndex)
			|| EdgeLayouts[EdgeIndex].RoutePoints.Num() < 2)
		{
			continue;
		}
		const FSFPPlanEdge& Edge = Plan->Edges[EdgeIndex];
		const FEdgeLayout& Layout = EdgeLayouts[EdgeIndex];
		TArray<FVector2f> Points;
		Points.Reserve(Layout.RoutePoints.Num());
		FVector2D FirstPoint = TransformGraphPoint(Layout.RoutePoints[0]);
		double EdgeMinX = FirstPoint.X;
		double EdgeMaxX = FirstPoint.X;
		double EdgeMinY = FirstPoint.Y;
		double EdgeMaxY = FirstPoint.Y;
		for (const FVector2D& GraphPoint : Layout.RoutePoints)
		{
			const FVector2D Point = TransformGraphPoint(GraphPoint);
			Points.Add(FVector2f(static_cast<float>(Point.X), static_cast<float>(Point.Y)));
			EdgeMinX = FMath::Min(EdgeMinX, Point.X);
			EdgeMaxX = FMath::Max(EdgeMaxX, Point.X);
			EdgeMinY = FMath::Min(EdgeMinY, Point.Y);
			EdgeMaxY = FMath::Max(EdgeMaxY, Point.Y);
		}
		if (EdgeMaxX < -260.0f || EdgeMinX > ViewSize.X + 260.0f
			|| EdgeMaxY < -70.0f || EdgeMinY > ViewSize.Y + 70.0f)
		{
			continue;
		}
		FLinearColor EdgeColor = GetEdgeColor(Edge);
		const bool bHoverFilter = HoveredNodeId != INDEX_NONE || HoveredPortNodeId != INDEX_NONE;
		const bool bMatchesHoveredPort = HoveredPortNodeId != INDEX_NONE
			&& (Edge.SourceNodeId == HoveredPortNodeId || Edge.TargetNodeId == HoveredPortNodeId)
			&& (HoveredPortItemClassPath.IsEmpty()
				|| Edge.ItemClassPath == HoveredPortItemClassPath
				|| (Edge.ItemClassPath.IsEmpty() && Edge.ItemName == HoveredPortItemClassPath));
		const bool bHighlightedEdge = bMatchesHoveredPort
			|| (HoveredPortNodeId == INDEX_NONE && HoveredNodeId != INDEX_NONE
				&& (Edge.SourceNodeId == HoveredNodeId || Edge.TargetNodeId == HoveredNodeId));
		if (bHoverFilter && !bHighlightedEdge)
		{
			EdgeColor.A *= 0.18f;
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			EdgeLayer,
			AllottedGeometry.ToPaintGeometry(),
			Points,
			ESlateDrawEffect::None,
			FLinearColor(0.004f, 0.008f, 0.012f, 0.98f),
			true,
			FMath::Max(3.8f, 5.5f * Zoom));
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			EdgeLayer,
			AllottedGeometry.ToPaintGeometry(),
			Points,
			ESlateDrawEffect::None,
			EdgeColor,
			true,
			bHighlightedEdge ? FMath::Max(2.8f, 4.2f * Zoom) : FMath::Max(1.5f, 2.5f * Zoom));

		const FVector2D End = TransformGraphPoint(Layout.RoutePoints.Last());
		const FVector2D BeforeEnd = TransformGraphPoint(
			Layout.RoutePoints[Layout.RoutePoints.Num() - 2]);
		const FVector2D DirectionVector = End - BeforeEnd;
		const double DirectionLength = FMath::Sqrt(
			DirectionVector.X * DirectionVector.X + DirectionVector.Y * DirectionVector.Y);
		const FVector2D Direction = DirectionLength > KINDA_SMALL_NUMBER
			? DirectionVector / DirectionLength
			: FVector2D(1.0f, 0.0f);
		const FVector2D Perpendicular(-Direction.Y, Direction.X);
		const float ArrowSize = FMath::Clamp(10.0f * Zoom, 7.0f, 16.0f);
		TArray<FVector2f> Arrow = {
			FVector2f(static_cast<float>(End.X), static_cast<float>(End.Y)),
			FVector2f(
				static_cast<float>((End - Direction * ArrowSize + Perpendicular * ArrowSize * 0.65f).X),
				static_cast<float>((End - Direction * ArrowSize + Perpendicular * ArrowSize * 0.65f).Y)),
			FVector2f(
				static_cast<float>((End - Direction * ArrowSize - Perpendicular * ArrowSize * 0.65f).X),
				static_cast<float>((End - Direction * ArrowSize - Perpendicular * ArrowSize * 0.65f).Y)),
			FVector2f(static_cast<float>(End.X), static_cast<float>(End.Y))
		};
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			EdgeLayer + 1,
			AllottedGeometry.ToPaintGeometry(),
			Arrow,
			ESlateDrawEffect::None,
			EdgeColor,
			true,
			FMath::Max(1.5f, 2.5f * Zoom));

		// Repeat direction markers along the ordered producer-to-consumer route.
		// Using each segment's vector also handles leftward and vertical returns.
		auto DrawFlowMarker = [&](const FVector2D& Tip, const FVector2D& Along)
		{
			if (Tip.X < -20.0 || Tip.X > ViewSize.X + 20.0
				|| Tip.Y < -20.0 || Tip.Y > ViewSize.Y + 20.0) return;
			if (bShowEdgeLabels)
			{
				const FVector2D LabelTopLeft = TransformGraphPoint(Layout.LabelPosition);
				const FVector2D LabelBottomRight = LabelTopLeft + Layout.LabelSize * Zoom;
				if (Tip.X >= LabelTopLeft.X - ArrowSize * 2 && Tip.X <= LabelBottomRight.X + ArrowSize * 2
					&& Tip.Y >= LabelTopLeft.Y - ArrowSize * 2 && Tip.Y <= LabelBottomRight.Y + ArrowSize * 2) return;
			}
			const FVector2D Side(-Along.Y, Along.X);
			const FVector2D Left = Tip - Along * ArrowSize + Side * ArrowSize * 0.65f;
			const FVector2D Right = Tip - Along * ArrowSize - Side * ArrowSize * 0.65f;
			TArray<FVector2f> Marker = {
				FVector2f(static_cast<float>(Left.X), static_cast<float>(Left.Y)),
				FVector2f(static_cast<float>(Tip.X), static_cast<float>(Tip.Y)),
				FVector2f(static_cast<float>(Right.X), static_cast<float>(Right.Y))};
			FSlateDrawElement::MakeLines(OutDrawElements, EdgeLayer + 1,
				AllottedGeometry.ToPaintGeometry(), Marker, ESlateDrawEffect::None,
				FLinearColor(0.01f, 0.015f, 0.02f, 1.0f), true, 5.0f);
			FSlateDrawElement::MakeLines(OutDrawElements, EdgeLayer + 1,
				AllottedGeometry.ToPaintGeometry(), Marker, ESlateDrawEffect::None,
				EdgeColor, true, 2.5f);
		};
		for (int32 Segment = 1; Segment < Layout.RoutePoints.Num(); ++Segment)
		{
			const FVector2D Start = TransformGraphPoint(Layout.RoutePoints[Segment - 1]);
			const FVector2D Finish = TransformGraphPoint(Layout.RoutePoints[Segment]);
			const FVector2D Delta = Finish - Start;
			const double Length = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);
			if (Length < ArrowSize * 4.0) continue;
			const FVector2D Along = Delta / Length;
			const int32 Markers = FMath::Clamp(FMath::FloorToInt(Length / 120.0), 1, 64);
			for (int32 Index = 0; Index < Markers; ++Index)
			{
				const double Distance = Length * (static_cast<double>(Index) + 0.5) / Markers;
				DrawFlowMarker(Start + Along * Distance, Along);
			}
			// Include markers near long segment ends even when a label hides its centre.
			if (Length > 120.0)
			{
				DrawFlowMarker(Start + Along * (ArrowSize * 2.5), Along);
				DrawFlowMarker(Finish - Along * (ArrowSize * 2.5), Along);
			}
		}

		// Use the actual item and flow rate; never infer a material from colour.
		if (Zoom >= 0.25f)
		{
			const bool bVolume = Edge.Form == TEXT("liquid") || Edge.Form == TEXT("gas");
			const FString Caption = FString::Printf(TEXT("%s · %s%s"), *Edge.ItemName,
				*FSFPNumberFormatting::Decimal(Edge.RatePerMinute, 2),
				Edge.Form == TEXT("power") ? TEXT(" MW") : (bVolume ? TEXT(" m³/min") : TEXT("/min")));
			const FText CaptionText = SFPLocalization::Text(Caption);
			const FVector2D CaptionSize = FVector2D(CaptionMeasure->Measure(CaptionText, RouteCaptionFont)) + FVector2D(16.0f, 8.0f);
			int32 CaptionCount = 0;
			for (int32 Segment = 1; Segment < Layout.RoutePoints.Num() && CaptionCount < 3; ++Segment)
			{
				const FVector2D A = TransformGraphPoint(Layout.RoutePoints[Segment - 1]);
				const FVector2D B = TransformGraphPoint(Layout.RoutePoints[Segment]);
				const bool bVertical = FMath::Abs(B.X - A.X) < 0.5f;
				const double Length = bVertical ? FMath::Abs(B.Y - A.Y) : FMath::Abs(B.X - A.X);
				if (Length < (bVertical ? 80.0 : CaptionSize.X + 32.0)) continue;
				FVector2D Mid = (A + B) * 0.5;
				if (bVertical)
					Mid.Y = (FMath::Max(0.0, FMath::Min(A.Y, B.Y)) + FMath::Min(static_cast<double>(ViewSize.Y), FMath::Max(A.Y, B.Y))) * 0.5;
				else
					Mid.X = (FMath::Max(0.0, FMath::Min(A.X, B.X)) + FMath::Min(static_cast<double>(ViewSize.X), FMath::Max(A.X, B.X))) * 0.5;
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const FVector2D P = bVertical
						? FVector2D(Mid.X + (Side == 0 ? 14.0 : -CaptionSize.X - 14.0), Mid.Y - CaptionSize.Y * 0.5)
						: FVector2D(Mid.X - CaptionSize.X * 0.5, Mid.Y + (Side == 0 ? -CaptionSize.Y - 12.0 : 12.0));
					if (P.X < 0 || P.Y < 0 || P.X + CaptionSize.X > ViewSize.X || P.Y + CaptionSize.Y > ViewSize.Y) continue;
					const bool bOccupied = CaptionObstacles.ContainsByPredicate([&](const FSlateRect& R)
					{
						return P.X < R.Right && P.X + CaptionSize.X > R.Left
							&& P.Y < R.Bottom && P.Y + CaptionSize.Y > R.Top;
					});
					if (bOccupied) continue;
					FSlateDrawElement::MakeBox(OutDrawElements, EdgeLayer + 2,
						AllottedGeometry.ToPaintGeometry(CaptionSize, FSlateLayoutTransform(P)), WhiteBrush,
						ESlateDrawEffect::None, FLinearColor(0.012f, 0.018f, 0.027f, 0.97f));
					FSlateDrawElement::MakeBox(OutDrawElements, EdgeLayer + 2,
						AllottedGeometry.ToPaintGeometry(FVector2D(3.0f, CaptionSize.Y), FSlateLayoutTransform(P)), WhiteBrush,
						ESlateDrawEffect::None, EdgeColor);
					FSlateDrawElement::MakeText(OutDrawElements, EdgeLayer + 3,
						AllottedGeometry.ToPaintGeometry(CaptionSize, FSlateLayoutTransform(P + FVector2D(8.0f, 4.0f))),
						CaptionText, RouteCaptionFont, ESlateDrawEffect::None, FLinearColor::White);
					ReserveCaptionRect(P, CaptionSize, 8.0f);
					++CaptionCount;
					break;
				}
			}
		}

		if (bShowEdgeLabels)
		{
			const FVector2D LabelPosition = TransformGraphPoint(Layout.LabelPosition);
			const FVector2D LabelSize = Layout.LabelSize * Zoom;

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				EdgeLayer + 1,
				AllottedGeometry.ToPaintGeometry(
					LabelSize,
					FSlateLayoutTransform(LabelPosition)),
				WhiteBrush,
				ESlateDrawEffect::None,
				FLinearColor(0.012f, 0.018f, 0.027f, 0.96f));

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				EdgeLayer + 2,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(FMath::Max(2.0f, 5.0f * Zoom), LabelSize.Y),
					FSlateLayoutTransform(LabelPosition)),
				WhiteBrush,
				ESlateDrawEffect::None,
				EdgeColor);

			FSlateDrawElement::MakeText(
				OutDrawElements,
				EdgeLayer + 3,
				AllottedGeometry.ToPaintGeometry(
					LabelSize - FVector2D(22.0f, 6.0f) * Zoom,
					FSlateLayoutTransform(LabelPosition + FVector2D(13.0f, 3.0f) * Zoom)),
				EdgeLabels.IsValidIndex(EdgeIndex) ? EdgeLabels[EdgeIndex] : FText::GetEmpty(),
				FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FMath::Max(8, FMath::RoundToInt(10.0f * Zoom))),
				ESlateDrawEffect::None,
				FLinearColor::White);
		}
	}

	const int32 NodeLayer = EdgeLayer + 4;
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		const FVector2D* GraphPosition = GraphPositions.Find(Node.Id);
		if (GraphPosition == nullptr)
		{
			continue;
		}
		const FVector2D Position = TransformGraphPoint(*GraphPosition);
		const FText* Title = NodeTitles.Find(Node.Id);
		const FText* RecipeTitle = NodeRecipeTitles.Find(Node.Id);
		const FText* MachineMeta = NodeMachineMeta.Find(Node.Id);
		const FText* Footer = NodeFooter.Find(Node.Id);

		const FVector2D PaintedSize = NodeSize * Zoom;
		if (Position.X + PaintedSize.X < 0.0f || Position.X > ViewSize.X
			|| Position.Y + PaintedSize.Y < 0.0f || Position.Y > ViewSize.Y)
		{
			continue;
		}
		const bool bNodeHighlighted = Node.Id == HoveredNodeId || Node.Id == HoveredPortNodeId;
		const FLinearColor OutlineColor = bNodeHighlighted
			? FLinearColor(0.20f, 0.82f, 1.0f, 1.0f)
			: (Node.bCompleted
				? FLinearColor(0.18f, 0.88f, 0.48f, 1.0f)
				: FLinearColor(0.96f, 0.43f, 0.055f, 1.0f));
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			NodeLayer,
			AllottedGeometry.ToPaintGeometry(PaintedSize, FSlateLayoutTransform(Position)),
			WhiteBrush,
			ESlateDrawEffect::None,
			OutlineColor);

		const FVector2D NodeInset((bNodeHighlighted ? 3.0f : 2.0f) * Zoom, (bNodeHighlighted ? 3.0f : 2.0f) * Zoom);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			NodeLayer + 1,
			AllottedGeometry.ToPaintGeometry(
				PaintedSize - NodeInset * 2.0f,
				FSlateLayoutTransform(Position + NodeInset)),
			WhiteBrush,
			ESlateDrawEffect::None,
			Node.bCompleted
				? FLinearColor(0.045f, 0.245f, 0.145f, 0.99f)
				: GetNodeColor(Node.Type));

		const FSlateRect CompletionRect = CompletionBoxForNode(Position, PaintedSize, Zoom);
		const FVector2D CompletionPosition(CompletionRect.Left, CompletionRect.Top);
		const FVector2D CompletionSize(
			CompletionRect.Right - CompletionRect.Left,
			CompletionRect.Bottom - CompletionRect.Top);
		const bool bWrappedRecipeTitle = RecipeTitle != nullptr && RecipeTitle->ToString().Contains(TEXT("\n"));
		const float HeaderHeight = bWrappedRecipeTitle ? 48.0f : 30.0f;
		const float MachineMetaTop = bWrappedRecipeTitle ? 56.0f : 40.0f;
		const float SeparatorTop = bWrappedRecipeTitle ? 98.0f : 82.0f;
		const float SectionHeaderTop = bWrappedRecipeTitle ? 108.0f : 92.0f;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			NodeLayer + 2,
			AllottedGeometry.ToPaintGeometry(CompletionSize, FSlateLayoutTransform(CompletionPosition)),
			WhiteBrush,
			ESlateDrawEffect::None,
			Node.bCompleted
				? FLinearColor(0.12f, 0.82f, 0.40f, 1.0f)
				: FLinearColor(0.13f, 0.16f, 0.18f, 1.0f));

		if (Node.bCompleted)
		{
			const float Left = CompletionRect.Left;
			const float Top = CompletionRect.Top;
			const float Width = CompletionSize.X;
			const float Height = CompletionSize.Y;
			TArray<FVector2f> CheckMark = {
				FVector2f(Left + Width * 0.20f, Top + Height * 0.54f),
				FVector2f(Left + Width * 0.43f, Top + Height * 0.76f),
				FVector2f(Left + Width * 0.82f, Top + Height * 0.25f)
			};
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				NodeLayer + 3,
				AllottedGeometry.ToPaintGeometry(),
				CheckMark,
				ESlateDrawEffect::None,
				FLinearColor::White,
				true,
				FMath::Max(1.5f, 2.3f * Zoom));
		}

		// Zoom LOD: full recipe title above 30 %, compact machine/node title from
		// 15-30 %, and no persistent title below 15 %. A hovered node keeps a
		// compact title so the overview remains navigable even at 5 % zoom.
		const bool bShowNodeTitle = bShowCompactNodeTitles || bNodeHighlighted;
		if (bShowNodeTitle)
		{
			const FText HeaderText = bShowNodeDetails
				? (RecipeTitle != nullptr ? *RecipeTitle : (Title != nullptr ? *Title : FText::GetEmpty()))
				: (Title != nullptr ? *Title : (RecipeTitle != nullptr ? *RecipeTitle : FText::GetEmpty()));
			const float TitleFontSize = bShowNodeDetails ? 13.0f : 10.0f;
			const float TitleHeight = bShowNodeDetails ? HeaderHeight : 24.0f;
			FSlateDrawElement::MakeText(
				OutDrawElements,
				NodeLayer + 4,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(FMath::Max(36.0, PaintedSize.X - CompletionSize.X - 30.0 * static_cast<double>(Zoom)), TitleHeight * Zoom),
					FSlateLayoutTransform(Position + FVector2D(12.0f, 8.0f) * Zoom)),
				HeaderText,
				FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), bShowNodeDetails
					? FMath::Max(9, FMath::RoundToInt(TitleFontSize * Zoom))
					: (bShowCompactNodeTitles ? FMath::Max(6, FMath::RoundToInt(TitleFontSize * Zoom)) : 9)),
				ESlateDrawEffect::None,
				FLinearColor::White);
		}

		if (bShowNodeDetails && MachineMeta != nullptr)
		{
			FSlateDrawElement::MakeText(
				OutDrawElements,
				NodeLayer + 4,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(NodeSize.X - 24.0f, 24.0f) * Zoom,
					FSlateLayoutTransform(Position + FVector2D(12.0f, MachineMetaTop) * Zoom)),
				*MachineMeta,
				FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FMath::Max(7, FMath::RoundToInt(9.0f * Zoom))),
				ESlateDrawEffect::None,
				FLinearColor(0.82f, 0.86f, 0.91f, 1.0f));
		}

		// Separators and fixed input/output hierarchy are detail LOD only.
		if (bShowNodeDetails)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, NodeLayer + 3,
				AllottedGeometry.ToPaintGeometry(FVector2D((NodeSize.X - 20.0f) * Zoom, 1.0f), FSlateLayoutTransform(Position + FVector2D(10.0f, SeparatorTop) * Zoom)),
				WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.35f, 0.42f, 0.48f, 0.7f));
		}
		if (!IsRoutingNodeType(Node.Type) && bShowNodeDetails)
		{
			static const FText InputHeader = SFPLocalization::Text(TEXT("EINGÄNGE"));
			static const FText OutputHeader = SFPLocalization::Text(TEXT("AUSGÄNGE"));
			FSlateDrawElement::MakeText(OutDrawElements, NodeLayer + 4,
				AllottedGeometry.ToPaintGeometry(FVector2D(245.0f, 24.0f) * Zoom, FSlateLayoutTransform(Position + FVector2D(14.0f, SectionHeaderTop) * Zoom)),
				InputHeader, FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FMath::Max(7, FMath::RoundToInt(8.0f * Zoom))), ESlateDrawEffect::None,
				FLinearColor(0.48f, 0.82f, 1.0f, 1.0f));
			FSlateDrawElement::MakeText(OutDrawElements, NodeLayer + 4,
				AllottedGeometry.ToPaintGeometry(FVector2D(245.0f, 24.0f) * Zoom, FSlateLayoutTransform(Position + FVector2D(NodeSize.X - 259.0f, SectionHeaderTop) * Zoom)),
				OutputHeader, FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FMath::Max(7, FMath::RoundToInt(8.0f * Zoom))), ESlateDrawEffect::None,
				FLinearColor(0.48f, 0.82f, 1.0f, 1.0f));

			auto DrawPortLabels = [&](const TArray<FPortDisplay>* Ports, const bool bOutgoing)
			{
				if (Ports == nullptr) return;
				for (const FPortDisplay& Port : *Ports)
				{
					const FVector2D Anchor = TransformGraphPoint(Port.GraphAnchor);
					const float TextWidth = 250.0f * Zoom;
					const FVector2D TextPos = bOutgoing
						? FVector2D(Position.X + PaintedSize.X - TextWidth - 14.0f * Zoom, Anchor.Y - 10.0f * Zoom)
						: FVector2D(Position.X + 14.0f * Zoom, Anchor.Y - 10.0f * Zoom);
					FSlateDrawElement::MakeText(OutDrawElements, NodeLayer + 5,
						AllottedGeometry.ToPaintGeometry(FVector2D(TextWidth, 22.0f * Zoom), FSlateLayoutTransform(TextPos)),
						Port.Label, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FMath::Max(7, FMath::RoundToInt(8.5f * Zoom))),
						ESlateDrawEffect::None, FLinearColor(0.93f, 0.95f, 0.98f, 1.0f));
				}
			};
			DrawPortLabels(InputPortsByNode.Find(Node.Id), false);
			DrawPortLabels(OutputPortsByNode.Find(Node.Id), true);
		}

		if (Footer != nullptr && !Footer->IsEmpty() && bShowNodeDetails)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, NodeLayer + 3,
				AllottedGeometry.ToPaintGeometry(FVector2D((NodeSize.X - 20.0f) * Zoom, 1.0f), FSlateLayoutTransform(Position + FVector2D(10.0f, NodeSize.Y - 43.0f) * Zoom)),
				WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.35f, 0.42f, 0.48f, 0.7f));
			FSlateDrawElement::MakeText(OutDrawElements, NodeLayer + 4,
				AllottedGeometry.ToPaintGeometry(FVector2D(NodeSize.X - 24.0f, 28.0f) * Zoom, FSlateLayoutTransform(Position + FVector2D(12.0f, NodeSize.Y - 34.0f) * Zoom)),
				*Footer, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FMath::Max(7, FMath::RoundToInt(8.5f * Zoom))), ESlateDrawEffect::None,
				FLinearColor(0.96f, 0.80f, 0.34f, 1.0f));
		}

		if (bShowRoutingSymbols && IsRoutingNodeType(Node.Type))
		{
			TArray<FRoutingWire> InputWires;
			TArray<FRoutingWire> OutputWires;
			if (const TArray<int32>* InputEdges = IncomingEdgesByNode.Find(Node.Id))
			{
				InputWires.Reserve(InputEdges->Num());
				for (const int32 EdgeIndex : *InputEdges)
				{
					if (Plan->Edges.IsValidIndex(EdgeIndex) && EdgeLayouts.IsValidIndex(EdgeIndex))
					{
						FRoutingWire& Wire = InputWires.AddDefaulted_GetRef();
						Wire.Anchor = TransformGraphPoint(EdgeLayouts[EdgeIndex].TargetAnchor);
						Wire.Color = GetEdgeColor(Plan->Edges[EdgeIndex]);
					}
				}
			}
			if (const TArray<int32>* OutputEdges = OutgoingEdgesByNode.Find(Node.Id))
			{
				OutputWires.Reserve(OutputEdges->Num());
				for (const int32 EdgeIndex : *OutputEdges)
				{
					if (Plan->Edges.IsValidIndex(EdgeIndex) && EdgeLayouts.IsValidIndex(EdgeIndex))
					{
						FRoutingWire& Wire = OutputWires.AddDefaulted_GetRef();
						Wire.Anchor = TransformGraphPoint(EdgeLayouts[EdgeIndex].SourceAnchor);
						Wire.Color = GetEdgeColor(Plan->Edges[EdgeIndex]);
					}
				}
			}
			DrawRoutingWiring(
				OutDrawElements,
				AllottedGeometry,
				NodeLayer + 5,
				Node.Type,
				Position,
				Zoom,
				InputWires,
				OutputWires);
		}
		else if (bShowRoutingSymbols && IsStorageNodeType(Node.Type)
			&& !Node.ClassPath.Equals(TEXT("SFP.PowerGrid")))
		{
			DrawStorageSymbol(
				OutDrawElements,
				AllottedGeometry,
				NodeLayer + 5,
				Position,
				Zoom,
				Node.Type == ESFPPlanNodeType::Byproduct);
		}
	}

	// Every edge endpoint receives a visible coloured port: sources and machines
	// expose outputs on the right, consumers and storage containers expose inputs
	// on the left, and routing nodes connect those same ports through their body.
	const int32 ConnectionPortLayer = NodeLayer + 8;
	for (int32 EdgeIndex = 0; EdgeIndex < Plan->Edges.Num(); ++EdgeIndex)
	{
		if (!EdgeLayouts.IsValidIndex(EdgeIndex)
			|| EdgeLayouts[EdgeIndex].RoutePoints.Num() < 2)
		{
			continue;
		}
		const FSFPPlanEdge& Edge = Plan->Edges[EdgeIndex];
		const FLinearColor PortColor = GetEdgeColor(Edge);
		auto DrawPort = [&](const FVector2D& GraphAnchor)
		{
			const FVector2D Center = TransformGraphPoint(GraphAnchor);
			const float PortSize = FMath::Clamp(8.0f * Zoom, 4.0f, 10.0f);
			const FVector2D PortExtent(PortSize, PortSize);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				ConnectionPortLayer,
				AllottedGeometry.ToPaintGeometry(
					PortExtent + FVector2D(2.0f, 2.0f),
					FSlateLayoutTransform(Center - PortExtent * 0.5f - FVector2D(1.0f, 1.0f))),
				WhiteBrush,
				ESlateDrawEffect::None,
				FLinearColor(0.01f, 0.015f, 0.02f, 1.0f));
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				ConnectionPortLayer + 1,
				AllottedGeometry.ToPaintGeometry(
					PortExtent,
					FSlateLayoutTransform(Center - PortExtent * 0.5f)),
				WhiteBrush,
				ESlateDrawEffect::None,
				PortColor);
		};

		DrawPort(EdgeLayouts[EdgeIndex].SourceAnchor);
		DrawPort(EdgeLayouts[EdgeIndex].TargetAnchor);
	}

	static const FText NavigationHelp = SFPLocalization::Text(
		TEXT("Mausrad: Zoom  ·  Mittlere/rechte Maustaste: verschieben  ·  Doppelklick: einpassen"));
	static const FText ColourHelp = SFPLocalization::Text(
		TEXT("Gleiche Farbe = gleiches Material  ·  Gelb: Strom  ·  Blau: Flüssigkeit  ·  Violett: Gas  ·  Türkis: lokaler Miner"));
	const FVector2D HelpSize(FMath::Min(760.0, FMath::Max(260.0, ViewSize.X - 24.0)), 42.0);
	const FVector2D HelpPosition(12.0, FMath::Max(0.0, ViewSize.Y - HelpSize.Y - 10.0));
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		ConnectionPortLayer + 2,
		AllottedGeometry.ToPaintGeometry(HelpSize, FSlateLayoutTransform(HelpPosition)),
		WhiteBrush,
		ESlateDrawEffect::None,
		FLinearColor(0.008f, 0.014f, 0.02f, 0.90f));
	FSlateDrawElement::MakeText(
		OutDrawElements,
		ConnectionPortLayer + 3,
		AllottedGeometry.ToPaintGeometry(
			HelpSize - FVector2D(16.0f, 22.0f),
			FSlateLayoutTransform(HelpPosition + FVector2D(8.0f, 3.0f))),
		NavigationHelp,
		FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8),
		ESlateDrawEffect::None,
		FLinearColor(0.78f, 0.83f, 0.88f, 1.0f));
	FSlateDrawElement::MakeText(
		OutDrawElements,
		ConnectionPortLayer + 3,
		AllottedGeometry.ToPaintGeometry(
			HelpSize - FVector2D(16.0f, 22.0f),
			FSlateLayoutTransform(HelpPosition + FVector2D(8.0f, 21.0f))),
		ColourHelp,
		FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8),
		ESlateDrawEffect::None,
		FLinearColor(0.96f, 0.67f, 0.18f, 1.0f));

	return ConnectionPortLayer + 3;
}

FReply SSFPGraphPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Plan.IsValid())
	{
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			const FVector2D* GraphPosition = GraphPositions.Find(Node.Id);
			if (GraphPosition == nullptr) continue;
			const FVector2D Position = TransformGraphPoint(*GraphPosition);
			const FVector2D PaintedSize = NodeSize * Zoom;
			if (NodeCompletionChanged.IsBound()
				&& ContainsPoint(CompletionBoxForNode(Position, PaintedSize, Zoom), LocalPosition))
			{
				NodeCompletionChanged.Execute(Node.Id, !Node.bCompleted);
				return FReply::Handled();
			}
			const FSlateRect NodeRect(Position.X, Position.Y, Position.X + PaintedSize.X, Position.Y + PaintedSize.Y);
			if (ContainsPoint(NodeRect, LocalPosition))
			{
				DraggedNodeId = Node.Id;
				bDraggingNode = true;
				HoveredNodeId = Node.Id;
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
		}
	}
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton
		|| MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bPanning = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Handled();
}

FReply SSFPGraphPanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDraggingNode)
	{
		bDraggingNode = false;
		DraggedNodeId = INDEX_NONE;
		// Re-route once after the drag. Solver/plan data is untouched.
		RebuildLayout();
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (bPanning)
	{
		bPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Handled();
}

FReply SSFPGraphPanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bPanning && HasMouseCapture())
	{
		Pan += MouseEvent.GetCursorDelta();
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}
	if (bDraggingNode && HasMouseCapture() && DraggedNodeId != INDEX_NONE)
	{
		const FVector2D GraphDelta = MouseEvent.GetCursorDelta() / FMath::Max(Zoom, 0.01f);
		ManualNodeOffsets.FindOrAdd(DraggedNodeId) += GraphDelta;
		if (FVector2D* Position = GraphPositions.Find(DraggedNodeId))
		{
			*Position += GraphDelta;
		}
		// Move only adjacent endpoint geometry during the live drag. Full routing
		// is recalculated on release, avoiding a graph rebuild on every mouse event.
		if (const TArray<int32>* Outgoing = OutgoingEdgesByNode.Find(DraggedNodeId))
		{
			for (const int32 EdgeIndex : *Outgoing)
			{
				if (!EdgeLayouts.IsValidIndex(EdgeIndex)) continue;
				FEdgeLayout& Layout = EdgeLayouts[EdgeIndex];
				Layout.SourceAnchor += GraphDelta;
				if (Layout.RoutePoints.Num() >= 2)
				{
					Layout.RoutePoints[0] += GraphDelta;
					Layout.RoutePoints[1] += GraphDelta;
				}
			}
		}
		if (const TArray<int32>* Incoming = IncomingEdgesByNode.Find(DraggedNodeId))
		{
			for (const int32 EdgeIndex : *Incoming)
			{
				if (!EdgeLayouts.IsValidIndex(EdgeIndex)) continue;
				FEdgeLayout& Layout = EdgeLayouts[EdgeIndex];
				Layout.TargetAnchor += GraphDelta;
				if (Layout.RoutePoints.Num() >= 2)
				{
					Layout.RoutePoints.Last() += GraphDelta;
					Layout.RoutePoints[Layout.RoutePoints.Num() - 2] += GraphDelta;
				}
			}
		}
		if (TArray<FPortDisplay>* Inputs = InputPortsByNode.Find(DraggedNodeId))
			for (FPortDisplay& Port : *Inputs) Port.GraphAnchor += GraphDelta;
		if (TArray<FPortDisplay>* Outputs = OutputPortsByNode.Find(DraggedNodeId))
			for (FPortDisplay& Port : *Outputs) Port.GraphAnchor += GraphDelta;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}

	if (!Plan.IsValid())
	{
		return FReply::Handled();
	}
	const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	int32 NewHoveredNode = INDEX_NONE;
	int32 NewHoveredPortNode = INDEX_NONE;
	FString NewHoveredPortItem;
	const float PortHitRadius = FMath::Clamp(18.0f * Zoom, 10.0f, 24.0f);
	for (const FSFPPlanNode& Node : Plan->Nodes)
	{
		const FVector2D* GraphPosition = GraphPositions.Find(Node.Id);
		if (GraphPosition == nullptr) continue;
		const FVector2D Position = TransformGraphPoint(*GraphPosition);
		const FVector2D PaintedSize = NodeSize * Zoom;
		const FSlateRect NodeRect(Position.X, Position.Y, Position.X + PaintedSize.X, Position.Y + PaintedSize.Y);
		if (ContainsPoint(NodeRect, LocalPosition)) NewHoveredNode = Node.Id;
		auto CheckPorts = [&](const TArray<FPortDisplay>* Ports)
		{
			if (Ports == nullptr) return;
			for (const FPortDisplay& Port : *Ports)
			{
				const FVector2D Center = TransformGraphPoint(Port.GraphAnchor);
				if (FVector2D::Distance(Center, LocalPosition) <= PortHitRadius)
				{
					NewHoveredPortNode = Node.Id;
					NewHoveredPortItem = Port.ItemClassPath;
					NewHoveredNode = Node.Id;
					return;
				}
			}
		};
		CheckPorts(InputPortsByNode.Find(Node.Id));
		CheckPorts(OutputPortsByNode.Find(Node.Id));
	}
	if (NewHoveredNode != HoveredNodeId || NewHoveredPortNode != HoveredPortNodeId
		|| NewHoveredPortItem != HoveredPortItemClassPath)
	{
		HoveredNodeId = NewHoveredNode;
		HoveredPortNodeId = NewHoveredPortNode;
		HoveredPortItemClassPath = MoveTemp(NewHoveredPortItem);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

FReply SSFPGraphPanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const float OldZoom = Zoom;
	Zoom = FMath::Clamp(
		Zoom + MouseEvent.GetWheelDelta() * MouseWheelZoomStep,
		MinimumGraphZoom,
		MaximumGraphZoom);
	const FVector2D Cursor = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	Pan = Cursor - (Cursor - Pan) * (Zoom / FMath::Max(OldZoom, MinimumGraphZoom));
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SSFPGraphPanel::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Plan.IsValid())
	{
		const FVector2D LocalPosition = InMyGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			const FVector2D* GraphPosition = GraphPositions.Find(Node.Id);
			if (GraphPosition != nullptr
				&& ContainsPoint(
					CompletionBoxForNode(TransformGraphPoint(*GraphPosition), NodeSize * Zoom, Zoom),
					LocalPosition))
			{
				return FReply::Handled();
			}
		}
	}
	FitGraph();
	return FReply::Handled();
}
