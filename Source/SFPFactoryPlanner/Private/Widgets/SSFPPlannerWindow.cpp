#include "Widgets/SSFPPlannerWindow.h"

#include "FGPlayerController.h"
#include "Resources/FGItemDescriptor.h"
#include "HAL/PlatformTime.h"
#include "SFPLocalization.h"
#include "SFPNumberFormatting.h"
#include "SFPPlannerHotkey.h"
#include "SFPPlannerPersistence.h"
#include "SFPPlannerRemoteCallObject.h"
#include "SFPPlannerSolver.h"
#include "Widgets/SSFPGraphPanel.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace SFPPlannerWindowPrivate
{
	namespace SFPTheme
	{
		const FLinearColor Background(0.018f, 0.026f, 0.032f, 0.99f);
		const FLinearColor Header(0.035f, 0.047f, 0.055f, 1.0f);
		const FLinearColor Panel(0.055f, 0.067f, 0.074f, 1.0f);
		const FLinearColor PanelRaised(0.095f, 0.108f, 0.116f, 1.0f);
		const FLinearColor Orange(0.96f, 0.43f, 0.055f, 1.0f);
		const FLinearColor Yellow(1.0f, 0.72f, 0.12f, 1.0f);
		const FLinearColor Cyan(0.18f, 0.68f, 0.70f, 1.0f);
		const FLinearColor Text(0.88f, 0.91f, 0.92f, 1.0f);
		const FLinearColor MutedText(0.62f, 0.68f, 0.70f, 1.0f);
	}

	constexpr int32 PlanningTabIndex = 0;
	constexpr int32 PowerTabIndex = 1;
	constexpr int32 MachinesTabIndex = 2;
	constexpr int32 ResourcesTabIndex = 3;
	constexpr int32 GraphTabIndex = 4;

	FString CurrentPlannerItemName(const FString& StoredName, const FString& ItemClassPath)
	{
		FString DisplayName = StoredName;
		if (!ItemClassPath.IsEmpty() && !ItemClassPath.StartsWith(TEXT("SFP.")))
		{
			UClass* ItemClass = FSoftClassPath(ItemClassPath).ResolveClass();
			if (IsValid(ItemClass) && ItemClass->IsChildOf(UFGItemDescriptor::StaticClass()))
			{
				const FString RuntimeName = UFGItemDescriptor::GetItemName(
					TSubclassOf<UFGItemDescriptor>(ItemClass)).ToString();
				if (!RuntimeName.IsEmpty())
				{
					DisplayName = RuntimeName;
				}
			}
		}
		return SFPLocalization::Translate(DisplayName);
	}

	FString BuildNodeCompletionKey(const FSFPPlanNode& Node)
	{
		return FString::Printf(
			TEXT("%d|%s|%s|%s|%s|%.9g|%.9g|%d"),
			static_cast<int32>(Node.Type),
			*Node.ClassPath,
			*Node.ProducedItemClassPath,
			*Node.RecipeClassPath,
			*Node.Title,
			Node.RatePerMinute,
			Node.MachineCount,
			Node.InfrastructureCount);
	}

	void CarryForwardNodeCompletion(const FSFPPlanResult* PreviousPlan, FSFPPlanResult& NewPlan)
	{
		for (FSFPPlanNode& Node : NewPlan.Nodes) Node.bCompleted = false;
		if (PreviousPlan == nullptr || PreviousPlan->bPowerProductionPlan != NewPlan.bPowerProductionPlan)
		{
			return;
		}
		if (NewPlan.bPowerProductionPlan)
		{
			if (PreviousPlan->RequestedNetPowerMW != NewPlan.RequestedNetPowerMW
				|| PreviousPlan->PowerReservePercent != NewPlan.PowerReservePercent
				|| PreviousPlan->RequestedGeneratorClassPath != NewPlan.RequestedGeneratorClassPath
				|| PreviousPlan->RequestedFuelClassPath != NewPlan.RequestedFuelClassPath
				|| PreviousPlan->ConfiguredGeneratorClockPercent != NewPlan.ConfiguredGeneratorClockPercent
				|| PreviousPlan->PassiveAlienPowerAugmenters != NewPlan.PassiveAlienPowerAugmenters
				|| PreviousPlan->FueledAlienPowerAugmenters != NewPlan.FueledAlienPowerAugmenters) return;
		}
		else
		{
			if (PreviousPlan->Targets.Num() != NewPlan.Targets.Num()) return;
			if (NewPlan.Targets.IsEmpty()
				&& (PreviousPlan->TargetName != NewPlan.TargetName
					|| PreviousPlan->TargetRatePerMinute != NewPlan.TargetRatePerMinute)) return;
			for (const FSFPPlanTarget& Target : NewPlan.Targets)
			{
				if (!PreviousPlan->Targets.ContainsByPredicate([&](const FSFPPlanTarget& OldTarget)
				{
					return OldTarget.ItemClassPath == Target.ItemClassPath
						&& OldTarget.RatePerMinute == Target.RatePerMinute;
				})) return;
			}
		}
		TMap<FString, TArray<bool>> CompletionByKey;
		for (const FSFPPlanNode& Node : PreviousPlan->Nodes)
		{
			CompletionByKey.FindOrAdd(BuildNodeCompletionKey(Node)).Add(Node.bCompleted);
		}
		TMap<FString, int32> NextIndexByKey;
		for (FSFPPlanNode& Node : NewPlan.Nodes)
		{
			const FString Key = BuildNodeCompletionKey(Node);
			int32& NextIndex = NextIndexByKey.FindOrAdd(Key);
			if (const TArray<bool>* States = CompletionByKey.Find(Key))
			{
				if (States->IsValidIndex(NextIndex))
				{
					Node.bCompleted = (*States)[NextIndex];
				}
			}
			++NextIndex;
		}
	}

	FString BuildTargetSummary(const FSFPPlanResult& Plan)
	{
		if (Plan.bPowerProductionPlan)
		{
			return FString::Printf(
				TEXT("STROMVERSORGUNG\nZiel: %s MW netto | Reserve: %s%%"),
				*FSFPNumberFormatting::Decimal(Plan.RequestedNetPowerMW, 2),
				*FSFPNumberFormatting::Decimal(Plan.PowerReservePercent, 1));
		}
		if (Plan.Targets.IsEmpty())
		{
			return FString::Printf(
				TEXT("%s: %s/min"),
				*Plan.TargetName,
				*FSFPNumberFormatting::Decimal(Plan.TargetRatePerMinute));
		}
		if (Plan.Targets.Num() == 1)
		{
			return FString::Printf(
				TEXT("%s: %s/min"),
				*Plan.Targets[0].DisplayName,
				*FSFPNumberFormatting::Decimal(Plan.Targets[0].RatePerMinute));
		}

		FString Summary = FString::Printf(TEXT("%d ENDPRODUKTE"), Plan.Targets.Num());
		for (const FSFPPlanTarget& Target : Plan.Targets)
		{
			Summary += FString::Printf(
				TEXT("\n• %s: %s/min"),
				*Target.DisplayName,
				*FSFPNumberFormatting::Decimal(Target.RatePerMinute));
		}
		return Summary;
	}

	FString BuildClockingBreakdown(const double EquivalentMachines)
	{
		const int32 BuiltMachines = FMath::Max(1, FMath::CeilToInt(EquivalentMachines));
		const int32 FullyClocked = FMath::FloorToInt(EquivalentMachines + KINDA_SMALL_NUMBER);
		const double PartialPercent = FMath::Max(
			0.0,
			(EquivalentMachines - static_cast<double>(FullyClocked)) * 100.0);
		if (PartialPercent <= 0.05)
		{
			return FString::Printf(TEXT("%d × 100%%"), BuiltMachines);
		}
		if (FullyClocked <= 0)
		{
			return FString::Printf(
				TEXT("1 × %s%%"),
				*FSFPNumberFormatting::Decimal(PartialPercent, 1));
		}
		return FString::Printf(
			TEXT("%d × 100%% + 1 × %s%%"),
			FullyClocked,
			*FSFPNumberFormatting::Decimal(PartialPercent, 1));
	}


	FString BuildPowerGeneratorClockingBreakdown(const FSFPPlanResult& Plan)
	{
		const double FullClockPercent = Plan.ConfiguredGeneratorClockPercent > KINDA_SMALL_NUMBER
			? Plan.ConfiguredGeneratorClockPercent : 100.0;
		if (Plan.PartialGeneratorClockPercent > 0.05)
		{
			if (Plan.FullClockGeneratorCount <= 0)
			{
				return FString::Printf(TEXT("1 × %s%%"),
					*FSFPNumberFormatting::Decimal(Plan.PartialGeneratorClockPercent, 1));
			}
			return FString::Printf(
				TEXT("%d × %s%% + 1 × %s%%"),
				Plan.FullClockGeneratorCount,
				*FSFPNumberFormatting::Decimal(FullClockPercent, 1),
				*FSFPNumberFormatting::Decimal(Plan.PartialGeneratorClockPercent, 1));
		}
		return FString::Printf(
			TEXT("%d × %s%%"),
			Plan.BuiltGeneratorCount,
			*FSFPNumberFormatting::Decimal(FullClockPercent, 1));
	}


	int32 BuiltMachineCountForNode(const FSFPPlanNode& Node)
	{
		return Node.BuiltMachineCount > 0
			? Node.BuiltMachineCount
			: FMath::Max(1, FMath::CeilToInt(Node.MachineCount));
	}

	FString BuildClockingBreakdown(const FSFPPlanNode& Node)
	{
		if (Node.BuiltMachineCount <= 0)
		{
			return BuildClockingBreakdown(Node.MachineCount);
		}
		const double FullClockPercent = Node.ConfiguredClockPercent > KINDA_SMALL_NUMBER
			? Node.ConfiguredClockPercent
			: 100.0;
		if (Node.PartialClockPercent > 0.05)
		{
			if (Node.FullClockMachineCount <= 0)
			{
				return FString::Printf(TEXT("1 × %s%%"), *FSFPNumberFormatting::Decimal(Node.PartialClockPercent, 1));
			}
			return FString::Printf(
				TEXT("%d × %s%% + 1 × %s%%"),
				Node.FullClockMachineCount,
				*FSFPNumberFormatting::Decimal(FullClockPercent, 1),
				*FSFPNumberFormatting::Decimal(Node.PartialClockPercent, 1));
		}
		return FString::Printf(
			TEXT("%d × %s%%"),
			Node.BuiltMachineCount,
			*FSFPNumberFormatting::Decimal(FullClockPercent, 1));
	}

	FString BuildPlanStatusText(
		const FSFPPlanResult& Plan,
		const TOptional<double> SolveMilliseconds = TOptional<double>(),
		const FString& Prefix = FString())
	{
		FString Status = Plan.bInputPlanning ? Plan.ErrorMessage + TEXT("\n") + Prefix : Prefix;
		if (!Status.IsEmpty())
		{
			Status += TEXT("\n");
		}
		Status += BuildTargetSummary(Plan);
		int32 BuiltMachineCount = 0;
		int32 CompletedNodeCount = 0;
		for (const FSFPPlanNode& Node : Plan.Nodes)
		{
			CompletedNodeCount += Node.bCompleted ? 1 : 0;
			if ((Node.Type == ESFPPlanNodeType::Machine || Node.Type == ESFPPlanNodeType::Generator)
				&& Node.MachineCount > 0.0)
			{
				BuiltMachineCount += BuiltMachineCountForNode(Node);
			}
		}
		if (Plan.bPowerProductionPlan)
		{
			Status += FString::Printf(
				TEXT("\n%d Knoten | %d Verbindungen\nBaufortschritt: %d/%d Knoten erledigt\n%d Maschinen und Generatoren tatsächlich bauen\n%s MW brutto − %s MW Eigenverbrauch = %s MW netto\n%s MW Reserve\n%d Routing-Bauwerke"),
				Plan.Nodes.Num(),
				Plan.Edges.Num(),
				CompletedNodeCount,
				Plan.Nodes.Num(),
				BuiltMachineCount,
				*FSFPNumberFormatting::Decimal(Plan.GrossPowerMW, 2),
				*FSFPNumberFormatting::Decimal(Plan.SelfConsumptionPowerMW, 2),
				*FSFPNumberFormatting::Decimal(Plan.NetPowerMW, 2),
				*FSFPNumberFormatting::Decimal(Plan.ReservePowerMW, 2),
				Plan.SplitterCount + Plan.MergerCount);
		}
		else
		{
			Status += FString::Printf(
				TEXT("\n%d Knoten | %d Verbindungen\nBaufortschritt: %d/%d Knoten erledigt\n%d Maschinen tatsächlich bauen | Strombedarf bei geplanter Taktung: %s MW\n%d Routing-Bauwerke"),
				Plan.Nodes.Num(),
				Plan.Edges.Num(),
				CompletedNodeCount,
				Plan.Nodes.Num(),
				BuiltMachineCount,
				*FSFPNumberFormatting::Decimal(Plan.TotalBasePowerMW, 2),
				Plan.SplitterCount + Plan.MergerCount);
		}
		if (SolveMilliseconds.IsSet())
		{
			Status += FString::Printf(
				TEXT("\nBerechnung: %s ms"),
				*FSFPNumberFormatting::Decimal(SolveMilliseconds.GetValue(), 2));
		}

		if (!Plan.Warnings.IsEmpty())
		{
			Status += FString::Printf(TEXT("\n\nHinweise (%d):"), Plan.Warnings.Num());
			const int32 WarningLimit = FMath::Min(Plan.Warnings.Num(), 8);
			for (int32 Index = 0; Index < WarningLimit; ++Index)
			{
				Status += FString::Printf(TEXT("\n• %s"), *Plan.Warnings[Index]);
			}
			if (Plan.Warnings.Num() > WarningLimit)
			{
				Status += FString::Printf(TEXT("\n• … und %d weitere"), Plan.Warnings.Num() - WarningLimit);
			}
		}
		return Status;
	}
}

using namespace SFPPlannerWindowPrivate;

SSFPPlannerWindow::~SSFPPlannerWindow()
{
	if (bCapturingPlannerHotkey)
	{
		SFPPlannerHotkey::SetCaptureInProgress(false);
	}
}

void SSFPPlannerWindow::Construct(const FArguments& InArgs)
{
	PlayerController = InArgs._PlayerController;
	OnClose = InArgs._OnClose;
	Solver = InArgs._Solver;

	if (!PlayerController.IsValid() || !Solver.IsValid())
	{
		StatusText = FString::Printf(TEXT("Initialisierung fehlgeschlagen: %s"), *InArgs._InitializeError);
	}
	else
	{
		StatusText = FString::Printf(
			TEXT("%d Produkte, %d Stromgeneratoren und %d Transportstufen geladen"),
			Solver->GetProducts().Num(),
			Solver->GetPowerGenerators().Num(),
			Solver->GetTransportTiers().Num());
	}

	RefreshProducts();
	RefreshTransportChoices();
	RefreshPowerChoices();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(SFPTheme::Background)
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(SFPTheme::Header)
				.Padding(FMargin(18.0f, 11.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(SFPLocalization::Text(TEXT("PIONEER PRODUCTION PLANNER")))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 24))
							.ColorAndOpacity(SFPTheme::Yellow)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(SFPLocalization::Text(TEXT("PRODUCTION CONTROL  /  RUNTIME DATA")))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
							.ColorAndOpacity(SFPTheme::Cyan)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(SFPLocalization::Text(TEXT("HOTKEY")))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
							.ColorAndOpacity(SFPTheme::MutedText)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.ContentPadding(FMargin(12.0f, 6.0f))
							.ButtonColorAndOpacity(SFPTheme::PanelRaised)
							.ToolTipText(SFPLocalization::Text(TEXT("Klicken und anschließend die neue Tastenkombination drücken")))
							.OnClicked(this, &SSFPPlannerWindow::HandleBeginHotkeyCapture)
							[
								SNew(STextBlock)
								.Text(this, &SSFPPlannerWindow::GetHotkeyButtonText)
								.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
								.ColorAndOpacity(SFPTheme::Text)
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(5.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.ContentPadding(FMargin(9.0f, 6.0f))
							.ButtonColorAndOpacity(SFPTheme::PanelRaised)
							.ToolTipText(SFPLocalization::Text(TEXT("Tastenkürzel deaktivieren; Terminal und Chatbefehl bleiben verfügbar")))
							.OnClicked(this, &SSFPPlannerWindow::HandleDisableHotkey)
							[
								SNew(STextBlock)
								.Text(SFPLocalization::Text(TEXT("AUS")))
								.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
								.ColorAndOpacity(SFPTheme::MutedText)
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.ContentPadding(FMargin(18.0f, 7.0f))
						.ButtonColorAndOpacity(SFPTheme::Orange)
						.OnClicked(this, &SSFPPlannerWindow::HandleClose)
						[
							SNew(STextBlock)
							.Text(SFPLocalization::Text(TEXT("SCHLIESSEN  [ESC]")))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
							.ColorAndOpacity(FLinearColor::White)
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(12.0f, 10.0f, 12.0f, 0.0f))
			[
				BuildPlanManagementBar()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(12.0f, 9.0f, 12.0f, 0.0f))
			[
				BuildTabBar()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(FMargin(12.0f, 8.0f, 12.0f, 12.0f))
			[
				SAssignNew(TabSwitcher, SWidgetSwitcher)
				.WidgetIndex(ActiveTabIndex)
				+ SWidgetSwitcher::Slot()
				[
					BuildPlanningTab()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildPowerTab()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildMachinesTab()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildResourcesTab()
				]
				+ SWidgetSwitcher::Slot()
				[
					BuildGraphTab()
				]
			]
		]
	];

	RefreshNamedPlans();

	RestoreLastPlan();
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildPlanManagementBar()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(SFPTheme::Panel)
		.Padding(FMargin(10.0f, 8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(SFPLocalization::Text(TEXT("PLAN")))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9))
				.ColorAndOpacity(SFPTheme::Orange)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton)
					.Text(this, &SSFPPlannerWindow::GetPlanScopeText)
					.ToolTipText(SFPLocalization::Text(TEXT("Zwischen persönlichen Client-Plänen und gemeinsamen Multiplayer-Plänen wechseln")))
					.OnClicked(this, &SSFPPlannerWindow::HandleTogglePlanScope)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.30f)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(PlanNameInput, SEditableTextBox)
				.HintText(SFPLocalization::Text(TEXT("z. B. Stahlwerk 120/min")))
				.OnTextChanged(this, &SSFPPlannerWindow::HandlePlanNameChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 14.0f, 0.0f)
			[
				SNew(SButton)
				.Text(SFPLocalization::Text(TEXT("SPEICHERN")))
				.ToolTipText(SFPLocalization::Text(TEXT("Aktuellen Produktionsplan unter dem eingegebenen Namen speichern")))
				.OnClicked(this, &SSFPPlannerWindow::HandleSavePlan)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.70f)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(SavedPlanCombo, SComboBox<TSharedPtr<FSFPSavedPlanInfo>>)
				.OptionsSource(&SavedPlans)
				.OnGenerateWidget(this, &SSFPPlannerWindow::HandleGenerateSavedPlanWidget)
				.OnSelectionChanged(this, &SSFPPlannerWindow::HandleSavedPlanSelected)
				[
					SNew(STextBlock)
					.Text(this, &SSFPPlannerWindow::GetSelectedSavedPlanText)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Text(SFPLocalization::Text(TEXT("LADEN")))
				.OnClicked(this, &SSFPPlannerWindow::HandleLoadSavedPlan)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(SFPLocalization::Text(TEXT("LÖSCHEN")))
				.OnClicked(this, &SSFPPlannerWindow::HandleDeleteSavedPlan)
			]
		];
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildTabButton(
	const int32 TabIndex,
	const FString& Label,
	const FString& Hint)
{
	return SNew(SBox)
		.MinDesiredHeight(40.0f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(18.0f, 8.0f))
			.ButtonColorAndOpacity_Lambda([this, TabIndex]() -> FSlateColor
			{
				return ActiveTabIndex == TabIndex
					? FSlateColor(SFPTheme::Orange)
					: FSlateColor(SFPTheme::PanelRaised);
			})
			.ToolTipText(SFPLocalization::Text(Hint))
			.OnClicked_Lambda([this, TabIndex]()
			{
				return HandleSelectTab(TabIndex);
			})
			[
				SNew(STextBlock)
				.Text(SFPLocalization::Text(Label))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
				.ColorAndOpacity_Lambda([this, TabIndex]() -> FSlateColor
				{
					return ActiveTabIndex == TabIndex
						? FSlateColor(FLinearColor::White)
						: FSlateColor(SFPTheme::Text);
				})
			]
		];
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildTabBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			BuildTabButton(PlanningTabIndex, TEXT("1  PLANUNG"), TEXT("Zielprodukt, Menge und alternative Rezepte"))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			BuildTabButton(PowerTabIndex, TEXT("2  STROMVERSORGUNG"), TEXT("Nettoleistung, Erzeuger, Betriebsart und Reserve"))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			BuildTabButton(MachinesTabIndex, TEXT("3  MASCHINEN"), TEXT("Benötigte Maschinen, Anzahl und Leistung"))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			BuildTabButton(ResourcesTabIndex, TEXT("4  RESSOURCEN & KOSTEN"), TEXT("Rohstofflimits, Transport und Baumaterial"))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			BuildTabButton(GraphTabIndex, TEXT("5  PRODUKTIONSGRAPH"), TEXT("Vollflächiger zoombarer Produktionsgraph"))
		];
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildPlanningTab()
{
	return SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot()
		.Value(0.30f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(SFPTheme::Panel)
			.Padding(12.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 9.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("ENDPRODUKTE DER PRODUKTIONSSTÄTTE")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
					.ColorAndOpacity(SFPTheme::Yellow)
				]
                + SVerticalBox::Slot().AutoHeight()
                [ BuildInputPlanningControls() ]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 7.0f)
				[
					SNew(SSearchBox)
					.HintText(SFPLocalization::Text(TEXT("Produkt oder Mod suchen ...")))
					.OnTextChanged(this, &SSFPPlannerWindow::HandleSearchChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 7.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SSFPPlannerWindow::GetOnlyAvailableState)
					.OnCheckStateChanged(this, &SSFPPlannerWindow::HandleOnlyAvailableChanged)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("Nur freigeschaltete Rezepte und Transportstufen")))
						.ColorAndOpacity(SFPTheme::Text)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
                    SNew(SBox).HeightOverride(130.0f)
                    .Visibility_Lambda([this]() { return bShowProductSearch ? EVisibility::Visible : EVisibility::Collapsed; })
                    [
					SAssignNew(ProductList, SListView<TSharedPtr<FSFPProductOption>>)
					.ListItemsSource(&FilteredProducts)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SSFPPlannerWindow::HandleGenerateProductRow)
					.OnSelectionChanged(this, &SSFPPlannerWindow::HandleProductSelected)
				]
                ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SButton)
						.Text_Lambda([this]() { return SelectedProduct.IsValid() ? FText::FromString(SelectedProduct->DisplayName) : SFPLocalization::Text(TEXT("Produkt auswählen")); })
						.OnClicked_Lambda([this]() { bShowProductSearch = !bShowProductSearch; return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(SFPLocalization::Text(TEXT("AUSWAHL LEEREN")))
						.ToolTipText(SFPLocalization::Text(TEXT("Produktmarkierung aufheben, ohne ein Endprodukt hinzuzufügen")))
						.IsEnabled_Lambda([this]() { return SelectedProduct.IsValid(); })
						.OnClicked_Lambda([this]()
						{
							SelectedProduct.Reset();
							if (ProductList.IsValid())
							{
								ProductList->ClearSelection();
							}
							StatusText = TEXT("Produktmarkierung aufgehoben");
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 9.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Menge des markierten Produkts pro Minute")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(0.0f, 0.0f, 7.0f, 0.0f)
					[
						SNew(SNumericEntryBox<double>)
						.AllowSpin(true)
						.MinValue(0.001)
						.MaxValue(1000000.0)
						.MinSliderValue(1.0)
						.MaxSliderValue(1200.0)
						.Value(this, &SSFPPlannerWindow::GetTargetRate)
						.OnValueChanged(this, &SSFPPlannerWindow::HandleTargetRateChanged)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonColorAndOpacity(SFPTheme::Cyan)
						.Text(SFPLocalization::Text(TEXT("+ ZIEL HINZUFÜGEN")))
						.ToolTipText(SFPLocalization::Text(TEXT("Markiertes Produkt mit dieser Rate zur gemeinsamen Produktionsstätte hinzufügen")))
						.OnClicked(this, &SSFPPlannerWindow::HandleAddTarget)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 9.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("GEWÄHLTE ENDPRODUKTE")))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
						.ColorAndOpacity(SFPTheme::Orange)
				]
				+ SVerticalBox::Slot()
				.FillHeight(0.44f)
				[
					SAssignNew(TargetList, SListView<TSharedPtr<FSFPSelectedTarget>>)
						.ListItemsSource(&SelectedTargets)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SSFPPlannerWindow::HandleGenerateTargetRow)
				]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SExpandableArea).InitiallyCollapsed(true)
                  .HeaderContent()[SNew(STextBlock).Text(SFPLocalization::Text(TEXT("Weitere Einstellungen")))]
                  .BodyContent()[SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("TRANSPORTSTUFEN")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
					.ColorAndOpacity(SFPTheme::Orange)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("FÖRDERBAND")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SAssignNew(ConveyorCombo, SComboBox<TSharedPtr<FSFPTransportChoice>>)
					.OptionsSource(&ConveyorChoices)
					.InitiallySelectedItem(SelectedConveyor)
					.OnGenerateWidget(this, &SSFPPlannerWindow::HandleGenerateTransportWidget)
					.OnSelectionChanged(this, &SSFPPlannerWindow::HandleConveyorSelected)
					[
						SNew(STextBlock)
						.Text(this, &SSFPPlannerWindow::GetSelectedConveyorText)
						.ColorAndOpacity(SFPTheme::Text)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("FÖRDERLIFT")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 7.0f)
				[
					SAssignNew(ConveyorLiftCombo, SComboBox<TSharedPtr<FSFPTransportChoice>>)
					.OptionsSource(&ConveyorLiftChoices)
					.InitiallySelectedItem(SelectedConveyorLift)
					.OnGenerateWidget(this, &SSFPPlannerWindow::HandleGenerateTransportWidget)
					.OnSelectionChanged(this, &SSFPPlannerWindow::HandleConveyorLiftSelected)
					[
						SNew(STextBlock)
						.Text(this, &SSFPPlannerWindow::GetSelectedConveyorLiftText)
						.ColorAndOpacity(SFPTheme::Text)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 7.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Mittlere Verbindungslänge für die Kostenschätzung (m)")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SNumericEntryBox<double>)
					.AllowSpin(true)
					.MinValue(0.5)
					.MaxValue(1000.0)
					.MinSliderValue(1.0)
					.MaxSliderValue(100.0)
					.Value(this, &SSFPPlannerWindow::GetEstimatedConnectionLength)
					.OnValueChanged(this, &SSFPPlannerWindow::HandleEstimatedConnectionLengthChanged)
				]
                  ]
                ]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(10.0f, 7.0f))
					.ButtonColorAndOpacity(SFPTheme::Orange)
					.OnClicked(this, &SSFPPlannerWindow::HandleCalculate)
					[
						SNew(STextBlock)
							.Text_Lambda([this]()
							{
								const bool bEditingPowerPlan = CurrentPlan.IsValid()
									&& CurrentPlan->bPowerProductionPlan
									&& SelectedTargets.IsEmpty();
								return SFPLocalization::Text(bEditingPowerPlan
									? TEXT("STROM- & BRENNSTOFFPRODUKTION NEU BERECHNEN")
									: TEXT("MEHRPRODUKT-PLAN BERECHNEN"));
							})
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
					]
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.40f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(SFPTheme::Panel)
			.Padding(12.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 7.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("REZEPTE & ROHSTOFFGEWINNUNG")))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
						.ColorAndOpacity(SFPTheme::Yellow)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(SFPLocalization::Text(TEXT("AUTOMATIK")))
						.OnClicked(this, &SSFPPlannerWindow::HandleResetRecipeChoices)
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(0.62f)
				[
					SAssignNew(RecipeChoiceList, SListView<TSharedPtr<FSFPRecipeChoiceRow>>)
					.ListItemsSource(&RecipeChoiceRows)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SSFPPlannerWindow::HandleGenerateRecipeChoiceRow)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("PLANSTATUS")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
					.ColorAndOpacity(SFPTheme::Orange)
				]
				+ SVerticalBox::Slot()
				.FillHeight(0.38f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor(SFPTheme::Header)
					.Padding(9.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(STextBlock)
							.Text(this, &SSFPPlannerWindow::GetStatusText)
							.AutoWrapText(true)
							.ColorAndOpacity(SFPTheme::Text)
						]
					]
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.30f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(SFPTheme::Panel)
			.Padding(12.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("MASCHINENEINSTELLUNGEN")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
					.ColorAndOpacity(SFPTheme::Yellow)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::MutedText)
					.Text(SFPLocalization::Text(TEXT("Maschinenvariante, Takt, Somersloops und Brennstoff werden pro Produktionsstufe eingestellt.")))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::Orange)
					.Visibility_Lambda([this]() { return MachineGuidanceState == 0 ? EVisibility::Collapsed : EVisibility::Visible; })
					.Text_Lambda([this]() { return SFPLocalization::Text(MachineGuidanceState == 1
						? TEXT("Maschineneinstellung geändert – bitte neu berechnen.")
						: MachineGuidanceState == 2 ? TEXT("Plan aktualisiert – zum Behalten speichern.")
						: TEXT("Plan gespeichert.")); })
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor(SFPTheme::Header)
					.Padding(6.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(MachineSettingsBox, SVerticalBox)
						]
					]
				]
			]
		]
		;
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildPowerTab()
{
	return SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot()
		.Value(0.42f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(SFPTheme::Panel)
			.Padding(16.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("STROMVERSORGUNG PLANEN")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16))
					.ColorAndOpacity(SFPTheme::Yellow)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Generatoren, modulare Komponenten und Brennstoffe kommen direkt aus dem aktiven Spielstand. Die vollständige Erzeugungskette wird in den Produktionsgraphen aufgenommen.")))
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SSFPPlannerWindow::GetUseFactoryPowerDemandState)
					.IsEnabled_Lambda([this]() { return LastFactoryPowerDemandMW > KINDA_SMALL_NUMBER; })
					.OnCheckStateChanged(this, &SSFPPlannerWindow::HandleUseFactoryPowerDemandChanged)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("STROMBEDARF DES AKTUELLEN FABRIKPLANS ALS ZIEL VERWENDEN")))
						.ColorAndOpacity(SFPTheme::Text)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SNew(STextBlock)
					.Text(this, &SSFPPlannerWindow::GetFactoryPowerDemandText)
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("GEWÜNSCHTE NETTOLEISTUNG (MW)")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
					.ColorAndOpacity(SFPTheme::Orange)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SNumericEntryBox<double>)
					.AllowSpin(true)
					.MinValue(0.1)
					.MaxValue(1000000000.0)
					.MinSliderValue(1.0)
					.MaxSliderValue(10000.0)
					.IsEnabled_Lambda([this]() { return !bUseCurrentFactoryPowerDemand; })
					.Value(this, &SSFPPlannerWindow::GetPowerTargetNetMW)
					.OnValueChanged(this, &SSFPPlannerWindow::HandlePowerTargetNetMWChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("SICHERHEITSRESERVE (%)")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
					.ColorAndOpacity(SFPTheme::Orange)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SNumericEntryBox<double>)
					.AllowSpin(true)
					.MinValue(0.0)
					.MaxValue(500.0)
					.MinSliderValue(0.0)
					.MaxSliderValue(50.0)
					.Value(this, &SSFPPlannerWindow::GetPowerReservePercent)
					.OnValueChanged(this, &SSFPPlannerWindow::HandlePowerReservePercentChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("ALIEN POWER AUGMENTER")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
					.ColorAndOpacity(SFPTheme::Orange)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Augmenter werden als Teil desselben Stromnetzes berechnet. Mit Matrix versorgte Augmenter planen zusätzlich den Matrix-Bedarf und dessen Produktionskette.")))
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.56f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("Passiv (+Basisleistung / Netzboost)")))
						.ColorAndOpacity(SFPTheme::Text)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.44f)
					[
						SNew(SNumericEntryBox<int32>)
						.AllowSpin(true)
						.MinValue(0)
						.MaxValue(10000)
						.MinSliderValue(0)
						.MaxSliderValue(20)
						.Value(this, &SSFPPlannerWindow::GetPassiveAlienPowerAugmenters)
						.OnValueChanged(this, &SSFPPlannerWindow::HandlePassiveAlienPowerAugmentersChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(0.56f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("Mit Alien Power Matrix versorgt")))
						.ColorAndOpacity(SFPTheme::Text)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.44f)
					[
						SNew(SNumericEntryBox<int32>)
						.AllowSpin(true)
						.MinValue(0)
						.MaxValue(10000)
						.MinSliderValue(0)
						.MaxSliderValue(20)
						.Value(this, &SSFPPlannerWindow::GetFueledAlienPowerAugmenters)
						.OnValueChanged(this, &SSFPPlannerWindow::HandleFueledAlienPowerAugmentersChanged)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SSFPPlannerWindow::GetOnlyAvailableState)
					.OnCheckStateChanged(this, &SSFPPlannerWindow::HandleOnlyAvailableChanged)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("Nur freigeschaltete Generatoren, Komponenten, Brennstoffe und Rezepte")))
						.ColorAndOpacity(SFPTheme::Text)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("GENERATOR")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SAssignNew(PowerGeneratorCombo, SComboBox<TSharedPtr<FSFPPowerGeneratorOption>>)
					.OptionsSource(&PowerGeneratorChoices)
					.InitiallySelectedItem(SelectedPowerGenerator)
					.OnGenerateWidget(this, &SSFPPlannerWindow::HandleGeneratePowerGeneratorWidget)
					.OnSelectionChanged(this, &SSFPPlannerWindow::HandlePowerGeneratorSelected)
					[
						SNew(STextBlock)
						.Text(this, &SSFPPlannerWindow::GetSelectedPowerGeneratorText)
						.ColorAndOpacity(SFPTheme::Text)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("GENERATOR-TAKT (%)")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SNumericEntryBox<double>)
					.AllowSpin(true)
					.MinValue(this, &SSFPPlannerWindow::GetPowerGeneratorMinClockPercent)
					.MaxValue(this, &SSFPPlannerWindow::GetPowerGeneratorMaxClockPercent)
					.MinSliderValue(this, &SSFPPlannerWindow::GetPowerGeneratorMinClockPercent)
					.MaxSliderValue(this, &SSFPPlannerWindow::GetPowerGeneratorMaxClockPercent)
					.Value(this, &SSFPPlannerWindow::GetPowerGeneratorClockPercent)
					.OnValueChanged(this, &SSFPPlannerWindow::HandlePowerGeneratorClockPercentChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const double Clock = PowerGeneratorClockPercent;
						const double BaseMW = SelectedPowerGenerator.IsValid()
							? SelectedPowerGenerator->PowerProductionMW : 0.0;
						const FString Detail = BaseMW > KINDA_SMALL_NUMBER
							? FString::Printf(TEXT("%s MW je voll getaktetem Generator. Stromgeneratoren können übertaktet, aber nicht mit Somersloops verstärkt werden."),
								*FSFPNumberFormatting::Decimal(BaseMW * Clock / 100.0, 2))
							: TEXT("Stromgeneratoren können übertaktet, aber nicht mit Somersloops verstärkt werden.");
						return SFPLocalization::Text(Detail);
					})
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("BRENNSTOFF / BETRIEBSART")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SAssignNew(PowerFuelCombo, SComboBox<TSharedPtr<FSFPPowerFuelOption>>)
					.OptionsSource(&PowerFuelChoices)
					.InitiallySelectedItem(SelectedPowerFuel)
					.OnGenerateWidget(this, &SSFPPlannerWindow::HandleGeneratePowerFuelWidget)
					.OnSelectionChanged(this, &SSFPPlannerWindow::HandlePowerFuelSelected)
					[
						SNew(STextBlock)
						.Text(this, &SSFPPlannerWindow::GetSelectedPowerFuelText)
						.ColorAndOpacity(SFPTheme::Text)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("MITTLERE VERBINDUNGSLÄNGE (M)")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 14.0f)
				[
					SNew(SNumericEntryBox<double>)
					.AllowSpin(true)
					.MinValue(0.5)
					.MaxValue(1000.0)
					.MinSliderValue(1.0)
					.MaxSliderValue(100.0)
					.Value(this, &SSFPPlannerWindow::GetEstimatedConnectionLength)
					.OnValueChanged(this, &SSFPPlannerWindow::HandleEstimatedConnectionLengthChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(10.0f, 8.0f))
					.ButtonColorAndOpacity(SFPTheme::Orange)
					.OnClicked(this, &SSFPPlannerWindow::HandleCalculatePower)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("STROM- & BRENNSTOFFPRODUKTION BERECHNEN")))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(10.0f, 8.0f))
					.ButtonColorAndOpacity(SFPTheme::Cyan)
					.OnClicked(this, &SSFPPlannerWindow::HandleCalculateMaximumPower)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("MAXIMAL MÖGLICHE NETTOLEISTUNG BERECHNEN")))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SSFPPlannerWindow::GetMaximumPowerEstimateText)
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::Cyan)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Zuerst einmal normal berechnen und im Reiter Planung Miner, Bohrköpfe, Module, Betriebsflüssigkeiten und verfügbare Reinheiten bestätigen.")))
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::MutedText)
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.58f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(SFPTheme::Panel)
			.Padding(16.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("NETZBILANZ & ERZEUGUNGSKETTE")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16))
					.ColorAndOpacity(SFPTheme::Yellow)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor(SFPTheme::Header)
					.Padding(14.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(STextBlock)
							.Text(this, &SSFPPlannerWindow::GetPowerSummaryText)
							.AutoWrapText(true)
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12))
							.ColorAndOpacity(SFPTheme::Text)
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildMachinesTab()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(SFPTheme::Panel)
		.Padding(16.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(SFPLocalization::Text(TEXT("MASCHINENÜBERSICHT")))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16))
				.ColorAndOpacity(SFPTheme::Yellow)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(SFPTheme::Header)
				.Padding(14.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(STextBlock)
						.Text(this, &SSFPPlannerWindow::GetMachineSummaryText)
						.AutoWrapText(true)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12))
						.ColorAndOpacity(SFPTheme::Text)
					]
				]
			]
		];
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildResourcesTab()
{
	return SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot()
		.Value(0.42f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(SFPTheme::Panel)
			.Padding(14.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("VERFÜGBARE ROHSTOFFE PRO MINUTE")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
					.ColorAndOpacity(SFPTheme::Yellow)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 9.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Werte anpassen; der knappste Eingang skaliert alle Endproduktraten proportional.")))
					.AutoWrapText(true)
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(InputBudgetList, SListView<TSharedPtr<FSFPInputBudgetOption>>)
					.ListItemsSource(&InputBudgets)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SSFPPlannerWindow::HandleGenerateInputBudgetRow)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(10.0f, 7.0f))
					.ButtonColorAndOpacity(SFPTheme::Orange)
					.ToolTipText(SFPLocalization::Text(TEXT("Aus allen eingetragenen Rohstofflimits neu berechnen")))
					.OnClicked(this, &SSFPPlannerWindow::HandleCalculateFromInputs)
					[
						SNew(STextBlock)
						.Text(SFPLocalization::Text(TEXT("AUS ROHSTOFFLIMITS BERECHNEN")))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
					]
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.58f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(SFPTheme::Panel)
			.Padding(14.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("BEDARF, TRANSPORT UND BAUKOSTEN")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
					.ColorAndOpacity(SFPTheme::Yellow)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor(SFPTheme::Header)
					.Padding(12.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(STextBlock)
							.Text(this, &SSFPPlannerWindow::GetResourceSummaryText)
							.AutoWrapText(true)
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11))
							.ColorAndOpacity(SFPTheme::Text)
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SSFPPlannerWindow::BuildGraphTab()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(SFPTheme::Panel)
		.Padding(6.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("PRODUKTIONSGRAPH")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))
					.ColorAndOpacity(SFPTheme::Yellow)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Kästchen: erledigt/offen   •   Mausrad: Zoom   •   Linke Maustaste: Knoten ziehen   •   Mitte/Rechts: Ansicht verschieben")))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SSFPPlannerWindow::GetGraphProgressText)
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
					.ColorAndOpacity(SFPTheme::Cyan)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(SFPLocalization::Text(TEXT("AUTOMATISCH ANORDNEN")))
					.OnClicked(this, &SSFPPlannerWindow::HandleAutoArrangeGraph)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(SFPLocalization::Text(TEXT("FIT GRAPH")))
					.OnClicked(this, &SSFPPlannerWindow::HandleResetGraph)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(SFPTheme::Background)
				.Padding(1.0f)
				[
					SAssignNew(GraphPanel, SSFPGraphPanel)
					.OnNodeCompletionChanged(this, &SSFPPlannerWindow::HandleNodeCompletionChanged)
				]
			]
		];
}

void SSFPPlannerWindow::RefreshProducts()
{
	FilteredProducts.Reset();
	if (!Solver.IsValid())
	{
		return;
	}

	for (const TSharedPtr<FSFPProductOption>& Product : Solver->GetProducts())
	{
		if (!Product.IsValid() || (bOnlyAvailable && !Product->bHasAvailableRecipe))
		{
			continue;
		}
		if (!SearchText.IsEmpty()
			&& !Product->DisplayName.Contains(SearchText, ESearchCase::IgnoreCase)
			&& !Product->SourceMount.Contains(SearchText, ESearchCase::IgnoreCase)
			&& !Product->ClassPath.Contains(SearchText, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FilteredProducts.Add(Product);
	}

	if (ProductList.IsValid())
	{
		ProductList->RequestListRefresh();
		if (!SelectedProduct.IsValid() || !FilteredProducts.Contains(SelectedProduct))
		{
			SelectedProduct.Reset();
			ProductList->ClearSelection();
		}
	}
}

void SSFPPlannerWindow::RefreshTransportChoices(
	const FString& PreferredConveyorPath,
	const FString& PreferredLiftPath)
{
	const FString ConveyorPath = !PreferredConveyorPath.IsEmpty()
		? PreferredConveyorPath
		: (SelectedConveyor.IsValid() ? SelectedConveyor->Tier.ClassPath : FString());
	const FString LiftPath = !PreferredLiftPath.IsEmpty()
		? PreferredLiftPath
		: (SelectedConveyorLift.IsValid() ? SelectedConveyorLift->Tier.ClassPath : FString());

	ConveyorChoices.Reset();
	ConveyorLiftChoices.Reset();
	SelectedConveyor.Reset();
	SelectedConveyorLift.Reset();
	if (!Solver.IsValid())
	{
		return;
	}

	for (const FSFPTransportTier& Tier : Solver->GetTransportTiers())
	{
		if ((Tier.Kind != TEXT("belt") && Tier.Kind != TEXT("lift"))
			|| (bOnlyAvailable && !Tier.bAvailable))
		{
			continue;
		}
		TSharedPtr<FSFPTransportChoice> Choice = MakeShared<FSFPTransportChoice>();
		Choice->Tier = Tier;
		TArray<TSharedPtr<FSFPTransportChoice>>& Choices = Tier.Kind == TEXT("belt")
			? ConveyorChoices
			: ConveyorLiftChoices;
		Choices.Add(Choice);
		if ((Tier.Kind == TEXT("belt") && Tier.ClassPath == ConveyorPath)
			|| (Tier.Kind == TEXT("lift") && Tier.ClassPath == LiftPath))
		{
			if (Tier.Kind == TEXT("belt"))
			{
				SelectedConveyor = Choice;
			}
			else
			{
				SelectedConveyorLift = Choice;
			}
		}
	}

	auto ChooseDefault = [](const TArray<TSharedPtr<FSFPTransportChoice>>& Choices)
	{
		TSharedPtr<FSFPTransportChoice> Fastest;
		if (!Choices.IsEmpty())
		{
			Fastest = Choices.Last();
		}
		for (const TSharedPtr<FSFPTransportChoice>& Choice : Choices)
		{
			if (Choice.IsValid() && Choice->Tier.bAvailable)
			{
				Fastest = Choice;
			}
		}
		return Fastest;
	};
	if (!SelectedConveyor.IsValid())
	{
		SelectedConveyor = ChooseDefault(ConveyorChoices);
	}
	if (!SelectedConveyorLift.IsValid())
	{
		SelectedConveyorLift = ChooseDefault(ConveyorLiftChoices);
	}

	if (ConveyorCombo.IsValid())
	{
		ConveyorCombo->RefreshOptions();
		ConveyorCombo->SetSelectedItem(SelectedConveyor);
	}
	if (ConveyorLiftCombo.IsValid())
	{
		ConveyorLiftCombo->RefreshOptions();
		ConveyorLiftCombo->SetSelectedItem(SelectedConveyorLift);
	}
}

void SSFPPlannerWindow::RefreshPowerChoices(
	const FString& PreferredGeneratorPath,
	const FString& PreferredFuelPath)
{
	PowerGeneratorChoices.Reset();
	PowerFuelChoices.Reset();
	SelectedPowerGenerator.Reset();
	SelectedPowerFuel.Reset();

	TSharedPtr<FSFPPowerGeneratorOption> AutomaticGenerator =
		MakeShared<FSFPPowerGeneratorOption>();
	AutomaticGenerator->DisplayName = TEXT("Automatik – beste verfügbare Kombination");
	AutomaticGenerator->bAvailable = true;
	PowerGeneratorChoices.Add(AutomaticGenerator);
	SelectedPowerGenerator = AutomaticGenerator;

	TSharedPtr<FSFPPowerFuelOption> AutomaticFuel = MakeShared<FSFPPowerFuelOption>();
	AutomaticFuel->DisplayName = TEXT("Automatik – passender Brennstoff");
	AutomaticFuel->bAvailable = true;
	PowerFuelChoices.Add(AutomaticFuel);
	SelectedPowerFuel = AutomaticFuel;

	if (Solver.IsValid())
	{
		for (const TSharedPtr<FSFPPowerGeneratorOption>& Generator : Solver->GetPowerGenerators())
		{
			if (!Generator.IsValid() || (bOnlyAvailable && !Generator->bAvailable))
			{
				continue;
			}
			PowerGeneratorChoices.Add(Generator);
			if (!PreferredGeneratorPath.IsEmpty() && Generator->ClassPath == PreferredGeneratorPath)
			{
				SelectedPowerGenerator = Generator;
			}
		}

		TMap<FString, TSharedPtr<FSFPPowerFuelOption>> FuelsByPath;
		for (const TSharedPtr<FSFPPowerGeneratorOption>& Generator : PowerGeneratorChoices)
		{
			if (!Generator.IsValid() || Generator->ClassPath.IsEmpty()
				|| (!SelectedPowerGenerator->ClassPath.IsEmpty()
					&& Generator->ClassPath != SelectedPowerGenerator->ClassPath))
			{
				continue;
			}
			for (const FSFPPowerFuelOption& Fuel : Generator->Fuels)
			{
				if (bOnlyAvailable && !Fuel.bAvailable)
				{
					continue;
				}
				TSharedPtr<FSFPPowerFuelOption>& Existing = FuelsByPath.FindOrAdd(Fuel.ClassPath);
				if (!Existing.IsValid())
				{
					Existing = MakeShared<FSFPPowerFuelOption>(Fuel);
				}
				else
				{
					Existing->bAvailable |= Fuel.bAvailable;
				}
			}
		}
		TArray<TSharedPtr<FSFPPowerFuelOption>> SortedFuels;
		FuelsByPath.GenerateValueArray(SortedFuels);
		SortedFuels.Sort([](
			const TSharedPtr<FSFPPowerFuelOption>& Left,
			const TSharedPtr<FSFPPowerFuelOption>& Right)
		{
			return Left.IsValid() && Right.IsValid()
				? Left->DisplayName.Compare(Right->DisplayName, ESearchCase::IgnoreCase) < 0
				: Left.IsValid();
		});
		for (const TSharedPtr<FSFPPowerFuelOption>& Fuel : SortedFuels)
		{
			PowerFuelChoices.Add(Fuel);
			if (!PreferredFuelPath.IsEmpty() && Fuel.IsValid()
				&& Fuel->ClassPath == PreferredFuelPath)
			{
				SelectedPowerFuel = Fuel;
			}
		}
	}

	if (PowerGeneratorCombo.IsValid())
	{
		PowerGeneratorCombo->RefreshOptions();
		PowerGeneratorCombo->SetSelectedItem(SelectedPowerGenerator);
	}
	if (PowerFuelCombo.IsValid())
	{
		PowerFuelCombo->RefreshOptions();
		PowerFuelCombo->SetSelectedItem(SelectedPowerFuel);
	}
}

void SSFPPlannerWindow::HandleSearchChanged(const FText& NewText)
{
	SearchText = NewText.ToString();
	bShowProductSearch = true;
	RefreshProducts();
}

void SSFPPlannerWindow::HandleOnlyAvailableChanged(const ECheckBoxState NewState)
{
	const FString PreviousGeneratorPath = SelectedPowerGenerator.IsValid()
		? SelectedPowerGenerator->ClassPath
		: FString();
	const FString PreviousFuelPath = SelectedPowerFuel.IsValid()
		? SelectedPowerFuel->ClassPath
		: FString();
	bOnlyAvailable = NewState == ECheckBoxState::Checked;
	RefreshProducts();
	RefreshTransportChoices();
	RefreshPowerChoices(PreviousGeneratorPath, PreviousFuelPath);
	StatusText = TEXT("Freischaltungsfilter geändert – Plan neu berechnen");
}

void SSFPPlannerWindow::HandleConveyorSelected(
	TSharedPtr<FSFPTransportChoice> Choice,
	const ESelectInfo::Type SelectInfo)
{
	SelectedConveyor = MoveTemp(Choice);
	if (SelectedConveyor.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		StatusText = TEXT("Förderband geändert – Produktionsplan neu berechnen");
	}
}

void SSFPPlannerWindow::HandleConveyorLiftSelected(
	TSharedPtr<FSFPTransportChoice> Choice,
	const ESelectInfo::Type SelectInfo)
{
	SelectedConveyorLift = MoveTemp(Choice);
	if (SelectedConveyorLift.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		StatusText = TEXT("Förderlift geändert – Produktionsplan neu berechnen");
	}
}

void SSFPPlannerWindow::HandlePowerGeneratorSelected(
	TSharedPtr<FSFPPowerGeneratorOption> Choice,
	const ESelectInfo::Type SelectInfo)
{
	PowerCalculationError.Reset();
	SelectedPowerGenerator = MoveTemp(Choice);
	if (SelectedPowerGenerator.IsValid())
	{
		const TOptional<double> MinClockValue = GetPowerGeneratorMinClockPercent();
		const TOptional<double> MaxClockValue = GetPowerGeneratorMaxClockPercent();
		const double MinClock = MinClockValue.IsSet() ? MinClockValue.GetValue() : 1.0;
		const double MaxClock = MaxClockValue.IsSet() ? MaxClockValue.GetValue() : 250.0;
		PowerGeneratorClockPercent = FMath::Clamp(PowerGeneratorClockPercent, MinClock, MaxClock);
	}
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	const FString GeneratorPath = SelectedPowerGenerator.IsValid()
		? SelectedPowerGenerator->ClassPath
		: FString();
	const FString FuelPath = SelectedPowerFuel.IsValid()
		? SelectedPowerFuel->ClassPath
		: FString();
	RefreshPowerChoices(GeneratorPath, FuelPath);
	StatusText = TEXT("Generator geändert – Stromplan neu berechnen");
}

void SSFPPlannerWindow::HandlePowerFuelSelected(
	TSharedPtr<FSFPPowerFuelOption> Choice,
	const ESelectInfo::Type SelectInfo)
{
	PowerCalculationError.Reset();
	SelectedPowerFuel = MoveTemp(Choice);
	if (SelectedPowerFuel.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		StatusText = TEXT("Brennstoff geändert – Stromplan neu berechnen");
	}
}

TSharedRef<SWidget> SSFPPlannerWindow::HandleGenerateTransportWidget(
	TSharedPtr<FSFPTransportChoice> Choice)
{
	const FString Label = Choice.IsValid()
		? FString::Printf(
			TEXT("%s  ·  %s/min%s"),
			*Choice->Tier.DisplayName,
			*FSFPNumberFormatting::Decimal(Choice->Tier.CapacityPerMinute, 0),
			Choice->Tier.bAvailable ? TEXT("") : TEXT("  ·  GESPERRT"))
		: TEXT("Keine Transportstufe geladen");
	return SNew(STextBlock)
		.Text(SFPLocalization::Text(Label))
		.ColorAndOpacity(Choice.IsValid() && Choice->Tier.bAvailable ? SFPTheme::Text : SFPTheme::MutedText);
}

TSharedRef<SWidget> SSFPPlannerWindow::HandleGeneratePowerGeneratorWidget(
	TSharedPtr<FSFPPowerGeneratorOption> Choice)
{
	FString Label = TEXT("Kein Generator geladen");
	if (Choice.IsValid())
	{
		const FString PowerPrefix = Choice->bVariableOutput ? TEXT("max. ") : FString();
		const FString Configuration = Choice->bModularPower && !Choice->ConfigurationDetail.IsEmpty()
			? FString::Printf(TEXT(" · %s"), *Choice->ConfigurationDetail)
			: FString();
		Label = Choice->ClassPath.IsEmpty()
			? Choice->DisplayName
			: FString::Printf(
				TEXT("%s · %s%s MW%s%s"),
				*Choice->DisplayName,
				*PowerPrefix,
				*FSFPNumberFormatting::Decimal(Choice->PowerProductionMW, 2),
				*Configuration,
				Choice->bAvailable ? TEXT("") : TEXT(" · GESPERRT"));
	}
	return SNew(STextBlock)
		.Text(SFPLocalization::Text(Label))
		.ColorAndOpacity(Choice.IsValid() && Choice->bAvailable ? SFPTheme::Text : SFPTheme::MutedText);
}

TSharedRef<SWidget> SSFPPlannerWindow::HandleGeneratePowerFuelWidget(
	TSharedPtr<FSFPPowerFuelOption> Choice)
{
	FString Label = TEXT("Kein Brennstoff geladen");
	if (Choice.IsValid())
	{
		if (Choice->ClassPath.IsEmpty() || Choice->bFuelFree)
		{
			Label = Choice->DisplayName;
			if (!Choice->bAvailable)
			{
				Label += TEXT(" · GESPERRT");
			}
		}
		else
		{
			const FString Consumption = Choice->ConsumptionRatePerGenerator > KINDA_SMALL_NUMBER
				? FString::Printf(
					TEXT(" · %s/min je Satz"),
					*FSFPNumberFormatting::Decimal(Choice->ConsumptionRatePerGenerator, 3))
				: FString();
			Label = FString::Printf(
				TEXT("%s · %s MJ%s%s"),
				*Choice->DisplayName,
				*FSFPNumberFormatting::Decimal(Choice->EnergyValueMJ, 3),
				*Consumption,
				Choice->bAvailable ? TEXT("") : TEXT(" · GESPERRT"));
		}
	}
	return SNew(STextBlock)
		.Text(SFPLocalization::Text(Label))
		.ColorAndOpacity(Choice.IsValid() && Choice->bAvailable ? SFPTheme::Text : SFPTheme::MutedText);
}

void SSFPPlannerWindow::HandleProductSelected(TSharedPtr<FSFPProductOption> Product, ESelectInfo::Type SelectInfo)
{
	SelectedProduct = MoveTemp(Product);
	if (SelectedProduct.IsValid())
	{
		StatusText = FString::Printf(
			TEXT("Markiert: %s | %d mögliche Rezepte%s | Menge festlegen und Ziel hinzufügen"),
			*SelectedProduct->DisplayName,
			SelectedProduct->RecipeCount,
			SelectedProduct->bHasAvailableRecipe ? TEXT(" | mindestens eines freigeschaltet") : TEXT(" | derzeit gesperrt"));
	}
}

TSharedRef<ITableRow> SSFPPlannerWindow::HandleGenerateProductRow(
	TSharedPtr<FSFPProductOption> Product,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Label = Product.IsValid()
		? FString::Printf(TEXT("%s  [%s]"), *Product->DisplayName, *Product->SourceMount)
		: TEXT("Ungültiges Produkt");
	return SNew(STableRow<TSharedPtr<FSFPProductOption>>, OwnerTable)
	[
		SNew(STextBlock)
		.Text(SFPLocalization::Text(Label))
		.ToolTipText(Product.IsValid() ? FText::FromString(Product->ClassPath) : FText::GetEmpty())
	];
}

FReply SSFPPlannerWindow::HandleAddTarget()
{
	if (!SelectedProduct.IsValid())
	{
		StatusText = TEXT("Bitte zuerst ein Produkt markieren");
		return FReply::Handled();
	}
	if (!FMath::IsFinite(TargetRate) || TargetRate <= 0.0)
	{
		StatusText = TEXT("Die Endproduktmenge muss größer als 0 sein");
		return FReply::Handled();
	}

	const TSharedPtr<FSFPSelectedTarget>* Existing = SelectedTargets.FindByPredicate(
		[this](const TSharedPtr<FSFPSelectedTarget>& Target)
		{
			return Target.IsValid()
				&& Target->Product.IsValid()
				&& Target->Product->ClassPath == SelectedProduct->ClassPath;
		});
	if (Existing != nullptr && Existing->IsValid())
	{
		(*Existing)->RatePerMinute = TargetRate;
        if (bInputPlanning) (*Existing)->bFixed = true;
		StatusText = FString::Printf(
			TEXT("%s auf %s/min aktualisiert"),
			*SelectedProduct->DisplayName,
			*FSFPNumberFormatting::Decimal(TargetRate));
	}
	else
	{
		TSharedPtr<FSFPSelectedTarget> NewTarget = MakeShared<FSFPSelectedTarget>();
		NewTarget->Product = SelectedProduct;
		NewTarget->RatePerMinute = TargetRate;
		SelectedTargets.Add(MoveTemp(NewTarget));
		StatusText = FString::Printf(
			TEXT("%s mit %s/min hinzugefügt — %d Endprodukt(e) ausgewählt"),
			*SelectedProduct->DisplayName,
			*FSFPNumberFormatting::Decimal(TargetRate),
			SelectedTargets.Num());
	}
	if (TargetList.IsValid())
	{
		TargetList->RequestListRefresh();
	}
	RefreshRecipeChoices(nullptr);
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleRemoveTarget(TSharedPtr<FSFPSelectedTarget> Target)
{
	if (Target.IsValid())
	{
		const FString RemovedName = Target->Product.IsValid()
			? Target->Product->DisplayName
			: TEXT("Ungültiges Ziel");
		SelectedTargets.Remove(Target);
		RefreshRecipeChoices(nullptr);
		StatusText = FString::Printf(
			TEXT("%s entfernt — Plan neu berechnen, um die Änderung zu übernehmen"),
			*RemovedName);
		if (TargetList.IsValid())
		{
			TargetList->RequestListRefresh();
		}
	}
	return FReply::Handled();
}

TSharedRef<ITableRow> SSFPPlannerWindow::HandleGenerateTargetRow(
	TSharedPtr<FSFPSelectedTarget> Target,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FSFPSelectedTarget>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.58f)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 2.0f, 6.0f, 2.0f)
		[
			SNew(STextBlock)
				.Text(Target.IsValid() && Target->Product.IsValid()
					? FText::FromString(Target->Product->DisplayName)
					: SFPLocalization::Text(TEXT("Ungültiges Ziel")))
				.ToolTipText(Target.IsValid() && Target->Product.IsValid()
					? FText::FromString(Target->Product->ClassPath)
					: FText::GetEmpty())
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.27f)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 2.0f, 6.0f, 2.0f)
		[
			SNew(SNumericEntryBox<double>)
				.AllowSpin(true)
				.MinValue(0.0)
				.MaxValue(1000000.0)
				.Value_Lambda([Target]() -> TOptional<double>
				{
					return Target.IsValid()
						? TOptional<double>(Target->RatePerMinute)
						: TOptional<double>();
				})
				.OnValueChanged_Lambda([this, Target](const double NewValue)
				{
					if (Target.IsValid())
					{
						Target->RatePerMinute = FMath::Max(0.0, NewValue);
                        if (bInputPlanning) Target->bFixed = true;
						StatusText = TEXT("Zielmenge geändert — Plan neu berechnen");
					}
				})
		]
        + SHorizontalBox::Slot().AutoWidth().Padding(3, 2)
        [ SNew(SCheckBox)
          .Visibility_Lambda([this]() { return bInputPlanning ? EVisibility::Visible : EVisibility::Collapsed; })
          .IsChecked_Lambda([Target]() { return Target->bFixed ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
          .OnCheckStateChanged_Lambda([this, Target](ECheckBoxState State) { Target->bFixed = State == ECheckBoxState::Checked; StatusText = TEXT("Zielmenge geändert — Plan neu berechnen"); })
          [ SNew(STextBlock).Text_Lambda([Target]() { return SFPLocalization::Text(Target->bFixed ? TEXT("Fest") : TEXT("Frei")); }) ] ]
		+ SHorizontalBox::Slot()
		.FillWidth(0.15f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
				.Text(SFPLocalization::Text(TEXT("ENTFERNEN")))
				.OnClicked(this, &SSFPPlannerWindow::HandleRemoveTarget, Target)
		]
	];
}

FReply SSFPPlannerWindow::HandleCalculate()
{
	if (!Solver.IsValid())
	{
		StatusText = TEXT("Der Planner-Solver ist nicht verfügbar");
		return FReply::Handled();
	}
	if (CurrentPlan.IsValid() && CurrentPlan->bPowerProductionPlan && SelectedTargets.IsEmpty())
	{
		return HandleCalculatePower();
	}
	if (SelectedTargets.IsEmpty())
	{
		StatusText = TEXT("Bitte mindestens ein Endprodukt mit + Ziel hinzufügen");
		return FReply::Handled();
	}
	// Keep values for inputs that still exist when only a recipe or rate changes.
	CalculateForTargets(1.0, true);
	return FReply::Handled();
}

void SSFPPlannerWindow::ReceiveResourceNodeInventory(
	const FString& InventoryJson,
	const FString& Error)
{
	ResourceNodeInventoryError = Error;
	ResourceNodeAvailability.Reset();
	if (Error.IsEmpty() && !InventoryJson.IsEmpty())
	{
		FString ParseError;
		if (!SFPResourceNodeInventory::FromJson(
			InventoryJson,
			ResourceNodeAvailability,
			ParseError))
		{
			ResourceNodeInventoryError = ParseError;
		}
	}
	if (RecipeChoiceList.IsValid())
	{
		RecipeChoiceList->RebuildList();
	}
}

TMap<FString, FSFPResourceSourceMix> SSFPPlannerWindow::BuildEffectiveResourceSourceMixes() const
{
	TMap<FString, FSFPResourceSourceMix> Effective = ResourceSourceMixes;
	for (const TPair<FString, FSFPResourceNodeAvailability>& Pair : ResourceNodeAvailability)
	{
		FSFPResourceSourceMix& Mix = Effective.FindOrAdd(Pair.Key);
		const FSFPResourceNodeAvailability& Live = Pair.Value;
		auto ApplyLiveLimit = [&Mix](
			const int32 Total,
			const int32 Occupied,
			bool FSFPResourceSourceMix::* LimitedMember,
			int32 FSFPResourceSourceMix::* CountMember)
		{
			if (Mix.*LimitedMember) return;
			Mix.*LimitedMember = true;
			Mix.*CountMember = Mix.bUseOccupiedSources
				? FMath::Max(0, Total)
				: FMath::Max(0, Total - Occupied);
		};
		ApplyLiveLimit(Live.ImpureTotal, Live.ImpureOccupied,
			&FSFPResourceSourceMix::bImpureLimited, &FSFPResourceSourceMix::ImpureCount);
		ApplyLiveLimit(Live.NormalTotal, Live.NormalOccupied,
			&FSFPResourceSourceMix::bNormalLimited, &FSFPResourceSourceMix::NormalCount);
		ApplyLiveLimit(Live.PureTotal, Live.PureOccupied,
			&FSFPResourceSourceMix::bPureLimited, &FSFPResourceSourceMix::PureCount);
	}
	return Effective;
}

bool SSFPPlannerWindow::AreMaximumPowerResourceSettingsPrepared(
	TArray<FString>& OutMissingItems) const
{
	OutMissingItems.Reset();
	TArray<TSharedPtr<FSFPRecipeChoiceRow>> Rows = RecipeChoiceRows;
	for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : RecipeChoiceRows)
	{
		if (Row.IsValid())
		{
			Rows.Append(Row->GroupedRawExtractions);
		}
	}

	for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : Rows)
	{
		if (!Row.IsValid() || !Row->Selected.IsValid()
			|| Row->Selected->Category != TEXT("Direktabbau / Förderung"))
		{
			continue;
		}

		TSet<FString> ExtractionConfigurations;
		for (const TSharedPtr<FSFPRecipeOption>& Option : Row->Options)
		{
			if (!Option.IsValid()
				|| Option->Category != TEXT("Direktabbau / Förderung")
				|| (bOnlyAvailable && !Option->bAvailable))
			{
				continue;
			}
			ExtractionConfigurations.Add(FString::Printf(
				TEXT("%s|%s|%s|%s"),
				*Option->SourceName,
				*Option->MachineClassPath,
				*Option->ModulesLabel,
				*Option->FluidLabel));
		}

		// A single extraction configuration has nothing for the player to decide.
		// Multiple miners, drill heads, modules or operating fluids must be
		// confirmed explicitly before they define a world-resource maximum.
		if (ExtractionConfigurations.Num() > 1
			&& !RecipeOverrides.Contains(Row->ItemClassPath))
		{
			OutMissingItems.AddUnique(CurrentPlannerItemName(Row->ItemName, Row->ItemClassPath));
		}
	}
	return OutMissingItems.IsEmpty();
}

FReply SSFPPlannerWindow::HandleCalculatePower()
{
	PowerCalculationError.Reset();
	if (!Solver.IsValid())
	{
		StatusText = TEXT("Der Planner-Solver ist nicht verfügbar");
		return FReply::Handled();
	}
	if (!SelectedPowerGenerator.IsValid() || !SelectedPowerFuel.IsValid())
	{
		StatusText = TEXT("Generator- und Brennstoffauswahl konnten nicht geladen werden");
		return FReply::Handled();
	}
	if (!SelectedConveyor.IsValid() || !SelectedConveyorLift.IsValid())
	{
		StatusText = TEXT("Förderband und Förderlift müssen für die Brennstoffkette ausgewählt sein");
		return FReply::Handled();
	}
	// A missing factory plan must never block manual power and fuel planning.
	if (bUseCurrentFactoryPowerDemand && LastFactoryPowerDemandMW <= KINDA_SMALL_NUMBER)
	{
		bUseCurrentFactoryPowerDemand = false;
	}

	FSFPPowerPlanRequest Request;
	Request.TargetNetPowerMW = ResolvePowerTargetNetMW();
	Request.ReservePercent = PowerReservePercent;
	Request.bOnlyAvailable = bOnlyAvailable;
	Request.GeneratorClassPath = SelectedPowerGenerator->ClassPath;
	Request.FuelClassPath = SelectedPowerFuel->ClassPath;
	Request.GeneratorClockPercent = PowerGeneratorClockPercent;
	Request.PassiveAlienPowerAugmenters = PassiveAlienPowerAugmenters;
	Request.FueledAlienPowerAugmenters = FueledAlienPowerAugmenters;
	Request.RecipeOverrides = RecipeOverrides;
	Request.MachineSettings = MachineSettingsOverrides;
	Request.ResourceSourceMixes = BuildEffectiveResourceSourceMixes();
	Request.EstimatedConnectionLengthMeters = EstimatedConnectionLengthMeters;
	Request.SelectedConveyorClassPath = SelectedConveyor->Tier.ClassPath;
	Request.SelectedConveyorLiftClassPath = SelectedConveyorLift->Tier.ClassPath;

	const double SolveStartTime = FPlatformTime::Seconds();
	const TSharedPtr<FSFPPlanResult> PreviousPlan = CurrentPlan;
	TSharedPtr<FSFPPlanResult> Plan = MakeShared<FSFPPlanResult>(Solver->SolvePower(Request));
	const double SolveMilliseconds = (FPlatformTime::Seconds() - SolveStartTime) * 1000.0;
	if (!Plan->bSuccess)
	{
		PowerCalculationError = Plan->ErrorMessage;
		StatusText = FString::Printf(TEXT("Stromberechnung fehlgeschlagen: %s"), *PowerCalculationError);
		return FReply::Handled();
	}
	if ((!Request.GeneratorClassPath.IsEmpty()
			&& Plan->SelectedGeneratorClassPath != Request.GeneratorClassPath)
		|| (!Request.FuelClassPath.IsEmpty()
			&& Plan->SelectedFuelClassPath != Request.FuelClassPath))
	{
		PowerCalculationError = TEXT("Der berechnete Stromplan entspricht nicht der gewählten Generator-/Brennstoffkombination");
		StatusText = FString::Printf(TEXT("Stromberechnung fehlgeschlagen: %s"), *PowerCalculationError);
		return FReply::Handled();
	}
	// Live world counts are recalculated whenever the planner opens and are not
	// persisted as manual caps in a personal or multiplayer plan.
	Plan->ResourceSourceMixes = ResourceSourceMixes;

	CarryForwardNodeCompletion(bCarryCurrentPlanProgress ? PreviousPlan.Get() : nullptr, *Plan);
	if (MachineGuidanceState == 1) MachineGuidanceState = 2;
	CurrentPlan = Plan;
	bCarryCurrentPlanProgress = true;
	bSharedPlanContentDirty = bSharedPlanMode && !ActiveSharedPlanFileName.IsEmpty();
	SelectedTargets.Reset();
	SelectedProduct.Reset();
	if (TargetList.IsValid())
	{
		TargetList->RequestListRefresh();
	}
	if (ProductList.IsValid())
	{
		ProductList->ClearSelection();
	}
	if (GraphPanel.IsValid())
	{
		GraphPanel->SetPlan(Plan);
	}
	RefreshRecipeChoices(Plan);
	RefreshInputBudgets(Plan, true);
	CaptureInputBudgetsToPlan();
	StatusText = BuildPlanStatusText(
		*Plan,
		SolveMilliseconds,
		TEXT("Strom- und Brennstoffproduktion einschließlich Eigenverbrauch berechnet"));
	PersistCurrentPlan(false);
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleCalculateMaximumPower()
{
	PowerCalculationError.Reset();
	if (!Solver.IsValid())
	{
		StatusText = TEXT("Der Planner-Solver ist nicht verfügbar");
		return FReply::Handled();
	}
	if (!SelectedPowerGenerator.IsValid() || !SelectedPowerFuel.IsValid())
	{
		StatusText = TEXT("Generator- und Brennstoffauswahl konnten nicht geladen werden");
		return FReply::Handled();
	}
	if (SelectedPowerGenerator->ClassPath.IsEmpty() || SelectedPowerFuel->ClassPath.IsEmpty())
	{
		StatusText = TEXT("Für die Maximalberechnung einen konkreten Generator und Brennstoff auswählen.");
		return FReply::Handled();
	}
	if (!SelectedConveyor.IsValid() || !SelectedConveyorLift.IsValid())
	{
		StatusText = TEXT("Förderband und Förderlift müssen für die Brennstoffkette ausgewählt sein");
		return FReply::Handled();
	}
	if (!CurrentPlan.IsValid() || !CurrentPlan->bPowerProductionPlan || RecipeChoiceRows.IsEmpty())
	{
		StatusText = TEXT("Zuerst die gewählte Strom- und Brennstoffproduktion einmal normal berechnen.");
		return FReply::Handled();
	}
	if (CurrentPlan->SelectedGeneratorClassPath != SelectedPowerGenerator->ClassPath
		|| CurrentPlan->SelectedFuelClassPath != SelectedPowerFuel->ClassPath)
	{
		StatusText = TEXT("Generator oder Brennstoff wurde geändert. Vor der Maximalberechnung einmal normal neu berechnen.");
		return FReply::Handled();
	}
	if (ResourceNodeAvailability.IsEmpty())
	{
		StatusText = ResourceNodeInventoryError.IsEmpty()
			? TEXT("Die Welt-Rohstoffinventur ist noch nicht verfügbar.")
			: FString::Printf(TEXT("Die Welt-Rohstoffinventur ist nicht verfügbar: %s"), *ResourceNodeInventoryError);
		return FReply::Handled();
	}

	TArray<FString> MissingResourceSettings;
	if (!AreMaximumPowerResourceSettingsPrepared(MissingResourceSettings))
	{
		StatusText = FString::Printf(
			TEXT("Vor der Maximalberechnung im Reiter Planung Miner, Bohrköpfe, Module und Betriebsflüssigkeiten bestätigen: %s"),
			*FString::Join(MissingResourceSettings, TEXT(", ")));
		return FReply::Handled();
	}

	auto BuildRequest = [this](
		const double TargetNetPowerMW,
		const TMap<FString, FString>& EffectiveRecipeOverrides)
	{
		FSFPPowerPlanRequest Request;
		Request.TargetNetPowerMW = TargetNetPowerMW;
		Request.ReservePercent = PowerReservePercent;
		Request.bOnlyAvailable = bOnlyAvailable;
		Request.GeneratorClassPath = SelectedPowerGenerator->ClassPath;
		Request.FuelClassPath = SelectedPowerFuel->ClassPath;
		Request.GeneratorClockPercent = PowerGeneratorClockPercent;
		Request.PassiveAlienPowerAugmenters = PassiveAlienPowerAugmenters;
		Request.FueledAlienPowerAugmenters = FueledAlienPowerAugmenters;
		Request.RecipeOverrides = EffectiveRecipeOverrides;
		Request.MachineSettings = MachineSettingsOverrides;
		Request.ResourceSourceMixes = BuildEffectiveResourceSourceMixes();
		// A world maximum always allocates every available purity. The normal
		// planner may keep automatic mixing disabled for a hand-authored plan,
		// but that would leave a single selected purity artificially unlimited.
		for (const TPair<FString, FSFPResourceNodeAvailability>& Pair : ResourceNodeAvailability)
		{
			Request.ResourceSourceMixes.FindOrAdd(Pair.Key).bEnabled = true;
		}
		Request.EstimatedConnectionLengthMeters = EstimatedConnectionLengthMeters;
		Request.SelectedConveyorClassPath = SelectedConveyor->Tier.ClassPath;
		Request.SelectedConveyorLiftClassPath = SelectedConveyorLift->Tier.ClassPath;
		return Request;
	};
	auto SolveProbe = [this, &BuildRequest](
		const double TargetNetPowerMW,
		const TMap<FString, FString>& EffectiveRecipeOverrides,
		TSharedPtr<FSFPPlanResult>& OutPlan) -> bool
	{
		OutPlan = MakeShared<FSFPPlanResult>(Solver->SolvePower(BuildRequest(
			TargetNetPowerMW,
			EffectiveRecipeOverrides)));
		// A mathematically positive target can still round down to no physical
		// generator. Such a result cannot seed a buildable world-limit search.
		return OutPlan.IsValid()
			&& OutPlan->bSuccess
			&& OutPlan->BuiltGeneratorCount > 0
			&& OutPlan->GrossPowerMW > KINDA_SMALL_NUMBER
			&& FMath::IsFinite(OutPlan->NetPowerMW);
	};

	constexpr double MaximumSearchCeilingMW = 1000000000.0;
	const double SolveStartTime = FPlatformTime::Seconds();
	auto RecordMaximumPowerCalculationDuration = [this, SolveStartTime]()
	{
		LastMaximumPowerCalculationSeconds = FMath::Max(
			0.0,
			FPlatformTime::Seconds() - SolveStartTime);
		SmoothedMaximumPowerCalculationSeconds = SmoothedMaximumPowerCalculationSeconds > 0.0
			? FMath::Lerp(
				SmoothedMaximumPowerCalculationSeconds,
				LastMaximumPowerCalculationSeconds,
				0.6)
			: LastMaximumPowerCalculationSeconds;
	};
	TSharedPtr<FSFPPlanResult> BestFeasiblePlan;
	const TOptional<double> MinimumGeneratorClockValue = GetPowerGeneratorMinClockPercent();
	const double MinimumGeneratorClockPercent = MinimumGeneratorClockValue.IsSet()
		? MinimumGeneratorClockValue.GetValue()
		: 1.0;
	const double MinimumSearchTargetMW = FMath::Clamp(
		SelectedPowerGenerator->PowerProductionMW
			* MinimumGeneratorClockPercent / 100.0,
		1.0,
		MaximumSearchCeilingMW);
	const double OneConfiguredGeneratorMW = FMath::Max(
		MinimumSearchTargetMW,
		SelectedPowerGenerator->PowerProductionMW
			* PowerGeneratorClockPercent / 100.0);
	double HighTargetMW = FMath::Clamp(
		FMath::Max(ResolvePowerTargetNetMW(), OneConfiguredGeneratorMW),
		MinimumSearchTargetMW,
		MaximumSearchCeilingMW);
	double LowTargetMW = 0.0;
	TSharedPtr<FSFPPlanResult> HighPlan;
	TSharedPtr<FSFPPlanResult> LimitingPlan;
	FString LastInfeasibleError;
	bool bHighSolved = SolveProbe(HighTargetMW, RecipeOverrides, HighPlan);
	if (!bHighSolved && HighPlan.IsValid())
	{
		LastInfeasibleError = HighPlan->ErrorMessage;
	}

	if (!bHighSolved || HighPlan->bResourceSourceLimitsExceeded)
	{
		// The player's current target can already be above the world limit or
		// beyond the numerically solvable range of a large modded feedback chain.
		// Walk down until a physical supplied plan exists. Failed probes remain
		// valid conservative upper bounds instead of aborting the whole search.
		if (bHighSolved && HighPlan->bResourceSourceLimitsExceeded)
		{
			LimitingPlan = HighPlan;
		}
		double DownwardProbeMW = HighTargetMW;
		for (int32 Reduction = 0; Reduction < 40; ++Reduction)
		{
			const double NextProbeMW = FMath::Max(
				MinimumSearchTargetMW,
				DownwardProbeMW * 0.5);
			if (NextProbeMW >= DownwardProbeMW - KINDA_SMALL_NUMBER)
			{
				break;
			}
			DownwardProbeMW = NextProbeMW;

			TSharedPtr<FSFPPlanResult> ProbePlan;
			const bool bProbeSolved = SolveProbe(DownwardProbeMW, RecipeOverrides, ProbePlan);
			if (!bProbeSolved)
			{
				HighTargetMW = DownwardProbeMW;
				HighPlan = ProbePlan;
				if (ProbePlan.IsValid() && !ProbePlan->ErrorMessage.IsEmpty())
				{
					LastInfeasibleError = ProbePlan->ErrorMessage;
				}
				continue;
			}
			if (ProbePlan->bResourceSourceLimitsExceeded)
			{
				HighTargetMW = DownwardProbeMW;
				HighPlan = ProbePlan;
				LimitingPlan = ProbePlan;
				continue;
			}

			LowTargetMW = DownwardProbeMW;
			BestFeasiblePlan = ProbePlan;
			break;
		}

		if (!BestFeasiblePlan.IsValid())
		{
			if (LimitingPlan.IsValid())
			{
				const FString LimitingResource = LimitingPlan->LimitingResourceDisplayName.IsEmpty()
					? TEXT("mindestens einem Rohstoff")
					: CurrentPlannerItemName(
						LimitingPlan->LimitingResourceDisplayName,
						LimitingPlan->LimitingResourceClassPath);
				PowerCalculationError = FString::Printf(
					TEXT("Bereits die kleinste physisch betreibbare Leistung überschreitet die verfügbare Menge von %s."),
					*LimitingResource);
			}
			else
			{
				PowerCalculationError = LastInfeasibleError.IsEmpty()
					? TEXT("Mit der gewählten Förderkonfiguration ist keine physisch betreibbare Nettoleistung möglich.")
					: LastInfeasibleError;
			}
			StatusText = FString::Printf(
				TEXT("Maximalberechnung fehlgeschlagen: %s"),
				*PowerCalculationError);
			RecordMaximumPowerCalculationDuration();
			return FReply::Handled();
		}
	}
	else
	{
		LowTargetMW = HighTargetMW;
		BestFeasiblePlan = HighPlan;
		for (int32 Expansion = 0;
			Expansion < 32 && HighTargetMW < MaximumSearchCeilingMW;
			++Expansion)
		{
			HighTargetMW = FMath::Min(MaximumSearchCeilingMW, HighTargetMW * 2.0);
			if (!SolveProbe(HighTargetMW, RecipeOverrides, HighPlan))
			{
				if (HighPlan.IsValid() && !HighPlan->ErrorMessage.IsEmpty())
				{
					LastInfeasibleError = HighPlan->ErrorMessage;
				}
				break;
			}
			if (HighPlan->bResourceSourceLimitsExceeded)
			{
				LimitingPlan = HighPlan;
				break;
			}
			LowTargetMW = HighTargetMW;
			BestFeasiblePlan = HighPlan;
		}
	}

	bool bSearchCapped = HighTargetMW >= MaximumSearchCeilingMW
		&& HighPlan.IsValid() && HighPlan->bSuccess
		&& !HighPlan->bResourceSourceLimitsExceeded;
	if (bSearchCapped)
	{
		LimitingPlan.Reset();
	}
	if (!bSearchCapped)
	{
		for (int32 Refinement = 0; Refinement < 24; ++Refinement)
		{
			const double WidthMW = HighTargetMW - LowTargetMW;
			if (WidthMW <= FMath::Max(0.01, LowTargetMW * 1.0e-7))
			{
				break;
			}
			const double ProbeTargetMW = LowTargetMW + WidthMW * 0.5;
			TSharedPtr<FSFPPlanResult> ProbePlan;
			if (!SolveProbe(ProbeTargetMW, RecipeOverrides, ProbePlan))
			{
				// A failed high-side probe still narrows the conservative maximum.
				// Keep the last fully buildable plan and continue below this bound.
				HighTargetMW = ProbeTargetMW;
				if (ProbePlan.IsValid() && !ProbePlan->ErrorMessage.IsEmpty())
				{
					LastInfeasibleError = ProbePlan->ErrorMessage;
				}
				continue;
			}
			if (ProbePlan->bResourceSourceLimitsExceeded)
			{
				HighTargetMW = ProbeTargetMW;
				LimitingPlan = ProbePlan;
			}
			else
			{
				LowTargetMW = ProbeTargetMW;
				BestFeasiblePlan = ProbePlan;
			}
		}
	}

	if (!BestFeasiblePlan.IsValid())
	{
		StatusText = TEXT("Maximalberechnung fehlgeschlagen: Kein versorgter Stromplan gefunden.");
		RecordMaximumPowerCalculationDuration();
		return FReply::Handled();
	}

	// A physical world maximum must not be tied to the one recipe chain picked by
	// the normal deterministic selector. Preserve every explicit player override,
	// but test every other available production recipe reached by the power chain.
	// Accepted alternatives can reveal new intermediate items, therefore repeat
	// the pass until the chain is stable. This is deliberately limited to the
	// maximum calculation; ordinary planning remains immediate and predictable.
	const TMap<FString, FString> ManualRecipeOverrides = RecipeOverrides;
	TMap<FString, FString> OptimizedRecipeOverrides = RecipeOverrides;
	int32 RecipeCandidatesTested = 0;
	int32 RecipeAlternativesAccepted = 0;
	int32 RecipeCandidatesAwaitingExtractionSettings = 0;

	auto HasUnconfirmedExtractionSettings = [
		this,
		&ManualRecipeOverrides](const TSharedPtr<FSFPPlanResult>& Plan) -> bool
	{
		if (!Plan.IsValid()) return true;
		TSet<FString> SeenItems;
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			if (Node.Type != ESFPPlanNodeType::Machine
				|| Node.ProducedItemClassPath.IsEmpty()
				|| SeenItems.Contains(Node.ProducedItemClassPath))
			{
				continue;
			}
			SeenItems.Add(Node.ProducedItemClassPath);

			TArray<TSharedPtr<FSFPRecipeOption>> Options;
			Solver->GetRecipeOptionsForItemPath(
				Node.ProducedItemClassPath,
				bOnlyAvailable,
				Options);
			const TSharedPtr<FSFPRecipeOption>* SelectedOption = Options.FindByPredicate(
				[&Node](const TSharedPtr<FSFPRecipeOption>& Option)
				{
					return Option.IsValid()
						&& Option->RecipeClassPath == Node.RecipeClassPath;
				});
			if (SelectedOption == nullptr || !SelectedOption->IsValid()
				|| (*SelectedOption)->Category != TEXT("Direktabbau / Förderung"))
			{
				continue;
			}

			TSet<FString> ExtractionConfigurations;
			for (const TSharedPtr<FSFPRecipeOption>& Option : Options)
			{
				if (!Option.IsValid()
					|| Option->Category != TEXT("Direktabbau / Förderung")
					|| (bOnlyAvailable && !Option->bAvailable))
				{
					continue;
				}
				ExtractionConfigurations.Add(FString::Printf(
					TEXT("%s|%s|%s|%s"),
					*Option->SourceName,
					*Option->MachineClassPath,
					*Option->ModulesLabel,
					*Option->FluidLabel));
			}
			if (ExtractionConfigurations.Num() > 1
				&& !ManualRecipeOverrides.Contains(Node.ProducedItemClassPath))
			{
				return true;
			}
		}
		return false;
	};

	auto SearchUpwardForOverrides = [
		this,
		&SolveProbe,
		MinimumSearchTargetMW,
		MaximumSearchCeilingMW](
		const TMap<FString, FString>& EffectiveRecipeOverrides,
		const TSharedPtr<FSFPPlanResult>& SeedPlan,
		TSharedPtr<FSFPPlanResult>& OutLimitingPlan,
		bool& bOutSearchCapped,
		FString& OutFailure) -> TSharedPtr<FSFPPlanResult>
	{
		OutLimitingPlan.Reset();
		bOutSearchCapped = false;
		OutFailure.Reset();
		if (!SeedPlan.IsValid() || !SeedPlan->bSuccess
			|| SeedPlan->bResourceSourceLimitsExceeded)
		{
			return nullptr;
		}

		TSharedPtr<FSFPPlanResult> BestPlan = SeedPlan;
		double LowMW = FMath::Clamp(
			SeedPlan->RequestedNetPowerMW,
			MinimumSearchTargetMW,
			MaximumSearchCeilingMW);
		double HighMW = LowMW;
		bool bUpperBoundFound = false;

		for (int32 Expansion = 0;
			Expansion < 32 && HighMW < MaximumSearchCeilingMW;
			++Expansion)
		{
			const double NextHighMW = FMath::Min(
				MaximumSearchCeilingMW,
				FMath::Max(HighMW * 2.0, HighMW + MinimumSearchTargetMW));
			TSharedPtr<FSFPPlanResult> ProbePlan;
			const bool bSolved = SolveProbe(
				NextHighMW,
				EffectiveRecipeOverrides,
				ProbePlan);
			HighMW = NextHighMW;
			if (!bSolved)
			{
				bUpperBoundFound = true;
				if (ProbePlan.IsValid()) OutFailure = ProbePlan->ErrorMessage;
				break;
			}
			if (ProbePlan->bResourceSourceLimitsExceeded)
			{
				bUpperBoundFound = true;
				OutLimitingPlan = ProbePlan;
				break;
			}
			LowMW = HighMW;
			BestPlan = ProbePlan;
		}

		if (!bUpperBoundFound && HighMW >= MaximumSearchCeilingMW)
		{
			bOutSearchCapped = true;
			return BestPlan;
		}

		for (int32 Refinement = 0; Refinement < 24; ++Refinement)
		{
			const double WidthMW = HighMW - LowMW;
			if (WidthMW <= FMath::Max(0.01, LowMW * 1.0e-7))
			{
				break;
			}
			const double ProbeMW = LowMW + WidthMW * 0.5;
			TSharedPtr<FSFPPlanResult> ProbePlan;
			if (!SolveProbe(ProbeMW, EffectiveRecipeOverrides, ProbePlan))
			{
				HighMW = ProbeMW;
				if (ProbePlan.IsValid() && !ProbePlan->ErrorMessage.IsEmpty())
				{
					OutFailure = ProbePlan->ErrorMessage;
				}
				continue;
			}
			if (ProbePlan->bResourceSourceLimitsExceeded)
			{
				HighMW = ProbeMW;
				OutLimitingPlan = ProbePlan;
			}
			else
			{
				LowMW = ProbeMW;
				BestPlan = ProbePlan;
			}
		}
		return BestPlan;
	};

	auto CalculateResourceHeadroom = [](const TSharedPtr<FSFPPlanResult>& Plan) -> double
	{
		if (!Plan.IsValid()) return 0.0;
		double HighestSourceUtilization = 0.0;
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			if (Node.Type != ESFPPlanNodeType::Machine
				|| Node.MaximumMachineCount <= KINDA_SMALL_NUMBER
				|| Node.MachineCount <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			HighestSourceUtilization = FMath::Max(
				HighestSourceUtilization,
				Node.MachineCount / Node.MaximumMachineCount);
		}
		return HighestSourceUtilization > KINDA_SMALL_NUMBER
			? 1.0 / HighestSourceUtilization
			: 1.0;
	};

	// Keep the Slate/game thread responsive enough for large Satisfactory Plus
	// catalogs. Every alternative gets one fixed-target feasibility probe; only
	// the best resource-headroom route is adopted per pass. The expensive full
	// maximum search runs once after the recipe chain has stabilized.
	constexpr int32 MaximumRecipeOptimizationPasses = 3;
	constexpr int32 MaximumRecipeCandidateProbes = 16;
	constexpr double MaximumRecipeOptimizationSeconds = 1.0;
	const double RecipeOptimizationStartTime = FPlatformTime::Seconds();
	bool bRecipeSearchBudgetReached = false;

	for (int32 OptimizationPass = 0;
		OptimizationPass < MaximumRecipeOptimizationPasses;
		++OptimizationPass)
	{
		TMap<FString, FString> ActiveRecipeByItem;
		for (const FSFPPlanNode& Node : BestFeasiblePlan->Nodes)
		{
			if (Node.Type == ESFPPlanNodeType::Machine
				&& !Node.ProducedItemClassPath.IsEmpty()
				&& !Node.RecipeClassPath.IsEmpty())
			{
				ActiveRecipeByItem.FindOrAdd(Node.ProducedItemClassPath) = Node.RecipeClassPath;
			}
		}

		TSharedPtr<FSFPPlanResult> BestCandidatePlan;
		TMap<FString, FString> BestCandidateOverrides;
		double BestCandidateHeadroom = CalculateResourceHeadroom(BestFeasiblePlan);
		double BestCandidateSelfConsumption = BestFeasiblePlan->SelfConsumptionPowerMW;
		FString BestCandidateRecipePath;
		TArray<FString> ActiveItemPaths;
		ActiveRecipeByItem.GetKeys(ActiveItemPaths);
		ActiveItemPaths.Sort();

		for (const FString& ActiveItemPath : ActiveItemPaths)
		{
			if (RecipeCandidatesTested >= MaximumRecipeCandidateProbes
				|| FPlatformTime::Seconds() - RecipeOptimizationStartTime
					>= MaximumRecipeOptimizationSeconds)
			{
				bRecipeSearchBudgetReached = true;
				break;
			}
			// An explicit recipe selection is a hard constraint, including selected
			// Satisfactory Plus miner/drill-head variants.
			if (ManualRecipeOverrides.Contains(ActiveItemPath))
			{
				continue;
			}
			const FString& ActiveRecipePath = ActiveRecipeByItem.FindChecked(ActiveItemPath);

			TArray<TSharedPtr<FSFPRecipeOption>> Options;
			Solver->GetRecipeOptionsForItemPath(ActiveItemPath, bOnlyAvailable, Options);
			Options.Sort([](
				const TSharedPtr<FSFPRecipeOption>& Left,
				const TSharedPtr<FSFPRecipeOption>& Right)
			{
				return Left.IsValid() && Right.IsValid()
					? Left->RecipeClassPath < Right->RecipeClassPath
					: Left.IsValid();
			});
			for (const TSharedPtr<FSFPRecipeOption>& Option : Options)
			{
				if (RecipeCandidatesTested >= MaximumRecipeCandidateProbes
					|| FPlatformTime::Seconds() - RecipeOptimizationStartTime
						>= MaximumRecipeOptimizationSeconds)
				{
					bRecipeSearchBudgetReached = true;
					break;
				}
				if (!Option.IsValid()
					|| Option->RecipeClassPath.IsEmpty()
					|| Option->RecipeClassPath == ActiveRecipePath
					|| Option->Category == TEXT("Direktabbau / Förderung"))
				{
					continue;
				}

				++RecipeCandidatesTested;
				TMap<FString, FString> CandidateOverrides = OptimizedRecipeOverrides;
				CandidateOverrides.Add(ActiveItemPath, Option->RecipeClassPath);

				// A route unable to supply the incumbent maximum cannot improve it.
				TSharedPtr<FSFPPlanResult> CandidateSeed;
				if (!SolveProbe(
					BestFeasiblePlan->RequestedNetPowerMW,
					CandidateOverrides,
					CandidateSeed)
					|| CandidateSeed->bResourceSourceLimitsExceeded)
				{
					continue;
				}
				if (HasUnconfirmedExtractionSettings(CandidateSeed))
				{
					++RecipeCandidatesAwaitingExtractionSettings;
					continue;
				}

				const double CandidateHeadroom = CalculateResourceHeadroom(CandidateSeed);
				const double HeadroomTolerance = FMath::Max(
					1.0e-6,
					BestCandidateHeadroom * 1.0e-6);
				const bool bMoreHeadroom = CandidateHeadroom
					> BestCandidateHeadroom + HeadroomTolerance;
				const bool bEqualHeadroom = FMath::IsNearlyEqual(
					CandidateHeadroom,
					BestCandidateHeadroom,
					HeadroomTolerance);
				const bool bLowerSelfConsumption = bEqualHeadroom
					&& CandidateSeed->SelfConsumptionPowerMW
						< BestCandidateSelfConsumption - 0.01;
				const bool bStableTieBreak = BestCandidatePlan.IsValid()
					&& bEqualHeadroom
					&& FMath::IsNearlyEqual(
						CandidateSeed->SelfConsumptionPowerMW,
						BestCandidateSelfConsumption,
						0.01)
					&& (BestCandidateRecipePath.IsEmpty()
						|| Option->RecipeClassPath < BestCandidateRecipePath);
				if (bMoreHeadroom || bLowerSelfConsumption || bStableTieBreak)
				{
					BestCandidatePlan = CandidateSeed;
					BestCandidateOverrides = MoveTemp(CandidateOverrides);
					BestCandidateHeadroom = CandidateHeadroom;
					BestCandidateSelfConsumption = CandidateSeed->SelfConsumptionPowerMW;
					BestCandidateRecipePath = Option->RecipeClassPath;
				}
			}
			if (bRecipeSearchBudgetReached) break;
		}

		if (!BestCandidatePlan.IsValid())
		{
			break;
		}
		OptimizedRecipeOverrides = MoveTemp(BestCandidateOverrides);
		BestFeasiblePlan = BestCandidatePlan;
		++RecipeAlternativesAccepted;
		if (bRecipeSearchBudgetReached) break;
	}

	if (RecipeAlternativesAccepted > 0)
	{
		TSharedPtr<FSFPPlanResult> OptimizedLimitingPlan;
		bool bOptimizedSearchCapped = false;
		FString OptimizedFailure;
		TSharedPtr<FSFPPlanResult> OptimizedMaximum = SearchUpwardForOverrides(
			OptimizedRecipeOverrides,
			BestFeasiblePlan,
			OptimizedLimitingPlan,
			bOptimizedSearchCapped,
			OptimizedFailure);
		if (OptimizedMaximum.IsValid())
		{
			BestFeasiblePlan = OptimizedMaximum;
			LimitingPlan = OptimizedLimitingPlan;
			LastInfeasibleError = OptimizedFailure;
			bSearchCapped = bOptimizedSearchCapped;
		}
	}

	if (RecipeCandidatesTested > 0)
	{
		BestFeasiblePlan->Warnings.AddUnique(FString::Printf(
			TEXT("Maximalsuche: %d alternative Produktionsrezepte geprüft, %d bessere Rezeptwechsel übernommen. Manuell festgelegte Rezepte blieben unverändert."),
			RecipeCandidatesTested,
			RecipeAlternativesAccepted));
	}
	if (RecipeCandidatesAwaitingExtractionSettings > 0)
	{
		BestFeasiblePlan->Warnings.AddUnique(FString::Printf(
			TEXT("Maximalsuche: %d Rezeptvarianten benötigen zuerst eine bestätigte Miner-, Bohrkopf-, Modul- oder Betriebsflüssigkeitsauswahl im Reiter Planung."),
			RecipeCandidatesAwaitingExtractionSettings));
	}
	if (bRecipeSearchBudgetReached)
	{
		BestFeasiblePlan->Warnings.AddUnique(FString::Printf(
			TEXT("Maximalsuche: Die Rezeptoptimierung wurde nach %d Kandidaten zeitlich begrenzt. Das Ergebnis ist baubar, kann aber unter dem theoretischen Maximum liegen."),
			RecipeCandidatesTested));
	}
	if (!LastInfeasibleError.IsEmpty() && !LimitingPlan.IsValid())
	{
		BestFeasiblePlan->Warnings.AddUnique(FString::Printf(
			TEXT("Maximalsuche: Ein höherer Probelauf war nicht vollständig lösbar; das letzte vollständig berechenbare Ergebnis wird als konservative Obergrenze verwendet. %s"),
			*LastInfeasibleError));
	}

	BestFeasiblePlan->bMaximumPowerPlan = true;
	BestFeasiblePlan->bMaximumPowerSearchCapped = bSearchCapped;
	if (LimitingPlan.IsValid())
	{
		BestFeasiblePlan->MaximumPowerLimitingResourceClassPath =
			LimitingPlan->LimitingResourceClassPath;
		BestFeasiblePlan->MaximumPowerLimitingResourceDisplayName =
			LimitingPlan->LimitingResourceDisplayName;
		BestFeasiblePlan->MaximumPowerLimitingResourceCapacityPerMinute =
			LimitingPlan->LimitingResourceCapacityPerMinute;
	}
	// Persist only the player's manual limits. Live save counts are refreshed on
	// every open and must never become stale plan data.
	BestFeasiblePlan->ResourceSourceMixes = ResourceSourceMixes;

	const TSharedPtr<FSFPPlanResult> PreviousPlan = CurrentPlan;
	CarryForwardNodeCompletion(bCarryCurrentPlanProgress ? PreviousPlan.Get() : nullptr, *BestFeasiblePlan);
	CurrentPlan = BestFeasiblePlan;
	bCarryCurrentPlanProgress = true;
	bSharedPlanContentDirty = bSharedPlanMode && !ActiveSharedPlanFileName.IsEmpty();
	bUseCurrentFactoryPowerDemand = false;
	PowerTargetNetMW = BestFeasiblePlan->RequestedNetPowerMW;
	SelectedTargets.Reset();
	SelectedProduct.Reset();
	if (TargetList.IsValid()) TargetList->RequestListRefresh();
	if (ProductList.IsValid()) ProductList->ClearSelection();
	if (GraphPanel.IsValid()) GraphPanel->SetPlan(BestFeasiblePlan);
	RefreshRecipeChoices(BestFeasiblePlan);
	RefreshInputBudgets(BestFeasiblePlan, true);
	CaptureInputBudgetsToPlan();

	const double SolveMilliseconds = (FPlatformTime::Seconds() - SolveStartTime) * 1000.0;
	RecordMaximumPowerCalculationDuration();
	if (bSearchCapped)
	{
		StatusText = FString::Printf(
			TEXT("Mindestens %s MW netto sind mit den gewählten Quellen möglich; die Suchgrenze wurde erreicht. Berechnung: %s ms"),
			*FSFPNumberFormatting::Decimal(BestFeasiblePlan->RequestedNetPowerMW, 2),
			*FSFPNumberFormatting::Decimal(SolveMilliseconds, 2));
	}
	else
	{
		StatusText = FString::Printf(
			TEXT("Maximal mögliche Nettoleistung aus den gewählten Weltressourcen: %s MW. Engpass: %s. Berechnung: %s ms"),
			*FSFPNumberFormatting::Decimal(BestFeasiblePlan->RequestedNetPowerMW, 2),
			BestFeasiblePlan->MaximumPowerLimitingResourceDisplayName.IsEmpty()
				? TEXT("Rohstoffkapazität")
				: *CurrentPlannerItemName(
					BestFeasiblePlan->MaximumPowerLimitingResourceDisplayName,
					BestFeasiblePlan->MaximumPowerLimitingResourceClassPath),
			*FSFPNumberFormatting::Decimal(SolveMilliseconds, 2));
	}
	PersistCurrentPlan(false);
	return FReply::Handled();
}

void SSFPPlannerWindow::CalculateForTargets(
	const double RateScale,
	const bool bPreserveInputBudgets,
	const FString& StatusPrefix)
{
	if (!Solver.IsValid() || SelectedTargets.IsEmpty()
		|| !FMath::IsFinite(RateScale) || RateScale <= 0.0)
	{
		StatusText = TEXT("Für die Berechnung fehlt mindestens ein gültiges Endprodukt");
		return;
	}
	if (!SelectedConveyor.IsValid() || !SelectedConveyorLift.IsValid())
	{
		StatusText = TEXT("Förderband und Förderlift müssen für die Produktionsstätte ausgewählt sein");
		return;
	}

	TArray<FSFPPlanTarget> Targets;
	for (const TSharedPtr<FSFPSelectedTarget>& SelectedTarget : SelectedTargets)
	{
		if (!SelectedTarget.IsValid() || !SelectedTarget->Product.IsValid())
		{
			continue;
		}
		FSFPPlanTarget Target;
		Target.ItemClass = SelectedTarget->Product->ItemClass;
		Target.ItemClassPath = SelectedTarget->Product->ClassPath;
		Target.DisplayName = SelectedTarget->Product->DisplayName;
		Target.Form = SelectedTarget->Product->Form;
		Target.RatePerMinute = SelectedTarget->RatePerMinute * RateScale;
		Targets.Add(MoveTemp(Target));
	}
	const double SolveStartTime = FPlatformTime::Seconds();
	const TSharedPtr<FSFPPlanResult> PreviousPlan = CurrentPlan;
	TSharedPtr<FSFPPlanResult> Plan = bInputPlanning ? SolveAvailableInputs(Targets) : MakeShared<FSFPPlanResult>(Solver->Solve(
		Targets,
		bOnlyAvailable,
		RecipeOverrides,
		EstimatedConnectionLengthMeters,
		SelectedConveyor.IsValid() ? SelectedConveyor->Tier.ClassPath : FString(),
		SelectedConveyorLift.IsValid() ? SelectedConveyorLift->Tier.ClassPath : FString(),
		TMap<FString, double>(),
		true,
		MachineSettingsOverrides,
		BuildEffectiveResourceSourceMixes()));
	const double SolveMilliseconds = (FPlatformTime::Seconds() - SolveStartTime) * 1000.0;
	if (!Plan->bSuccess)
	{
		StatusText = FString::Printf(TEXT("Berechnung fehlgeschlagen: %s"), *Plan->ErrorMessage);
		return;
	}
	Plan->ResourceSourceMixes = ResourceSourceMixes;
	CarryForwardNodeCompletion(bCarryCurrentPlanProgress ? PreviousPlan.Get() : nullptr, *Plan);

	for (const FSFPPlanTarget& SolvedTarget : Plan->Targets)
	{
		const TSharedPtr<FSFPSelectedTarget>* TargetRow = SelectedTargets.FindByPredicate(
			[&SolvedTarget](const TSharedPtr<FSFPSelectedTarget>& Candidate)
			{
				return Candidate.IsValid()
					&& Candidate->Product.IsValid()
					&& Candidate->Product->ClassPath == SolvedTarget.ItemClassPath;
			});
		if (TargetRow != nullptr && TargetRow->IsValid())
		{
			(*TargetRow)->RatePerMinute = SolvedTarget.RatePerMinute;
		}
	}
	if (TargetList.IsValid())
	{
		TargetList->RequestListRefresh();
	}
	CaptureInputPlanning(*Plan);
	if (GraphPanel.IsValid())
	{
		GraphPanel->SetPlan(Plan);
	}
	if (MachineGuidanceState == 1) MachineGuidanceState = 2;
	CurrentPlan = Plan;
	LastFactoryPowerDemandMW = FMath::Max(0.0, Plan->TotalBasePowerMW);
	bCarryCurrentPlanProgress = true;
	bSharedPlanContentDirty = bSharedPlanMode && !ActiveSharedPlanFileName.IsEmpty();
	RefreshRecipeChoices(Plan);
	RefreshInputBudgets(Plan, bPreserveInputBudgets);
	CaptureInputBudgetsToPlan();
	StatusText = BuildPlanStatusText(*Plan, SolveMilliseconds, StatusPrefix);
	PersistCurrentPlan(false);
}

void SSFPPlannerWindow::RefreshRecipeChoices(const TSharedPtr<FSFPPlanResult>& Plan)
{
	RecipeChoiceRows.Reset();
	if (Solver.IsValid() && Plan.IsValid())
	{
		TSet<int32> RelevantNodes;
		TSet<FString> EndProductPaths;
		for (const FSFPPlanTarget& Target : Plan->Targets)
		{
			EndProductPaths.Add(Target.ItemClassPath);
		}
		TArray<int32> PendingNodes;
		TMap<int32, TArray<int32>> Suppliers;
		TMap<int32, TArray<int32>> Consumers;
		for (const FSFPPlanEdge& Edge : Plan->Edges)
		{
			if (Edge.RatePerMinute > KINDA_SMALL_NUMBER)
			{
				Suppliers.FindOrAdd(Edge.TargetNodeId).Add(Edge.SourceNodeId);
				Consumers.FindOrAdd(Edge.SourceNodeId).Add(Edge.TargetNodeId);
			}
		}
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			if (Node.Type == ESFPPlanNodeType::Target)
			{
				RelevantNodes.Add(Node.Id);
				PendingNodes.Add(Node.Id);
			}
		}
		for (int32 Index = 0; Index < PendingNodes.Num(); ++Index)
		{
			if (const TArray<int32>* Inputs = Suppliers.Find(PendingNodes[Index]))
			{
				for (const int32 Supplier : *Inputs)
				{
					if (!RelevantNodes.Contains(Supplier))
					{
						RelevantNodes.Add(Supplier);
						PendingNodes.Add(Supplier);
					}
				}
			}
		}
		TSet<FString> SeenItems;
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			if (!RelevantNodes.Contains(Node.Id)
				|| Node.Type != ESFPPlanNodeType::Machine
				|| Node.ProducedItemClassPath.IsEmpty()
				|| SeenItems.Contains(Node.ProducedItemClassPath))
			{
				continue;
			}
			SeenItems.Add(Node.ProducedItemClassPath);
			TSharedPtr<FSFPRecipeChoiceRow> Row = MakeShared<FSFPRecipeChoiceRow>();
			Row->PlanNodeId = Node.Id;
			Row->ItemClassPath = Node.ProducedItemClassPath;
			Solver->GetRecipeOptionsForItemPath(Row->ItemClassPath, bOnlyAvailable, Row->Options);
            if (bInputPlanning) Row->Options.RemoveAll([this](const TSharedPtr<FSFPRecipeOption>& O) {
                return O.IsValid() && SelectedSupplies.ContainsByPredicate([&](const TSharedPtr<FSFPPlanSupply>& S) { return S->ItemClassPath == O->SourceItemClassPath; });
            });
			if (Row->Options.IsEmpty()
				|| (Row->Options.Num() == 1 && !EndProductPaths.Contains(Row->ItemClassPath)
					&& Row->Options[0]->Category != TEXT("Direktabbau / Förderung")))
			{
				continue;
			}
			for (const FSFPPlanEdge& Edge : Plan->Edges)
			{
				if (Edge.SourceNodeId == Node.Id && Edge.ItemClassPath == Row->ItemClassPath)
				{
					Row->ItemName = Edge.ItemName;
					break;
				}
			}
			if (Row->ItemName.IsEmpty())
			{
				Row->ItemName = FPaths::GetBaseFilename(Row->ItemClassPath);
			}
			const FString SelectedPath = Node.RecipeClassPath;
			const TSharedPtr<FSFPRecipeOption>* Match = Row->Options.FindByPredicate(
				[&SelectedPath](const TSharedPtr<FSFPRecipeOption>& Option)
				{
					return Option.IsValid() && Option->RecipeClassPath == SelectedPath;
				});
			// An older saved plan can still use a now-excluded secondary route.
			// Never pretend that the first dropdown option was used by that graph.
			if (Match == nullptr)
			{
				TSharedPtr<FSFPRecipeOption> Previous = MakeShared<FSFPRecipeOption>();
				Previous->ItemClassPath = Row->ItemClassPath;
				Previous->RecipeClassPath = Node.RecipeClassPath;
				Previous->DisplayName = SFPLocalization::Translate(TEXT("Plan neu berechnen"));
				Row->Options.Add(Previous);
				Row->Selected = Previous;
			}
			else
			{
				Row->Selected = *Match;
			}
			RecipeChoiceRows.Add(MoveTemp(Row));
		}

		// A separately processed material and its one-use raw extraction are one
		// user decision. Keep both solver overrides independent, but render the raw
		// miner controls inside the immediate processing card instead of presenting
		// two near-duplicate cards such as Crushed Siderite and Siderite Ore.
		TMap<int32, TSharedPtr<FSFPRecipeChoiceRow>> RowsByNodeId;
		for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : RecipeChoiceRows)
		{
			if (Row.IsValid()) RowsByNodeId.Add(Row->PlanNodeId, Row);
		}
		TSet<int32> GroupedRawNodeIds;
		for (const TSharedPtr<FSFPRecipeChoiceRow>& ProcessingRow : RecipeChoiceRows)
		{
			if (!ProcessingRow.IsValid() || !ProcessingRow->Selected.IsValid()
				|| ProcessingRow->Selected->Category == TEXT("Direktabbau / Förderung"))
			{
				continue;
			}
			const TArray<int32>* DirectSuppliers = Suppliers.Find(ProcessingRow->PlanNodeId);
			if (DirectSuppliers == nullptr) continue;
			for (const int32 SupplierNodeId : *DirectSuppliers)
			{
				const TSharedPtr<FSFPRecipeChoiceRow>* RawRow = RowsByNodeId.Find(SupplierNodeId);
				const TArray<int32>* RawConsumers = Consumers.Find(SupplierNodeId);
				if (RawRow == nullptr || !RawRow->IsValid() || !(*RawRow)->Selected.IsValid()
					|| (*RawRow)->Selected->Category != TEXT("Direktabbau / Förderung")
					|| (*RawRow)->Selected->bProcessesResource
					|| RawConsumers == nullptr || RawConsumers->Num() != 1
					|| GroupedRawNodeIds.Contains(SupplierNodeId))
				{
					continue;
				}
				ProcessingRow->GroupedRawExtractions.Add(*RawRow);
				GroupedRawNodeIds.Add(SupplierNodeId);
			}
			ProcessingRow->GroupedRawExtractions.Sort([](
				const TSharedPtr<FSFPRecipeChoiceRow>& Left,
				const TSharedPtr<FSFPRecipeChoiceRow>& Right)
			{
				return Left.IsValid() && Right.IsValid()
					? Left->ItemName.Compare(Right->ItemName, ESearchCase::IgnoreCase) < 0
					: Left.IsValid();
			});
		}
		RecipeChoiceRows.RemoveAll([&GroupedRawNodeIds](const TSharedPtr<FSFPRecipeChoiceRow>& Row)
		{
			return Row.IsValid() && GroupedRawNodeIds.Contains(Row->PlanNodeId);
		});
		RecipeChoiceRows.Sort([&EndProductPaths](const TSharedPtr<FSFPRecipeChoiceRow>& Left, const TSharedPtr<FSFPRecipeChoiceRow>& Right)
		{
			if (Left.IsValid() && Right.IsValid()
				&& EndProductPaths.Contains(Left->ItemClassPath) != EndProductPaths.Contains(Right->ItemClassPath))
			{
				return EndProductPaths.Contains(Left->ItemClassPath);
			}
			return Left.IsValid() && Right.IsValid()
				? Left->ItemName.Compare(Right->ItemName, ESearchCase::IgnoreCase) < 0
				: Left.IsValid();
		});
	}
	if (RecipeChoiceList.IsValid())
	{
		RecipeChoiceList->RequestListRefresh();
	}
	RefreshMachineSettings();
}

void SSFPPlannerWindow::RefreshMachineSettings()
{
	if (!MachineSettingsBox.IsValid())
	{
		return;
	}
	MachineSettingsBox->ClearChildren();
	TArray<TSharedPtr<FSFPRecipeChoiceRow>> AllRecipeRows = RecipeChoiceRows;
	for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : RecipeChoiceRows)
	{
		if (Row.IsValid()) AllRecipeRows.Append(Row->GroupedRawExtractions);
	}

	// Keep the existing machine-variant selector. Rows are grouped only when the
	// same recipe can run in more than one compatible machine variant.
	TMap<FString, TArray<TSharedPtr<FSFPRecipeChoiceRow>>> Groups;
	TMap<FString, TArray<FString>> Names;
	for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : AllRecipeRows)
	{
		if (!Row.IsValid() || !Row->Selected.IsValid()
			|| Row->Selected->Category == TEXT("Direktabbau / Förderung"))
		{
			continue;
		}
		TArray<FString> Machines;
		for (const TSharedPtr<FSFPRecipeOption>& Option : Row->Options)
		{
			if (Option.IsValid()
				&& Option->DisplayName == Row->Selected->DisplayName
				&& Option->Category == Row->Selected->Category
				&& !Option->MachineName.IsEmpty())
			{
				Machines.AddUnique(Option->MachineName);
			}
		}
		if (Machines.Num() < 2)
		{
			continue;
		}
		Machines.Sort();
		const FString Key = FString::Join(Machines, TEXT("|"));
		Groups.FindOrAdd(Key).Add(Row);
		Names.Add(Key, Machines);
	}

	TArray<FString> GroupKeys;
	Groups.GetKeys(GroupKeys);
	GroupKeys.Sort();
	for (const FString& Key : GroupKeys)
	{
		const TArray<TSharedPtr<FSFPRecipeChoiceRow>> Rows = Groups[Key];
		auto Values = MakeShared<TArray<TSharedPtr<FString>>>();
		for (const FString& Name : Names[Key])
		{
			Values->Add(MakeShared<FString>(Name));
		}
		MachineSettingsBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(SFPLocalization::Text(TEXT("Maschinenvariante")))
		];
		MachineSettingsBox->AddSlot().AutoHeight().Padding(0, 0, 0, 7)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&Values.Get())
			.MaxListHeight(260)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Value)
			{
				return StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(Value.IsValid()
					? SFPLocalization::Text(*Value)
					: FText::GetEmpty()));
			})
			.OnSelectionChanged_Lambda([this, Rows, Values](TSharedPtr<FString> Value, ESelectInfo::Type Info)
			{
				if (Info == ESelectInfo::Direct || !Value.IsValid())
				{
					return;
				}
				for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : Rows)
				{
					if (!Row.IsValid() || !Row->Selected.IsValid()) continue;
					for (const TSharedPtr<FSFPRecipeOption>& Option : Row->Options)
					{
						if (Option.IsValid()
							&& Option->MachineName == *Value
							&& Option->DisplayName == Row->Selected->DisplayName
							&& Option->Category == Row->Selected->Category)
						{
							Row->Selected = Option;
							RecipeOverrides.Add(Row->ItemClassPath, Option->RecipeClassPath);
							break;
						}
					}
				}
				MachineGuidanceState = 1;
				bCarryCurrentPlanProgress = false;
				StatusText = TEXT("Maschinenauswahl geändert – bitte neu berechnen.");
				if (RecipeChoiceList.IsValid()) RecipeChoiceList->RebuildList();
				RefreshMachineSettings();
			})
			[
				SNew(STextBlock).Text_Lambda([Rows]()
				{
					if (Rows.IsEmpty() || !Rows[0].IsValid() || !Rows[0]->Selected.IsValid()) return FText::GetEmpty();
					const FString Machine = Rows[0]->Selected->MachineName;
					for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : Rows)
					{
						if (Row.IsValid() && Row->Selected.IsValid() && Row->Selected->MachineName != Machine)
						{
							return SFPLocalization::Text(TEXT("Unterschiedliche Maschinen"));
						}
					}
					return SFPLocalization::Text(Machine);
				})
			]
		];
	}

	// One operating-point editor per selected runtime recipe variant. This is
	// generic for vanilla, Satisfactory Plus and other manufacturer subclasses.
	TArray<TSharedPtr<FSFPRecipeOption>> OperatingOptions;
	TSet<FString> SeenRecipePaths;
	for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : AllRecipeRows)
	{
		if (!Row.IsValid() || !Row->Selected.IsValid()) continue;
		const TSharedPtr<FSFPRecipeOption>& Option = Row->Selected;
		if (Option->RecipeClassPath.IsEmpty() || Option->MachineName.IsEmpty()
			|| SeenRecipePaths.Contains(Option->RecipeClassPath))
		{
			continue;
		}
		const bool bHasOperatingControls = Option->MachineConfig.bCanChangePotential
			|| Option->MachineConfig.bCanChangeProductionBoost
			|| Option->bFuelPowered;
		if (!bHasOperatingControls)
		{
			continue;
		}
		SeenRecipePaths.Add(Option->RecipeClassPath);
		OperatingOptions.Add(Option);
	}
	OperatingOptions.Sort([](const TSharedPtr<FSFPRecipeOption>& Left, const TSharedPtr<FSFPRecipeOption>& Right)
	{
		if (!Left.IsValid()) return false;
		if (!Right.IsValid()) return true;
		const int32 MachineOrder = Left->MachineName.Compare(Right->MachineName, ESearchCase::IgnoreCase);
		return MachineOrder == 0
			? Left->DisplayName.Compare(Right->DisplayName, ESearchCase::IgnoreCase) < 0
			: MachineOrder < 0;
	});

	if (OperatingOptions.IsEmpty() && Groups.IsEmpty())
	{
		MachineSettingsBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(SFPLocalization::Text(
				TEXT("Nach der Berechnung erscheinen hier Maschinenvarianten, Taktung und Produktionsverstärkung des Plans.")))
		];
		return;
	}

	for (const TSharedPtr<FSFPRecipeOption>& Option : OperatingOptions)
	{
		if (!Option.IsValid()) continue;
		const FString SettingsKey = Option->RecipeClassPath;
		FSFPMachinePlanSettings& Settings = MachineSettingsOverrides.FindOrAdd(SettingsKey);
		if (!FMath::IsFinite(Settings.ClockPercent) || Settings.ClockPercent <= 0.0)
		{
			Settings.ClockPercent = 100.0;
		}

		MachineSettingsBox->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *Option->MachineName, *Option->DisplayName)))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
		];

		if (Option->MachineConfig.bCanChangePotential)
		{
			const double MinClockPercent = FMath::Max(0.1, Option->MachineConfig.MinPotential * 100.0);
			const TOptional<double> MaxClockPercent = Option->MachineConfig.bRuntimeMaxPotentialKnown
				? TOptional<double>(Option->MachineConfig.MaxPotential * 100.0)
				: TOptional<double>();
			MachineSettingsBox->AddSlot().AutoHeight().Padding(0, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.48f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Takt (%)")))
					.ToolTipText(SFPLocalization::Text(TEXT("Der Solver füllt Maschinen bis zu diesem Takt und untertaktet nur die letzte Maschine passend zum Bedarf.")))
				]
				+ SHorizontalBox::Slot().FillWidth(0.52f)
				[
					SNew(SNumericEntryBox<double>)
					.AllowSpin(true)
					.MinValue(MinClockPercent)
					.MaxValue(MaxClockPercent)
					.MinSliderValue(MinClockPercent)
					.MaxSliderValue(MaxClockPercent)
					.Value_Lambda([this, SettingsKey]() -> TOptional<double>
					{
						const FSFPMachinePlanSettings* Current = MachineSettingsOverrides.Find(SettingsKey);
						return Current != nullptr ? TOptional<double>(Current->ClockPercent) : TOptional<double>(100.0);
					})
					.OnValueChanged_Lambda([this, SettingsKey, MinClockPercent, MaxClockPercent](const double NewValue)
					{
						if (!FMath::IsFinite(NewValue)) return;
						FSFPMachinePlanSettings& Current = MachineSettingsOverrides.FindOrAdd(SettingsKey);
						Current.ClockPercent = FMath::Max(MinClockPercent, NewValue);
						if (MaxClockPercent.IsSet()) Current.ClockPercent = FMath::Min(Current.ClockPercent, MaxClockPercent.GetValue());
						MachineGuidanceState = 1;
						bCarryCurrentPlanProgress = false;
						StatusText = TEXT("Taktung geändert – bitte Produktionsplan neu berechnen.");
					})
				]
			];
		}

		if (Option->MachineConfig.bCanChangeProductionBoost
			&& Option->MachineConfig.ProductionBoostPerSloop > KINDA_SMALL_NUMBER)
		{
			const int32 MaxSloops = Option->MachineConfig.bRuntimeMaxProductionBoostKnown
				? FMath::Max(0, Option->MachineConfig.MaxSomersloops)
				: 64;
			MachineSettingsBox->AddSlot().AutoHeight().Padding(0, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.48f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(SFPLocalization::Text(TEXT("Sloops / Maschine")))
				]
				+ SHorizontalBox::Slot().FillWidth(0.52f)
				[
					SNew(SNumericEntryBox<int32>)
					.AllowSpin(true)
					.MinValue(0)
					.MaxValue(MaxSloops)
					.MinSliderValue(0)
					.MaxSliderValue(MaxSloops)
					.Value_Lambda([this, SettingsKey]() -> TOptional<int32>
					{
						const FSFPMachinePlanSettings* Current = MachineSettingsOverrides.Find(SettingsKey);
						return Current != nullptr ? TOptional<int32>(Current->SomersloopCount) : TOptional<int32>(0);
					})
					.OnValueChanged_Lambda([this, SettingsKey, MaxSloops](const int32 NewValue)
					{
						FSFPMachinePlanSettings& Current = MachineSettingsOverrides.FindOrAdd(SettingsKey);
						Current.SomersloopCount = FMath::Clamp(NewValue, 0, MaxSloops);
						MachineGuidanceState = 1;
						bCarryCurrentPlanProgress = false;
						StatusText = TEXT("Somersloop-Einstellung geändert – bitte Produktionsplan neu berechnen.");
					})
				]
			];

			MachineSettingsBox->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
			[
				SNew(STextBlock)
				.ColorAndOpacity(SFPTheme::MutedText)
				.Text_Lambda([this, SettingsKey, Option]()
				{
					const FSFPMachinePlanSettings* Current = MachineSettingsOverrides.Find(SettingsKey);
					const int32 Count = Current != nullptr ? Current->SomersloopCount : 0;
					double Boost = Option->MachineConfig.BaseProductionBoost
						+ static_cast<double>(Count) * Option->MachineConfig.ProductionBoostPerSloop;
					if (Option->MachineConfig.bRuntimeMaxProductionBoostKnown)
					{
						Boost = FMath::Min(Boost, Option->MachineConfig.MaxProductionBoost);
					}
					return SFPLocalization::Text(FString::Printf(
						TEXT("Boost: x%s · Power-Exponent: %s"),
						*FSFPNumberFormatting::Decimal(Boost, 2),
						*FSFPNumberFormatting::Decimal(Option->MachineConfig.ProductionBoostPowerExponent, 3)));
				})
			];
		}

		if (Option->bFuelPowered && !Option->FuelOptions.IsEmpty())
		{
			auto Fuels = MakeShared<TArray<TSharedPtr<FSFPPlannerFuelOption>>>();
			TSharedPtr<FSFPPlannerFuelOption> AutomaticFuel = MakeShared<FSFPPlannerFuelOption>();
			AutomaticFuel->DisplayName = TEXT("Automatisch");
			Fuels->Add(AutomaticFuel);
			for (const FSFPPlannerFuelOption& Fuel : Option->FuelOptions)
			{
				Fuels->Add(MakeShared<FSFPPlannerFuelOption>(Fuel));
			}
			MachineSettingsBox->AddSlot().AutoHeight().Padding(0, 1, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(0.48f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(SFPLocalization::Text(TEXT("Brennstoff")))
				]
				+ SHorizontalBox::Slot().FillWidth(0.52f)
				[
					SNew(SComboBox<TSharedPtr<FSFPPlannerFuelOption>>)
					.OptionsSource(&Fuels.Get())
					.MaxListHeight(300.0f)
					.OnGenerateWidget_Lambda([](TSharedPtr<FSFPPlannerFuelOption> Fuel)
					{
						if (!Fuel.IsValid()) return StaticCastSharedRef<SWidget>(SNew(STextBlock));
						const FString Label = Fuel->ClassPath.IsEmpty()
							? Fuel->DisplayName
							: FString::Printf(TEXT("%s · %s MJ"), *Fuel->DisplayName, *FSFPNumberFormatting::Decimal(Fuel->EnergyValueMJ, 1));
						return StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(Label)));
					})
					.OnSelectionChanged_Lambda([this, SettingsKey, Fuels](TSharedPtr<FSFPPlannerFuelOption> Fuel, ESelectInfo::Type Info)
					{
						if (Info == ESelectInfo::Direct || !Fuel.IsValid()) return;
						MachineSettingsOverrides.FindOrAdd(SettingsKey).FuelClassPath = Fuel->ClassPath;
						MachineGuidanceState = 1;
						bCarryCurrentPlanProgress = false;
						StatusText = TEXT("Brennstoff geändert – bitte Produktionsplan neu berechnen.");
					})
					[
						SNew(STextBlock).Text_Lambda([this, SettingsKey, Fuels]()
						{
							const FSFPMachinePlanSettings* Current = MachineSettingsOverrides.Find(SettingsKey);
							const FString SelectedPath = Current != nullptr ? Current->FuelClassPath : FString();
							for (const TSharedPtr<FSFPPlannerFuelOption>& Fuel : *Fuels)
							{
								if (Fuel.IsValid() && Fuel->ClassPath == SelectedPath)
								{
									return SFPLocalization::Text(Fuel->DisplayName);
								}
							}
							return SFPLocalization::Text(TEXT("Automatisch"));
						})
					]
				]
			];
		}
	}
}

TSharedRef<ITableRow> SSFPPlannerWindow::HandleGenerateRecipeChoiceRow(
	TSharedPtr<FSFPRecipeChoiceRow> Row,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	if (!Row.IsValid()) return SNew(STableRow<TSharedPtr<FSFPRecipeChoiceRow>>, OwnerTable)[Body];
	auto AppendMixedSourceControls = [this](
		const TSharedRef<SVerticalBox>& Container,
		const TSharedPtr<FSFPRecipeChoiceRow>& ExtractionRow)
	{
		if (!ExtractionRow.IsValid() || !ExtractionRow->Selected.IsValid()
			|| ExtractionRow->Selected->Category != TEXT("Direktabbau / Förderung"))
		{
			return;
		}
		TSet<FString> AvailablePurities;
		for (const TSharedPtr<FSFPRecipeOption>& Option : ExtractionRow->Options)
		{
			if (Option.IsValid()
				&& Option->Category == TEXT("Direktabbau / Förderung")
				&& Option->SourceName == ExtractionRow->Selected->SourceName
				&& Option->MachineClassPath == ExtractionRow->Selected->MachineClassPath
				&& Option->ModulesLabel == ExtractionRow->Selected->ModulesLabel
				&& Option->FluidLabel == ExtractionRow->Selected->FluidLabel
				&& (Option->bAvailable || !bOnlyAvailable))
			{
				AvailablePurities.Add(Option->Purity);
			}
		}
		if (!AvailablePurities.Contains(TEXT("Unrein"))
			|| !AvailablePurities.Contains(TEXT("Normal"))
			|| !AvailablePurities.Contains(TEXT("Rein")))
		{
			return;
		}

		const FString MixKey = ExtractionRow->Selected->SourceItemClassPath.IsEmpty()
			? ExtractionRow->ItemClassPath
			: ExtractionRow->Selected->SourceItemClassPath;
		const FSFPResourceSourceMix* ExistingMix = ResourceSourceMixes.Find(MixKey);
		const bool bMixed = ExistingMix == nullptr || ExistingMix->bEnabled;
		auto RequiredCountForPurity = [this, ExtractionRow](const FString& Purity)
		{
			if (!CurrentPlan.IsValid()) return 0;
			TSet<FString> RecipePaths;
			for (const TSharedPtr<FSFPRecipeOption>& Option : ExtractionRow->Options)
			{
				if (Option.IsValid()
					&& Option->Category == TEXT("Direktabbau / Förderung")
					&& Option->Purity == Purity
					&& Option->SourceName == ExtractionRow->Selected->SourceName
					&& Option->MachineClassPath == ExtractionRow->Selected->MachineClassPath
					&& Option->ModulesLabel == ExtractionRow->Selected->ModulesLabel
					&& Option->FluidLabel == ExtractionRow->Selected->FluidLabel)
				{
					RecipePaths.Add(Option->RecipeClassPath);
				}
			}
			int32 RequiredCount = 0;
			for (const FSFPPlanNode& Node : CurrentPlan->Nodes)
			{
				if (Node.Type == ESFPPlanNodeType::Machine && RecipePaths.Contains(Node.RecipeClassPath))
				{
					RequiredCount += FMath::Max(0, Node.BuiltMachineCount);
				}
			}
			return RequiredCount;
		};
		Container->AddSlot().AutoHeight().Padding(3, 6, 3, 2)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, MixKey]()
			{
				const FSFPResourceSourceMix* Mix = ResourceSourceMixes.Find(MixKey);
				return (Mix == nullptr || Mix->bEnabled)
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, MixKey](const ECheckBoxState State)
			{
				FSFPResourceSourceMix& Mix = ResourceSourceMixes.FindOrAdd(MixKey);
				Mix.bEnabled = State == ECheckBoxState::Checked;
				StatusText = TEXT("Vorkommensmix geändert – bitte Produktionsplan neu berechnen.");
				bCarryCurrentPlanProgress = false;
				if (RecipeChoiceList.IsValid()) RecipeChoiceList->RebuildList();
			})
			[
				SNew(STextBlock).Text(SFPLocalization::Text(TEXT("Reinheiten automatisch verteilen")))
			]
		];
		if (!bMixed)
		{
			return;
		}
		Container->AddSlot().AutoHeight().Padding(3, 1, 3, 3)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(SFPLocalization::Text(TEXT("Der Planner zeigt den benötigten Node-Mix. Begrenze nur Reinheiten, die dir nicht ausreichend zur Verfügung stehen; 0 schließt diese Reinheit aus.")))
		];
		const FSFPResourceNodeAvailability* LiveAvailability = ResourceNodeAvailability.Find(MixKey);
		if (LiveAvailability != nullptr)
		{
			Container->AddSlot().AutoHeight().Padding(3, 1, 3, 4)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, MixKey]()
				{
					const FSFPResourceSourceMix* Mix = ResourceSourceMixes.Find(MixKey);
					return Mix != nullptr && Mix->bUseOccupiedSources
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, MixKey](const ECheckBoxState State)
				{
					FSFPResourceSourceMix& Mix = ResourceSourceMixes.FindOrAdd(MixKey);
					Mix.bEnabled = true;
					Mix.bUseOccupiedSources = State == ECheckBoxState::Checked;
					bCarryCurrentPlanProgress = false;
					StatusText = TEXT("Verfügbare Rohstoffquellen geändert – bitte neu berechnen.");
					if (RecipeChoiceList.IsValid()) RecipeChoiceList->RebuildList();
				})
				[
					SNew(STextBlock).Text(SFPLocalization::Text(TEXT("Bereits belegte Quellen mit einbeziehen")))
				]
			];
		}
		else if (!ResourceNodeInventoryError.IsEmpty())
		{
			Container->AddSlot().AutoHeight().Padding(3, 1, 3, 4)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(SFPTheme::Orange)
				.Text(FText::FromString(
					SFPLocalization::Text(TEXT("Weltinventur nicht verfügbar")).ToString()
					+ TEXT(": ") + ResourceNodeInventoryError))
			];
		}
		TSharedRef<SHorizontalBox> Counts = SNew(SHorizontalBox);
		auto AddCount = [this, Counts, MixKey, RequiredCountForPurity](
			const FString& Label,
			int32 FSFPResourceSourceMix::* CountMember,
			bool FSFPResourceSourceMix::* LimitedMember)
		{
			const int32 RequiredCount = RequiredCountForPurity(Label);
			const FSFPResourceNodeAvailability* Availability = ResourceNodeAvailability.Find(MixKey);
			const FSFPResourceSourceMix* CurrentMix = ResourceSourceMixes.Find(MixKey);
			const int32 TotalCount = Availability != nullptr ? Availability->TotalForPurity(Label) : 0;
			const int32 OccupiedCount = Availability != nullptr ? Availability->OccupiedForPurity(Label) : 0;
			const int32 FreeCount = Availability != nullptr ? Availability->FreeForPurity(Label) : 0;
			const bool bManualLimit = CurrentMix != nullptr && CurrentMix->*LimitedMember;
			const bool bIncludeOccupied = CurrentMix != nullptr && CurrentMix->bUseOccupiedSources;
			const int32 PlannedAvailable = bManualLimit
				? FMath::Max(0, CurrentMix->*CountMember)
				: (bIncludeOccupied ? TotalCount : FreeCount);
			const int32 MissingCount = Availability != nullptr
				? FMath::Max(0, RequiredCount - PlannedAvailable) : 0;
			Counts->AddSlot().FillWidth(1.0f).Padding(0, 0, 6, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(SFPLocalization::Text(Label))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 1)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Availability != nullptr
						? FString::Printf(
							TEXT("%s %d | %s %d | %s %d"),
							*SFPLocalization::Text(TEXT("Gesamt")).ToString(), TotalCount,
							*SFPLocalization::Text(TEXT("Besetzt")).ToString(), OccupiedCount,
							*SFPLocalization::Text(TEXT("Frei")).ToString(), FreeCount)
						: FString::Printf(
							TEXT("%s: %d"),
							*SFPLocalization::Text(TEXT("Benötigt")).ToString(), RequiredCount)))
					.ColorAndOpacity(SFPTheme::MutedText)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 1)
				[
					SNew(STextBlock)
					.Visibility(Availability != nullptr ? EVisibility::Visible : EVisibility::Collapsed)
					.Text(FText::FromString(FString::Printf(
						TEXT("%s %d | %s %d | %s %d"),
						*SFPLocalization::Text(TEXT("Benötigt")).ToString(), RequiredCount,
						*SFPLocalization::Text(TEXT("Verfügbar")).ToString(), PlannedAvailable,
						*SFPLocalization::Text(TEXT("Fehlt")).ToString(), MissingCount)))
					.ColorAndOpacity(SFPTheme::Cyan)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, MixKey, LimitedMember]()
					{
						const FSFPResourceSourceMix* Mix = ResourceSourceMixes.Find(MixKey);
						return Mix != nullptr && Mix->*LimitedMember
							? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, MixKey, CountMember, LimitedMember, RequiredCount](const ECheckBoxState State)
					{
						FSFPResourceSourceMix& Mix = ResourceSourceMixes.FindOrAdd(MixKey);
						Mix.bEnabled = true;
						Mix.*LimitedMember = State == ECheckBoxState::Checked;
						if (Mix.*LimitedMember)
						{
							Mix.*CountMember = RequiredCount;
						}
						StatusText = TEXT("Vorkommensmix geändert – bitte Produktionsplan neu berechnen.");
						bCarryCurrentPlanProgress = false;
					})
					[
						SNew(STextBlock).Text(SFPLocalization::Text(TEXT("Verfügbarkeit begrenzen")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SNumericEntryBox<int32>)
					.Visibility_Lambda([this, MixKey, LimitedMember]()
					{
						const FSFPResourceSourceMix* Mix = ResourceSourceMixes.Find(MixKey);
						return Mix != nullptr && Mix->*LimitedMember
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					.AllowSpin(true)
					.MinValue(0)
					.MaxValue(100000)
					.MinSliderValue(0)
					.MaxSliderValue(100)
					.Value_Lambda([this, MixKey, CountMember]() -> TOptional<int32>
					{
						const FSFPResourceSourceMix* Mix = ResourceSourceMixes.Find(MixKey);
						return Mix != nullptr ? TOptional<int32>(Mix->*CountMember) : TOptional<int32>(0);
					})
					.OnValueChanged_Lambda([this, MixKey, CountMember](const int32 NewValue)
					{
						FSFPResourceSourceMix& Mix = ResourceSourceMixes.FindOrAdd(MixKey);
						Mix.bEnabled = true;
						Mix.*CountMember = FMath::Clamp(NewValue, 0, 100000);
						StatusText = TEXT("Vorkommensmix geändert – bitte Produktionsplan neu berechnen.");
						bCarryCurrentPlanProgress = false;
					})
				]
			];
		};
		AddCount(TEXT("Rein"), &FSFPResourceSourceMix::PureCount, &FSFPResourceSourceMix::bPureLimited);
		AddCount(TEXT("Normal"), &FSFPResourceSourceMix::NormalCount, &FSFPResourceSourceMix::bNormalLimited);
		AddCount(TEXT("Unrein"), &FSFPResourceSourceMix::ImpureCount, &FSFPResourceSourceMix::bImpureLimited);
		Container->AddSlot().AutoHeight().Padding(3, 1, 3, 5)[Counts];
	};
	auto UsesAutomaticPurityMix = [this](const TSharedPtr<FSFPRecipeChoiceRow>& ExtractionRow)
	{
		if (!ExtractionRow.IsValid() || !ExtractionRow->Selected.IsValid()
			|| ExtractionRow->Selected->Category != TEXT("Direktabbau / Förderung"))
		{
			return false;
		}

		TSet<FString> AvailablePurities;
		for (const TSharedPtr<FSFPRecipeOption>& Option : ExtractionRow->Options)
		{
			if (Option.IsValid()
				&& Option->Category == TEXT("Direktabbau / Förderung")
				&& Option->SourceName == ExtractionRow->Selected->SourceName
				&& Option->MachineClassPath == ExtractionRow->Selected->MachineClassPath
				&& Option->ModulesLabel == ExtractionRow->Selected->ModulesLabel
				&& Option->FluidLabel == ExtractionRow->Selected->FluidLabel
				&& (Option->bAvailable || !bOnlyAvailable))
			{
				AvailablePurities.Add(Option->Purity);
			}
		}
		if (!AvailablePurities.Contains(TEXT("Unrein"))
			|| !AvailablePurities.Contains(TEXT("Normal"))
			|| !AvailablePurities.Contains(TEXT("Rein")))
		{
			return false;
		}

		const FString MixKey = ExtractionRow->Selected->SourceItemClassPath.IsEmpty()
			? ExtractionRow->ItemClassPath
			: ExtractionRow->Selected->SourceItemClassPath;
		const FSFPResourceSourceMix* Mix = ResourceSourceMixes.Find(MixKey);
		return Mix == nullptr || Mix->bEnabled;
	};
	Body->AddSlot().AutoHeight().Padding(3, 6)
	[
		SNew(STextBlock).Text(FText::FromString(
			SFPLocalization::Text(TEXT("Ausgabe")).ToString() + TEXT(": ")
			+ CurrentPlannerItemName(Row->ItemName, Row->ItemClassPath)))
	];
	Body->AddSlot().AutoHeight().Padding(3, 2)
	[
		SNew(STextBlock).AutoWrapText(true).Text_Lambda([this, Row]()
		{
			return SFPLocalization::Text(RecipeOverrides.Contains(Row->ItemClassPath)
				? TEXT("Eigene Auswahl – nach Änderungen neu berechnen")
				: TEXT("Automatische Annahme – Quelle, Miner und Reinheit mit deiner Fabrik abgleichen."));
		})
	];
	using FField = FString FSFPRecipeOption::*;
	TArray<FField> Fields;
	TArray<FString> Labels;
	Fields.Add(&FSFPRecipeOption::Category);
	Labels.Add(TEXT("Bezugsweg"));
	const bool bExtraction = Row->Selected.IsValid() && Row->Selected->Category == TEXT("Direktabbau / Förderung");
	if (bExtraction)
	{
		Body->AddSlot().AutoHeight().Padding(3, 2, 3, 6)
		[
			SNew(STextBlock).AutoWrapText(true).Text(SFPLocalization::Text(
				Row->Selected->bProcessesResource
				? TEXT("Verarbeitung im Miner: Das Zusatzmodul erzeugt die oben angegebene Ausgabe direkt aus dem Rohstoff. Für reinen Erzabbau mit separater Verarbeitung unter Bezugsweg ein Herstellungsrezept wählen und neu berechnen. Danach die Bohrköpfe beim benötigten Erz einstellen.")
				: TEXT("Reiner Rohstoffabbau: Der Miner liefert den Rohstoff unverarbeitet. Die Auswahl unten gilt für diese Ausgabe.")))
		];
		Fields.Add(&FSFPRecipeOption::SourceName);
		Labels.Add(TEXT("Rohstoff am Eingang"));
		Fields.Add(&FSFPRecipeOption::MachineName);
		Labels.Add(TEXT("Miner / Förderanlage"));
		if (!UsesAutomaticPurityMix(Row))
		{
			Fields.Add(&FSFPRecipeOption::Purity);
			Labels.Add(TEXT("Reinheit"));
		}
		Fields.Add(&FSFPRecipeOption::ModulesLabel);
		Labels.Add(TEXT("Module / Bohrkopf"));
		Fields.Add(&FSFPRecipeOption::FluidLabel);
		Labels.Add(TEXT("Betriebsflüssigkeit"));
	}
	else
	{
		Fields.Add(&FSFPRecipeOption::DisplayName);
		Labels.Add(TEXT("Rezept"));
	}
	for (int32 FieldIndex = 0; FieldIndex < Fields.Num(); ++FieldIndex)
	{
		const FField Field = Fields[FieldIndex];
		TSharedRef<TArray<TSharedPtr<FString>>> Values = MakeShared<TArray<TSharedPtr<FString>>>();
		for (const auto& Option : Row->Options)
		{
			if (!Option.IsValid()) continue;
			bool bMatches = true;
			for (int32 Previous = 0; Previous < FieldIndex && Row->Selected.IsValid(); ++Previous)
				if (Option.Get()->*Fields[Previous] != Row->Selected.Get()->*Fields[Previous]) bMatches = false;
			const FString Value = Option.Get()->*Field;
			if (bMatches && !Value.IsEmpty() && !Values->ContainsByPredicate([&](const TSharedPtr<FString>& V) { return *V == Value; }))
				Values->Add(MakeShared<FString>(Value));
		}
		if (Values->IsEmpty()) continue;
		TSharedPtr<FString> Initial;
		for (const auto& Value : *Values)
			if (Row->Selected.IsValid() && *Value == Row->Selected.Get()->*Field) Initial = Value;
		Body->AddSlot().AutoHeight().Padding(3, 1)
		[
			SNew(STextBlock).Text(SFPLocalization::Text(Labels[FieldIndex]))
		];
		Body->AddSlot().AutoHeight().Padding(3, 0, 3, 4)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&Values.Get()).InitiallySelectedItem(Initial).MaxListHeight(320.0f)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Value)
			{
				return StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(Value.IsValid() ? SFPLocalization::Text(*Value) : FText::GetEmpty()));
			})
			.OnSelectionChanged_Lambda([this, Row, Values, Fields, FieldIndex, Field](TSharedPtr<FString> Value, ESelectInfo::Type Info)
			{
				if (Info == ESelectInfo::Direct || !Value.IsValid() || Values->IsEmpty()) return;
				TSharedPtr<FSFPRecipeOption> Best;
				int32 BestScore = MIN_int32;
				for (const auto& Option : Row->Options)
				{
					if (!Option.IsValid() || Option.Get()->*Field != *Value) continue;
					bool bMatches = true;
					int32 Score = Option->bAvailable ? 100 : 0;
					for (int32 I = 0; I < Fields.Num() && Row->Selected.IsValid(); ++I)
					{
						const bool bEqual = Option.Get()->*Fields[I] == Row->Selected.Get()->*Fields[I];
						if (I < FieldIndex && !bEqual) bMatches = false;
						if (bEqual) ++Score;
					}
					if (bMatches && Score > BestScore) { Best = Option; BestScore = Score; }
				}
				if (!Best.IsValid()) return;
				Row->Selected = Best;
				RecipeOverrides.Add(Row->ItemClassPath, Best->RecipeClassPath);
				StatusText = TEXT("Bezugsweg geändert – jetzt Produktionsplan neu berechnen und speichern.");
				if (RecipeChoiceList.IsValid()) RecipeChoiceList->RebuildList();
				RefreshMachineSettings();
			})
			[
				SNew(STextBlock).Text_Lambda([Row, Field]()
				{
					return Row->Selected.IsValid() ? SFPLocalization::Text(Row->Selected.Get()->*Field) : FText::GetEmpty();
				})
			]
		];
	}
	Body->AddSlot().AutoHeight().Padding(3, 2)
	[
		SNew(STextBlock).AutoWrapText(true).Text_Lambda([Row]()
		{
			if (!Row->Selected.IsValid()) return FText::GetEmpty();
			const FString Locked = Row->Selected->bAvailable ? FString() : TEXT("Gesperrt – im aktuellen Spielstand nicht verfügbar. ");
			return SFPLocalization::Text(Locked + Row->Selected->ConfigurationDetail);
		})
	];
	if (bExtraction)
	{
		AppendMixedSourceControls(Body, Row);
	}
	for (const TSharedPtr<FSFPRecipeChoiceRow>& RawRow : Row->GroupedRawExtractions)
	{
		if (!RawRow.IsValid() || !RawRow->Selected.IsValid()) continue;
		Body->AddSlot().AutoHeight().Padding(3, 10, 3, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				SFPLocalization::Text(TEXT("Rohstoff & Miner")).ToString() + TEXT(": ")
				+ CurrentPlannerItemName(RawRow->ItemName, RawRow->ItemClassPath)))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
		];
		Body->AddSlot().AutoHeight().Padding(3, 2, 3, 6)
		[
			SNew(STextBlock).AutoWrapText(true).Text(SFPLocalization::Text(
				TEXT("Reiner Rohstoffabbau: Der Miner liefert den Rohstoff unverarbeitet. Die Auswahl unten gilt für diese Ausgabe.")))
		];
		TArray<FField> RawFields;
		TArray<FString> RawLabels;
		RawFields.Add(&FSFPRecipeOption::Category);
		RawLabels.Add(TEXT("Bezugsweg"));
		RawFields.Add(&FSFPRecipeOption::SourceName);
		RawLabels.Add(TEXT("Rohstoff am Eingang"));
		RawFields.Add(&FSFPRecipeOption::MachineName);
		RawLabels.Add(TEXT("Miner / Förderanlage"));
		if (!UsesAutomaticPurityMix(RawRow))
		{
			RawFields.Add(&FSFPRecipeOption::Purity);
			RawLabels.Add(TEXT("Reinheit"));
		}
		RawFields.Add(&FSFPRecipeOption::ModulesLabel);
		RawLabels.Add(TEXT("Module / Bohrkopf"));
		RawFields.Add(&FSFPRecipeOption::FluidLabel);
		RawLabels.Add(TEXT("Betriebsflüssigkeit"));
		for (int32 FieldIndex = 0; FieldIndex < RawFields.Num(); ++FieldIndex)
		{
			const FField Field = RawFields[FieldIndex];
			TSharedRef<TArray<TSharedPtr<FString>>> Values = MakeShared<TArray<TSharedPtr<FString>>>();
			for (const TSharedPtr<FSFPRecipeOption>& Option : RawRow->Options)
			{
				if (!Option.IsValid()) continue;
				bool bMatches = true;
				for (int32 Previous = 0; Previous < FieldIndex && RawRow->Selected.IsValid(); ++Previous)
				{
					if (Option.Get()->*RawFields[Previous] != RawRow->Selected.Get()->*RawFields[Previous]) bMatches = false;
				}
				const FString Value = Option.Get()->*Field;
				if (bMatches && !Value.IsEmpty()
					&& !Values->ContainsByPredicate([&](const TSharedPtr<FString>& Existing) { return *Existing == Value; }))
				{
					Values->Add(MakeShared<FString>(Value));
				}
			}
			if (Values->IsEmpty()) continue;
			TSharedPtr<FString> Initial;
			for (const TSharedPtr<FString>& Value : *Values)
			{
				if (RawRow->Selected.IsValid() && *Value == RawRow->Selected.Get()->*Field) Initial = Value;
			}
			Body->AddSlot().AutoHeight().Padding(3, 1)
			[
				SNew(STextBlock).Text(SFPLocalization::Text(RawLabels[FieldIndex]))
			];
			Body->AddSlot().AutoHeight().Padding(3, 0, 3, 4)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&Values.Get()).InitiallySelectedItem(Initial).MaxListHeight(320.0f)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> Value)
				{
					return StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(
						Value.IsValid() ? SFPLocalization::Text(*Value) : FText::GetEmpty()));
				})
				.OnSelectionChanged_Lambda([this, RawRow, Values, RawFields, FieldIndex, Field](
					TSharedPtr<FString> Value,
					ESelectInfo::Type Info)
				{
					if (Info == ESelectInfo::Direct || !Value.IsValid() || Values->IsEmpty()) return;
					TSharedPtr<FSFPRecipeOption> Best;
					int32 BestScore = MIN_int32;
					for (const TSharedPtr<FSFPRecipeOption>& Option : RawRow->Options)
					{
						if (!Option.IsValid() || Option.Get()->*Field != *Value) continue;
						bool bMatches = true;
						int32 Score = Option->bAvailable ? 100 : 0;
						for (int32 I = 0; I < RawFields.Num() && RawRow->Selected.IsValid(); ++I)
						{
							const bool bEqual = Option.Get()->*RawFields[I] == RawRow->Selected.Get()->*RawFields[I];
							if (I < FieldIndex && !bEqual) bMatches = false;
							if (bEqual) ++Score;
						}
						if (bMatches && Score > BestScore) { Best = Option; BestScore = Score; }
					}
					if (!Best.IsValid()) return;
					RawRow->Selected = Best;
					RecipeOverrides.Add(RawRow->ItemClassPath, Best->RecipeClassPath);
					StatusText = TEXT("Bezugsweg geändert – jetzt Produktionsplan neu berechnen und speichern.");
					if (RecipeChoiceList.IsValid()) RecipeChoiceList->RebuildList();
					RefreshMachineSettings();
				})
				[
					SNew(STextBlock).Text_Lambda([RawRow, Field]()
					{
						return RawRow->Selected.IsValid()
							? SFPLocalization::Text(RawRow->Selected.Get()->*Field)
							: FText::GetEmpty();
					})
				]
			];
		}
		Body->AddSlot().AutoHeight().Padding(3, 2)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([RawRow]()
			{
				if (!RawRow->Selected.IsValid()) return FText::GetEmpty();
				const FString Locked = RawRow->Selected->bAvailable
					? FString() : TEXT("Gesperrt – im aktuellen Spielstand nicht verfügbar. ");
				return SFPLocalization::Text(Locked + RawRow->Selected->ConfigurationDetail);
			})
		];
		AppendMixedSourceControls(Body, RawRow);
	}
	Body->AddSlot().AutoHeight().Padding(3, 2, 3, 8)
	[
		SNew(SButton).Text(SFPLocalization::Text(TEXT("Diese Einstellungen bestätigen")))
		.OnClicked_Lambda([this, Row]()
		{
			if (Row->Selected.IsValid())
			{
				RecipeOverrides.Add(Row->ItemClassPath, Row->Selected->RecipeClassPath);
				for (const TSharedPtr<FSFPRecipeChoiceRow>& RawRow : Row->GroupedRawExtractions)
				{
					if (RawRow.IsValid() && RawRow->Selected.IsValid())
					{
						RecipeOverrides.Add(RawRow->ItemClassPath, RawRow->Selected->RecipeClassPath);
					}
				}
				StatusText = TEXT("Auswahl bestätigt – Plan neu berechnen und speichern.");
			}
			return FReply::Handled();
		})
	];
	return SNew(STableRow<TSharedPtr<FSFPRecipeChoiceRow>>, OwnerTable)[Body];
}

FReply SSFPPlannerWindow::HandleResetRecipeChoices()
{
	RecipeOverrides.Reset();
	ResourceSourceMixes.Reset();
	if (CurrentPlan.IsValid() && CurrentPlan->bPowerProductionPlan)
	{
		StatusText = TEXT("Automatische Rezeptwahl aktiviert");
		return HandleCalculatePower();
	}
	if (CurrentPlan.IsValid() && !SelectedTargets.IsEmpty())
	{
		CalculateForTargets(1.0, true, TEXT("Automatische Rezeptwahl aktiviert"));
	}
	else
	{
		RefreshRecipeChoices(nullptr);
		StatusText = TEXT("Automatische Rezeptwahl aktiviert");
	}
	return FReply::Handled();
}

TSharedRef<ITableRow> SSFPPlannerWindow::HandleGenerateInputBudgetRow(
	TSharedPtr<FSFPInputBudgetOption> Input,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Requirement = Input.IsValid()
		? FString::Printf(
			TEXT("Bedarf %s/min"),
			*FSFPNumberFormatting::Decimal(Input->RequiredRatePerMinute))
		: TEXT("Ungültiger Eingang");
	return SNew(STableRow<TSharedPtr<FSFPInputBudgetOption>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.45f)
		.VAlign(VAlign_Center)
		.Padding(2.0f, 1.0f, 5.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(Input.IsValid() ? FText::FromString(Input->ItemName) : FText::GetEmpty())
			.ToolTipText(Input.IsValid() ? FText::FromString(Input->ItemClassPath) : FText::GetEmpty())
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.30f)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 1.0f, 5.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(SFPLocalization::Text(Requirement))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.25f)
		.VAlign(VAlign_Center)
		[
			SNew(SNumericEntryBox<double>)
			.AllowSpin(true)
			.MinValue(0.0)
			.MaxValue(1000000000.0)
			.Value_Lambda([Input]() -> TOptional<double>
			{
				return Input.IsValid() ? TOptional<double>(Input->AvailableRatePerMinute) : TOptional<double>();
			})
			.OnValueChanged_Lambda([this, Input](const double NewValue)
			{
				if (Input.IsValid())
				{
					Input->AvailableRatePerMinute = FMath::Max(0.0, NewValue);
					if (CurrentPlan.IsValid())
					{
						CurrentPlan->AvailableInputRates.Add(Input->ItemClassPath, Input->AvailableRatePerMinute);
					}
				}
			})
		]
	];
}

void SSFPPlannerWindow::RefreshInputBudgets(
	const TSharedPtr<FSFPPlanResult>& Plan,
	const bool bPreserveExistingValues)
{
	TMap<FString, double> ExistingValues;
	if (bPreserveExistingValues)
	{
		for (const TSharedPtr<FSFPInputBudgetOption>& Input : InputBudgets)
		{
			if (Input.IsValid())
			{
				ExistingValues.Add(Input->ItemClassPath, Input->AvailableRatePerMinute);
			}
		}
	}

	InputBudgets.Reset();
	if (Plan.IsValid())
	{
		TMap<FString, TSharedPtr<FSFPInputBudgetOption>> InputsByClassPath;
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			if ((Node.Type != ESFPPlanNodeType::Source && Node.Type != ESFPPlanNodeType::Cycle)
				|| Node.RatePerMinute <= 0.0)
			{
				continue;
			}
			const FString Key = Node.ClassPath.IsEmpty() ? Node.Title : Node.ClassPath;
			TSharedPtr<FSFPInputBudgetOption>& Input = InputsByClassPath.FindOrAdd(Key);
			if (!Input.IsValid())
			{
				Input = MakeShared<FSFPInputBudgetOption>();
				Input->ItemName = Node.Title;
				Input->ItemClassPath = Key;
			}
			Input->RequiredRatePerMinute += Node.RatePerMinute;
		}
		InputsByClassPath.GenerateValueArray(InputBudgets);
		InputBudgets.Sort([](const TSharedPtr<FSFPInputBudgetOption>& Left, const TSharedPtr<FSFPInputBudgetOption>& Right)
		{
			return Left.IsValid() && Right.IsValid()
				? Left->ItemName.Compare(Right->ItemName, ESearchCase::IgnoreCase) < 0
				: Left.IsValid();
		});
		for (const TSharedPtr<FSFPInputBudgetOption>& Input : InputBudgets)
		{
			if (!Input.IsValid())
			{
				continue;
			}
			const double* ExistingValue = ExistingValues.Find(Input->ItemClassPath);
			const double* SavedValue = Plan.IsValid()
				? Plan->AvailableInputRates.Find(Input->ItemClassPath)
				: nullptr;
			Input->AvailableRatePerMinute = ExistingValue != nullptr
				? *ExistingValue
				: SavedValue != nullptr
					? *SavedValue
					: Input->RequiredRatePerMinute;
		}
	}
	if (InputBudgetList.IsValid())
	{
		InputBudgetList->RequestListRefresh();
	}
}

void SSFPPlannerWindow::CaptureInputBudgetsToPlan()
{
	if (!CurrentPlan.IsValid())
	{
		return;
	}
	CurrentPlan->AvailableInputRates.Reset();
	// This function is also called by automatic persistence while closing the
	// planner.  Never copy pending UI choices into an already calculated result:
	// doing so made an old 500 MW fuel-generator result look as if it had been
	// recalculated for a newly selected 2,500 MW nuclear generator.  Solver input
	// fields and transport choices are committed only by a successful solve.
	for (const TSharedPtr<FSFPInputBudgetOption>& Input : InputBudgets)
	{
		if (Input.IsValid())
		{
			CurrentPlan->AvailableInputRates.Add(Input->ItemClassPath, Input->AvailableRatePerMinute);
		}
	}
}

FReply SSFPPlannerWindow::HandleCalculateFromInputs()
{
    if (bInputPlanning)
    {
        StatusText = TEXT("Vorhandene Eingänge im Reiter Planung ändern und dort neu berechnen.");
        return FReply::Handled();
    }
	if (!Solver.IsValid() || !CurrentPlan.IsValid() || InputBudgets.IsEmpty()
		|| (!CurrentPlan->bPowerProductionPlan && SelectedTargets.IsEmpty()))
	{
		StatusText = TEXT("Zuerst einen Produktionsplan berechnen, damit alle externen Eingänge ermittelt werden");
		return FReply::Handled();
	}

	double Scale = TNumericLimits<double>::Max();
	FString LimitingInput;
	for (const TSharedPtr<FSFPInputBudgetOption>& Input : InputBudgets)
	{
		if (!Input.IsValid() || Input->RequiredRatePerMinute <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		if (!FMath::IsFinite(Input->AvailableRatePerMinute) || Input->AvailableRatePerMinute < 0.0)
		{
			StatusText = FString::Printf(TEXT("Ungültige Eingangsmenge für %s"), *Input->ItemName);
			return FReply::Handled();
		}
		const double CandidateScale = Input->AvailableRatePerMinute / Input->RequiredRatePerMinute;
		if (CandidateScale < Scale)
		{
			Scale = CandidateScale;
			LimitingInput = Input->ItemName;
		}
	}
	if (!FMath::IsFinite(Scale) || Scale == TNumericLimits<double>::Max() || Scale <= 0.0)
	{
		StatusText = TEXT("Mit diesen Eingängen ist keine positive Endproduktmenge möglich");
		return FReply::Handled();
	}

	const FString Prefix = FString::Printf(
		TEXT("Alle Endprodukte proportional aus den Rohstofflimits skaliert — Engpass: %s"),
		*LimitingInput);
	if (CurrentPlan->bPowerProductionPlan)
	{
		PowerTargetNetMW = FMath::Max(0.1, PowerTargetNetMW * Scale);
		StatusText = FString::Printf(
			TEXT("Stromziel aus Rohstofflimits skaliert — Engpass: %s"),
			*LimitingInput);
		return HandleCalculatePower();
	}
	CalculateForTargets(Scale, true, Prefix);
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleSavePlan()
{
	if (!CurrentPlan.IsValid())
	{
		StatusText = TEXT("Zuerst einen Produktionsplan berechnen");
		return FReply::Handled();
	}
	CaptureInputBudgetsToPlan();
	if (bSharedPlanMode)
	{
		const FString CleanName = PlanNameText.TrimStartAndEnd();
		int64 ExpectedRevision = 0;
		if (SelectedSavedPlan.IsValid() && SelectedSavedPlan->Name == CleanName)
		{
			if (SelectedSavedPlan->FileName != ActiveSharedPlanFileName || ActiveSharedPlanRevision <= 0)
			{
				StatusText = TEXT("Bestehenden Multiplayer-Plan vor dem Überschreiben zuerst laden");
				return FReply::Handled();
			}
			ExpectedRevision = ActiveSharedPlanRevision;
		}
		PersistCurrentPlan(false);
		QueueSharedPlanSave(CleanName, ExpectedRevision, false);
		return FReply::Handled();
	}

	FString FileName;
	FString Path;
	FString Error;
	if (!FSFPPlannerPersistence::SaveNamedPlan(PlanNameText, *CurrentPlan, FileName, Path, Error))
	{
		StatusText = FString::Printf(TEXT("Speichern fehlgeschlagen: %s"), *Error);
		return FReply::Handled();
	}

	PersistCurrentPlan(false);
	RefreshNamedPlans(FileName);
	if (MachineGuidanceState == 2) MachineGuidanceState = 3;
	StatusText = FString::Printf(TEXT("Plan „%s“ gespeichert"), *PlanNameText.TrimStartAndEnd());
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleLoadSavedPlan()
{
	if (!SelectedSavedPlan.IsValid())
	{
		StatusText = TEXT("Bitte zuerst einen gespeicherten Plan aus der Liste auswählen");
		return FReply::Handled();
	}
	if (bSharedPlanMode)
	{
		const FString SelectedName = SelectedSavedPlan->Name;
		const FString SelectedFileName = SelectedSavedPlan->FileName;
		FString Error;
		bSharedPlanRequestPending = true;
		StatusText = FString::Printf(TEXT("Multiplayer-Plan „%s“ wird geladen …"), *SelectedName);
		if (!USFPPlannerRemoteCallObject::RequestSharedPlanDownload(
			PlayerController.Get(),
			SelectedFileName,
			Error))
		{
			bSharedPlanRequestPending = false;
			StatusText = FString::Printf(TEXT("Multiplayer-Plan laden fehlgeschlagen: %s"), *Error);
		}
		return FReply::Handled();
	}

	FString Path;
	FString Error;
	TSharedPtr<FSFPPlanResult> Plan = FSFPPlannerPersistence::LoadNamedPlan(SelectedSavedPlan->FileName, Path, Error);
	if (!Plan.IsValid())
	{
		StatusText = FString::Printf(TEXT("Laden fehlgeschlagen: %s"), *Error);
		RefreshNamedPlans();
		return FReply::Handled();
	}

	ApplyPlanToUI(Plan);
	PersistCurrentPlan(false);
	StatusText = BuildPlanStatusText(
		*Plan,
		TOptional<double>(),
		FString::Printf(TEXT("Plan „%s“ geladen"), *SelectedSavedPlan->Name));
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleDeleteSavedPlan()
{
	if (!SelectedSavedPlan.IsValid())
	{
		StatusText = TEXT("Bitte zuerst einen gespeicherten Plan aus der Liste auswählen");
		return FReply::Handled();
	}
	if (bSharedPlanMode)
	{
		if (!SelectedSavedPlan->bCanDelete)
		{
			const FString OwnerName = SelectedSavedPlan->OwnerName.IsEmpty()
				? SFPLocalization::Select(TEXT("dieses Plans"), TEXT("of this plan"))
				: SelectedSavedPlan->OwnerName;
			StatusText = SFPLocalization::IsGerman()
				? FString::Printf(TEXT("Nur der Ersteller %s darf diesen Multiplayer-Plan löschen"), *OwnerName)
				: FString::Printf(TEXT("Only the creator %s may delete this multiplayer plan"), *OwnerName);
			return FReply::Handled();
		}
		const FString SelectedName = SelectedSavedPlan->Name;
		const FString SelectedFileName = SelectedSavedPlan->FileName;
		const int64 SelectedRevision = SelectedSavedPlan->Revision;
		FString Error;
		bSharedPlanRequestPending = true;
		StatusText = FString::Printf(TEXT("Multiplayer-Plan „%s“ wird gelöscht …"), *SelectedName);
		if (!USFPPlannerRemoteCallObject::RequestSharedPlanDelete(
			PlayerController.Get(),
			SelectedFileName,
			SelectedRevision,
			Error))
		{
			bSharedPlanRequestPending = false;
			StatusText = FString::Printf(TEXT("Multiplayer-Plan löschen fehlgeschlagen: %s"), *Error);
		}
		return FReply::Handled();
	}

	const FString DeletedName = SelectedSavedPlan->Name;
	FString Path;
	FString Error;
	if (!FSFPPlannerPersistence::DeleteNamedPlan(SelectedSavedPlan->FileName, Path, Error))
	{
		StatusText = FString::Printf(TEXT("Löschen fehlgeschlagen: %s"), *Error);
		return FReply::Handled();
	}
	RefreshNamedPlans();
	PlanNameText.Reset();
	if (PlanNameInput.IsValid())
	{
		PlanNameInput->SetText(FText::GetEmpty());
	}
	StatusText = FString::Printf(TEXT("Gespeicherter Plan „%s“ wurde gelöscht; der aktuelle Graph bleibt geöffnet"), *DeletedName);
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleTogglePlanScope()
{
	bSharedPlanMode = !bSharedPlanMode;
	bCarryCurrentPlanProgress = false;
	bSharedPlanRequestPending = false;
	bAutomaticSharedProgressSave = false;
	bSharedProgressDirty = false;
	bSharedPlanContentDirty = false;
	ActiveSharedPlanFileName.Reset();
	ActiveSharedPlanRevision = 0;
	IgnoreNextSharedChangeFileName.Reset();
	SelectedSavedPlan.Reset();
	SavedPlans.Reset();
	if (SavedPlanCombo.IsValid())
	{
		SavedPlanCombo->ClearSelection();
		SavedPlanCombo->RefreshOptions();
	}
	StatusText = bSharedPlanMode
		? TEXT("Multiplayer-Pläne: gemeinsam gespeichert und für alle Spieler synchronisiert")
		: TEXT("Persönliche Pläne: nur auf diesem Client gespeichert");
	RefreshNamedPlans();
	return FReply::Handled();
}

bool SSFPPlannerWindow::QueueSharedPlanSave(
	const FString& Name,
	const int64 ExpectedRevision,
	const bool bAutomaticProgressSave)
{
	if (!CurrentPlan.IsValid())
	{
		StatusText = TEXT("Zuerst einen Produktionsplan berechnen");
		return false;
	}
	if (bSharedPlanRequestPending)
	{
		if (bAutomaticProgressSave)
		{
			bSharedProgressDirty = true;
			StatusText = TEXT("Multiplayer-Plan-Synchronisierung läuft; neuer Fortschritt wird danach übertragen");
		}
		else
		{
			StatusText = TEXT("Eine Multiplayer-Plan-Anfrage läuft bereits");
		}
		return false;
	}

	FString Error;
	bSharedPlanRequestPending = true;
	bAutomaticSharedProgressSave = bAutomaticProgressSave;
	if (!bAutomaticProgressSave)
	{
		bSharedPlanContentDirty = false;
	}
	StatusText = bAutomaticProgressSave
		? TEXT("Baufortschritt wird mit dem Multiplayer-Plan synchronisiert …")
		: FString::Printf(TEXT("Multiplayer-Plan „%s“ wird gespeichert …"), *Name);
	if (!USFPPlannerRemoteCallObject::RequestSharedPlanSave(
		PlayerController.Get(),
		Name,
		ExpectedRevision,
		*CurrentPlan,
		Error))
	{
		bSharedPlanRequestPending = false;
		bAutomaticSharedProgressSave = false;
		if (!bAutomaticProgressSave && !ActiveSharedPlanFileName.IsEmpty())
		{
			bSharedPlanContentDirty = true;
		}
		StatusText = FString::Printf(TEXT("Multiplayer-Plan speichern fehlgeschlagen: %s"), *Error);
		return false;
	}
	return true;
}

FReply SSFPPlannerWindow::HandleClose()
{
	if (bCapturingPlannerHotkey)
	{
		bCapturingPlannerHotkey = false;
		SFPPlannerHotkey::SetCaptureInProgress(false);
	}
	PersistCurrentPlan(false);
	OnClose.ExecuteIfBound();
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleBeginHotkeyCapture()
{
	bCapturingPlannerHotkey = true;
	SFPPlannerHotkey::SetCaptureInProgress(true);
	StatusText = TEXT("Neue Tastenkombination drücken; Esc bricht ab, Rücktaste deaktiviert den Hotkey");
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleDisableHotkey()
{
	bCapturingPlannerHotkey = false;
	SFPPlannerHotkey::SetCaptureInProgress(false);
	SFPPlannerHotkey::Disable();
	StatusText = TEXT("Planner-Hotkey deaktiviert; Terminal und /sfpplanner open bleiben verfügbar");
	return FReply::Handled();
}

FText SSFPPlannerWindow::GetHotkeyButtonText() const
{
	if (bCapturingPlannerHotkey)
	{
		return SFPLocalization::Text(TEXT("TASTE DRÜCKEN …"));
	}

	const FString DisplayName = SFPPlannerHotkey::GetDisplayName();
	return DisplayName.IsEmpty()
		? SFPLocalization::Text(TEXT("DEAKTIVIERT"))
		: FText::FromString(DisplayName);
}

FReply SSFPPlannerWindow::HandleResetGraph()
{
	if (GraphPanel.IsValid())
	{
		GraphPanel->FitGraph();
	}
	return FReply::Handled();
}

FReply SSFPPlannerWindow::HandleAutoArrangeGraph()
{
	if (GraphPanel.IsValid())
	{
		GraphPanel->AutoArrange();
	}
	return FReply::Handled();
}

void SSFPPlannerWindow::HandleNodeCompletionChanged(const int32 NodeId, const bool bCompleted)
{
	if (!CurrentPlan.IsValid())
	{
		return;
	}
	FSFPPlanNode* Node = CurrentPlan->Nodes.FindByPredicate([NodeId](const FSFPPlanNode& Candidate)
	{
		return Candidate.Id == NodeId;
	});
	if (Node == nullptr || Node->bCompleted == bCompleted)
	{
		return;
	}

	Node->bCompleted = bCompleted;
	if (GraphPanel.IsValid())
	{
		GraphPanel->RefreshCompletionState();
	}
	const FString ProgressMessage = bCompleted
		? FString::Printf(TEXT("Knoten „%s“ erledigt"), *Node->Title)
		: FString::Printf(TEXT("Knoten „%s“ wieder geöffnet"), *Node->Title);
	if (!PersistCurrentPlan(false))
	{
		StatusText = ProgressMessage + TEXT(" — automatisches Speichern fehlgeschlagen");
		return;
	}
	if (bSharedPlanMode
		&& !bSharedPlanContentDirty
		&& !ActiveSharedPlanFileName.IsEmpty()
		&& ActiveSharedPlanRevision > 0)
	{
		const FString SharedName = SelectedSavedPlan.IsValid()
			&& SelectedSavedPlan->FileName == ActiveSharedPlanFileName
			? SelectedSavedPlan->Name
			: PlanNameText.TrimStartAndEnd();
		if (!SharedName.IsEmpty())
		{
			QueueSharedPlanSave(SharedName, ActiveSharedPlanRevision, true);
			return;
		}
	}
	if (bSharedPlanMode && bSharedPlanContentDirty)
	{
		StatusText = BuildPlanStatusText(
			*CurrentPlan,
			TOptional<double>(),
			ProgressMessage + TEXT(" — lokal geändert; mit SPEICHERN als Multiplayer-Plan veröffentlichen"));
		return;
	}
	StatusText = BuildPlanStatusText(*CurrentPlan, TOptional<double>(), ProgressMessage);
}

FReply SSFPPlannerWindow::HandleSelectTab(const int32 TabIndex)
{
	if (TabIndex == PowerTabIndex && CurrentPlan.IsValid() && !CurrentPlan->bPowerProductionPlan)
	{
		LastFactoryPowerDemandMW = FMath::Max(0.0, CurrentPlan->TotalBasePowerMW);
	}
	ActiveTabIndex = FMath::Clamp(TabIndex, PlanningTabIndex, GraphTabIndex);
	if (TabSwitcher.IsValid())
	{
		TabSwitcher->SetActiveWidgetIndex(ActiveTabIndex);
	}
	if (ActiveTabIndex == GraphTabIndex && GraphPanel.IsValid())
	{
		GraphPanel->ResetView();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

TOptional<double> SSFPPlannerWindow::GetTargetRate() const
{
	return TargetRate;
}

void SSFPPlannerWindow::HandleTargetRateChanged(const double NewValue)
{
	TargetRate = FMath::Max(0.001, NewValue);
}

TOptional<double> SSFPPlannerWindow::GetPowerTargetNetMW() const
{
	return PowerTargetNetMW;
}

void SSFPPlannerWindow::HandlePowerTargetNetMWChanged(const double NewValue)
{
	PowerCalculationError.Reset();
	PowerTargetNetMW = FMath::Max(0.1, NewValue);
	StatusText = TEXT("Nettoleistungsziel geändert – Stromplan neu berechnen");
}

ECheckBoxState SSFPPlannerWindow::GetUseFactoryPowerDemandState() const
{
	return bUseCurrentFactoryPowerDemand && LastFactoryPowerDemandMW > KINDA_SMALL_NUMBER
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SSFPPlannerWindow::HandleUseFactoryPowerDemandChanged(const ECheckBoxState NewState)
{
	bUseCurrentFactoryPowerDemand = NewState == ECheckBoxState::Checked;
	if (bUseCurrentFactoryPowerDemand)
	{
		if (CurrentPlan.IsValid() && !CurrentPlan->bPowerProductionPlan)
		{
			LastFactoryPowerDemandMW = FMath::Max(0.0, CurrentPlan->TotalBasePowerMW);
		}
		if (LastFactoryPowerDemandMW > KINDA_SMALL_NUMBER)
		{
			StatusText = TEXT("Strombedarf des aktuellen Fabrikplans übernommen – Brennstoffproduktion berechnen");
			return;
		}
		bUseCurrentFactoryPowerDemand = false;
		StatusText = TEXT("Kein berechneter Fabrikplan vorhanden – manuelles Nettoleistungsziel bleibt aktiv");
		return;
	}
	StatusText = TEXT("Manuelles Nettoleistungsziel aktiviert");
}

FText SSFPPlannerWindow::GetFactoryPowerDemandText() const
{
	if (LastFactoryPowerDemandMW <= KINDA_SMALL_NUMBER)
	{
		return SFPLocalization::Text(TEXT("Noch kein berechneter Fabrikplan verfügbar. Das manuelle Nettoleistungsziel ist aktiv."));
	}
	const FString FormattedDemand = FSFPNumberFormatting::Decimal(LastFactoryPowerDemandMW, 2);
	if (bUseCurrentFactoryPowerDemand)
	{
		const FString ActiveDetail = SFPLocalization::IsGerman()
			? FString::Printf(
				TEXT("Aktiver Zielwert: %s MW aus dem Fabrikplan. Das manuelle Nettoziel wird ersetzt; Reserve und Eigenverbrauch der Brennstoffherstellung kommen zusätzlich hinzu."),
				*FormattedDemand)
			: FString::Printf(
				TEXT("Active target: %s MW from the factory plan. This replaces the manual net target; reserve and fuel-production self-consumption are added automatically."),
				*FormattedDemand);
		return FText::FromString(ActiveDetail);
	}
	const FString Detail = SFPLocalization::IsGerman()
		? FString::Printf(
			TEXT("Verfügbarer Fabrikbedarf: %s MW. Aktivieren, um damit das manuelle Nettoziel zu ersetzen."),
			*FormattedDemand)
		: FString::Printf(
			TEXT("Available factory demand: %s MW. Enable this option to replace the manual net target."),
			*FormattedDemand);
	return FText::FromString(Detail);
}

double SSFPPlannerWindow::ResolvePowerTargetNetMW() const
{
	return bUseCurrentFactoryPowerDemand && LastFactoryPowerDemandMW > KINDA_SMALL_NUMBER
		? LastFactoryPowerDemandMW
		: PowerTargetNetMW;
}

TOptional<double> SSFPPlannerWindow::GetPowerReservePercent() const
{
	return PowerReservePercent;
}

void SSFPPlannerWindow::HandlePowerReservePercentChanged(const double NewValue)
{
	PowerCalculationError.Reset();
	PowerReservePercent = FMath::Clamp(NewValue, 0.0, 500.0);
	StatusText = TEXT("Leistungsreserve geändert – Stromplan neu berechnen");
}

TOptional<double> SSFPPlannerWindow::GetPowerGeneratorClockPercent() const
{
	return PowerGeneratorClockPercent;
}

TOptional<double> SSFPPlannerWindow::GetPowerGeneratorMinClockPercent() const
{
	if (!SelectedPowerGenerator.IsValid() || SelectedPowerGenerator->ClassPath.IsEmpty())
	{
		return 1.0;
	}
	if (!SelectedPowerGenerator->bCanChangePotential)
	{
		return 100.0;
	}
	return FMath::Max(1.0, SelectedPowerGenerator->MinPotential * 100.0);
}

TOptional<double> SSFPPlannerWindow::GetPowerGeneratorMaxClockPercent() const
{
	if (!SelectedPowerGenerator.IsValid() || SelectedPowerGenerator->ClassPath.IsEmpty())
	{
		return 250.0;
	}
	if (!SelectedPowerGenerator->bCanChangePotential)
	{
		return 100.0;
	}
	return FMath::Max(100.0, SelectedPowerGenerator->MaxPotential * 100.0);
}

void SSFPPlannerWindow::HandlePowerGeneratorClockPercentChanged(const double NewValue)
{
	if (!FMath::IsFinite(NewValue))
	{
		return;
	}
	const TOptional<double> MinClockValue = GetPowerGeneratorMinClockPercent();
	const TOptional<double> MaxClockValue = GetPowerGeneratorMaxClockPercent();
	const double MinClock = MinClockValue.IsSet() ? MinClockValue.GetValue() : 1.0;
	const double MaxClock = MaxClockValue.IsSet() ? MaxClockValue.GetValue() : 250.0;
	PowerGeneratorClockPercent = FMath::Clamp(NewValue, MinClock, MaxClock);
	bCarryCurrentPlanProgress = false;
	StatusText = TEXT("Generator-Takt geändert – Stromplan neu berechnen");
}

TOptional<int32> SSFPPlannerWindow::GetPassiveAlienPowerAugmenters() const
{
	return PassiveAlienPowerAugmenters;
}

void SSFPPlannerWindow::HandlePassiveAlienPowerAugmentersChanged(const int32 NewValue)
{
	PassiveAlienPowerAugmenters = FMath::Clamp(NewValue, 0, 10000);
	StatusText = TEXT("Alien Power Augmenter geändert – Stromplan neu berechnen");
}

TOptional<int32> SSFPPlannerWindow::GetFueledAlienPowerAugmenters() const
{
	return FueledAlienPowerAugmenters;
}

void SSFPPlannerWindow::HandleFueledAlienPowerAugmentersChanged(const int32 NewValue)
{
	FueledAlienPowerAugmenters = FMath::Clamp(NewValue, 0, 10000);
	StatusText = TEXT("Alien Power Augmenter mit Matrix geändert – Stromplan neu berechnen");
}

TOptional<double> SSFPPlannerWindow::GetEstimatedConnectionLength() const
{
	return EstimatedConnectionLengthMeters;
}

void SSFPPlannerWindow::HandleEstimatedConnectionLengthChanged(const double NewValue)
{
	EstimatedConnectionLengthMeters = FMath::Clamp(NewValue, 0.5, 1000.0);
}

ECheckBoxState SSFPPlannerWindow::GetOnlyAvailableState() const
{
	return bOnlyAvailable ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FText SSFPPlannerWindow::GetSelectedConveyorText() const
{
	return SFPLocalization::Text(SelectedConveyor.IsValid()
		? FString::Printf(TEXT("%s  ·  %s/min"),
			*SelectedConveyor->Tier.DisplayName,
			*FSFPNumberFormatting::Decimal(SelectedConveyor->Tier.CapacityPerMinute, 0))
		: TEXT("Kein Förderband geladen"));
}

FText SSFPPlannerWindow::GetSelectedConveyorLiftText() const
{
	return SFPLocalization::Text(SelectedConveyorLift.IsValid()
		? FString::Printf(TEXT("%s  ·  %s/min"),
			*SelectedConveyorLift->Tier.DisplayName,
			*FSFPNumberFormatting::Decimal(SelectedConveyorLift->Tier.CapacityPerMinute, 0))
		: TEXT("Kein Förderlift geladen"));
}

FText SSFPPlannerWindow::GetSelectedPowerGeneratorText() const
{
	if (!SelectedPowerGenerator.IsValid())
	{
		return SFPLocalization::Text(TEXT("Kein Generator geladen"));
	}
	return SelectedPowerGenerator->ClassPath.IsEmpty()
		? FText::FromString(SelectedPowerGenerator->DisplayName)
		: SFPLocalization::Text(FString::Printf(
			TEXT("%s · %s%s MW"),
			*SelectedPowerGenerator->DisplayName,
			SelectedPowerGenerator->bVariableOutput ? TEXT("max. ") : TEXT(""),
			*FSFPNumberFormatting::Decimal(SelectedPowerGenerator->PowerProductionMW, 2)));
}

FText SSFPPlannerWindow::GetSelectedPowerFuelText() const
{
	if (!SelectedPowerFuel.IsValid())
	{
		return SFPLocalization::Text(TEXT("Kein Brennstoff geladen"));
	}
	return SelectedPowerFuel->ClassPath.IsEmpty() || SelectedPowerFuel->bFuelFree
		? FText::FromString(SelectedPowerFuel->DisplayName)
		: SFPLocalization::Text(FString::Printf(
			TEXT("%s · %s MJ"),
			*SelectedPowerFuel->DisplayName,
			*FSFPNumberFormatting::Decimal(SelectedPowerFuel->EnergyValueMJ, 3)));
}

double SSFPPlannerWindow::EstimateMaximumPowerCalculationSeconds() const
{
	if (SmoothedMaximumPowerCalculationSeconds > 0.0)
	{
		return FMath::Clamp(SmoothedMaximumPowerCalculationSeconds, 0.1, 300.0);
	}

	const int32 PlanNodeCount = CurrentPlan.IsValid() ? CurrentPlan->Nodes.Num() : 0;
	int32 AlternativeRecipeCount = 0;
	TFunction<void(const TArray<TSharedPtr<FSFPRecipeChoiceRow>>&)> CountAlternatives;
	CountAlternatives = [&AlternativeRecipeCount, &CountAlternatives](
		const TArray<TSharedPtr<FSFPRecipeChoiceRow>>& Rows)
	{
		for (const TSharedPtr<FSFPRecipeChoiceRow>& Row : Rows)
		{
			if (!Row.IsValid())
			{
				continue;
			}
			AlternativeRecipeCount += FMath::Max(0, Row->Options.Num() - 1);
			CountAlternatives(Row->GroupedRawExtractions);
		}
	};
	CountAlternatives(RecipeChoiceRows);

	// The maximum search has a bounded alternate-recipe scan, followed by one
	// physical world-limit search. This estimate is intentionally conservative;
	// the measured duration from completed runs replaces it automatically.
	return FMath::Clamp(
		2.0 + static_cast<double>(PlanNodeCount) * 0.05
			+ static_cast<double>(FMath::Min(16, AlternativeRecipeCount)) * 0.2,
		2.0,
		45.0);
}

FText SSFPPlannerWindow::GetMaximumPowerEstimateText() const
{
	if (!CurrentPlan.IsValid() || !CurrentPlan->bPowerProductionPlan)
	{
		return SFPLocalization::Text(TEXT(
			"Geschätzte Dauer: zuerst einmal normal berechnen."));
	}

	const double EstimatedSeconds = EstimateMaximumPowerCalculationSeconds();
	if (LastMaximumPowerCalculationSeconds > 0.0)
	{
		return SFPLocalization::Text(FString::Printf(
			TEXT("Geschätzte Dauer: ca. %s s (letzter Lauf: %s s)"),
			*FSFPNumberFormatting::Decimal(EstimatedSeconds, 1),
			*FSFPNumberFormatting::Decimal(LastMaximumPowerCalculationSeconds, 1)));
	}

	return SFPLocalization::Text(FString::Printf(
		TEXT("Geschätzte Dauer: ca. %s s (erste Schätzung aus Plan und Rezepten)"),
		*FSFPNumberFormatting::Decimal(EstimatedSeconds, 1)));
}

FText SSFPPlannerWindow::GetStatusText() const
{
	return SFPLocalization::Text(StatusText);
}

FText SSFPPlannerWindow::GetPowerSummaryText() const
{
	if (!CurrentPlan.IsValid() || !CurrentPlan->bPowerProductionPlan)
	{
		return SFPLocalization::Text(TEXT(
			"Noch kein Stromplan berechnet.\n\n"
			"Nettoziel und Sicherheitsreserve festlegen. Generator und Betriebsart können gezielt gewählt oder der Automatik überlassen werden."));
	}

	const FSFPPlanResult& Plan = *CurrentPlan;
	FString Summary;
	if (!PowerCalculationError.IsEmpty())
	{
		Summary += FString::Printf(
			TEXT("LETZTE BERECHNUNG FEHLGESCHLAGEN\n%s\nDer darunter angezeigte Stromplan ist das letzte erfolgreiche Ergebnis.\n\n"),
			*PowerCalculationError);
	}
	else if (IsPowerPlanRequestDirty())
	{
		Summary += TEXT("ÄNDERUNGEN NOCH NICHT BERECHNET\nDer darunter angezeigte Stromplan ist das letzte erfolgreiche Ergebnis. Strom- und Brennstoffproduktion neu berechnen, um die aktuelle Auswahl zu übernehmen.\n\n");
	}
	if (Plan.bMaximumPowerPlan)
	{
		if (Plan.bMaximumPowerSearchCapped)
		{
			Summary += FString::Printf(
				TEXT("WELTRESSOURCEN-MAXIMUM\n"
					"• Mindestens erreichbar: %s MW netto\n"
					"• Die Suchgrenze wurde erreicht; für mindestens eine benötigte Ressource liegt keine wirksame endliche Grenze vor.\n\n"),
				*FSFPNumberFormatting::Decimal(Plan.RequestedNetPowerMW, 2));
		}
		else
		{
			Summary += FString::Printf(
				TEXT("WELTRESSOURCEN-MAXIMUM\n"
					"• Maximal nutzbares Nettoziel: %s MW\n"
					"• Begrenzender Rohstoff: %s\n"
					"• Förderkapazität dieser Konfiguration: %s/min\n"
					"• Sicherheitsreserve, freie/belegte Quellen sowie gewählte Miner, Bohrköpfe, Module und Betriebsflüssigkeiten sind berücksichtigt.\n\n"),
				*FSFPNumberFormatting::Decimal(Plan.RequestedNetPowerMW, 2),
				Plan.MaximumPowerLimitingResourceDisplayName.IsEmpty()
					? TEXT("Rohstoffkapazität")
					: *CurrentPlannerItemName(
						Plan.MaximumPowerLimitingResourceDisplayName,
						Plan.MaximumPowerLimitingResourceClassPath),
				*FSFPNumberFormatting::Decimal(Plan.MaximumPowerLimitingResourceCapacityPerMinute, 3));
		}
	}
	Summary += FString::Printf(
		TEXT("NETZBILANZ\n"
			"• Zielverbrauch: %s MW\n"
			"• Bruttoerzeugung: %s MW\n"
			"• Eigenverbrauch der Erzeugungskette: %s MW\n"
			"• Reale Nettoleistung: %s MW\n"
			"• Verbleibende Reserve: %s MW (%s%%)\n\n"
			"ERZEUGUNG\n"
			"• Generator: %s\n"
			"• Betriebsart: %s\n"
			"• Generatorleistung: %s MW je Gebäude bei %s%% (Basis %s MW @ 100%%)\n"
			"• %d Generatoren bauen | %s\n"),
		*FSFPNumberFormatting::Decimal(Plan.RequestedNetPowerMW, 2),
		*FSFPNumberFormatting::Decimal(Plan.GrossPowerMW, 2),
		*FSFPNumberFormatting::Decimal(Plan.SelfConsumptionPowerMW, 2),
		*FSFPNumberFormatting::Decimal(Plan.NetPowerMW, 2),
		*FSFPNumberFormatting::Decimal(Plan.ReservePowerMW, 2),
		*FSFPNumberFormatting::Decimal(Plan.PowerReservePercent, 1),
		*Plan.SelectedGeneratorDisplayName,
		*Plan.SelectedFuelDisplayName,
		*FSFPNumberFormatting::Decimal(Plan.GeneratorPowerMW, 2),
		*FSFPNumberFormatting::Decimal(Plan.ConfiguredGeneratorClockPercent, 1),
		*FSFPNumberFormatting::Decimal(Plan.GeneratorBasePowerMW > KINDA_SMALL_NUMBER ? Plan.GeneratorBasePowerMW : Plan.GeneratorPowerMW, 2),
		Plan.BuiltGeneratorCount,
		*BuildPowerGeneratorClockingBreakdown(Plan));

	if (Plan.PassiveAlienPowerAugmenters + Plan.FueledAlienPowerAugmenters > 0)
	{
		Summary += FString::Printf(
			TEXT("\nALIEN POWER AUGMENTER\n"
				"• Passiv: %d | Mit Matrix: %d\n"
				"• Generatorbasis vor Augmenter: %s MW\n"
				"• Augmenter-Basisleistung: %s MW | Netzfaktor ×%s\n"
				"• Augmenter-Beitrag zur Bruttoerzeugung: %s MW\n"),
			Plan.PassiveAlienPowerAugmenters,
			Plan.FueledAlienPowerAugmenters,
			*FSFPNumberFormatting::Decimal(Plan.BaseGeneratorGrossPowerMW, 2),
			*FSFPNumberFormatting::Decimal(Plan.AlienPowerAugmenterBaseMW, 2),
			*FSFPNumberFormatting::Decimal(Plan.AlienPowerMultiplier, 3),
			*FSFPNumberFormatting::Decimal(Plan.AlienPowerContributionMW, 2));
		if (Plan.AlienPowerMatrixRatePerMinute > KINDA_SMALL_NUMBER)
		{
			Summary += FString::Printf(
				TEXT("• %s: %s/min\n"),
				Plan.AlienPowerMatrixDisplayName.IsEmpty() ? TEXT("Alien Power Matrix") : *Plan.AlienPowerMatrixDisplayName,
				*FSFPNumberFormatting::Decimal(Plan.AlienPowerMatrixRatePerMinute, 3));
		}
	}

	if (Plan.SelectedFuelClassPath == TEXT("SFP.FuelFree"))
	{
		Summary += TEXT("• Kein Brennstoffbedarf\n");
	}
	else if (Plan.SelectedFuelForm == TEXT("liquid") || Plan.SelectedFuelForm == TEXT("gas"))
	{
		Summary += FString::Printf(
			TEXT("• Brennstoffbedarf: %s m³/min\n"),
			*FSFPNumberFormatting::Decimal(Plan.FuelRatePerMinute, 3));
	}
	else
	{
		Summary += FString::Printf(
			TEXT("• Brennstoffbedarf: %s/min\n"),
			*FSFPNumberFormatting::Decimal(Plan.FuelRatePerMinute, 3));
	}
	if (!Plan.SupplementalDisplayName.IsEmpty() && Plan.SupplementalRatePerMinute > 0.0)
	{
		if (Plan.SupplementalForm == TEXT("liquid") || Plan.SupplementalForm == TEXT("gas"))
		{
			Summary += FString::Printf(
				TEXT("• %s: %s m³/min\n"),
				*Plan.SupplementalDisplayName,
				*FSFPNumberFormatting::Decimal(Plan.SupplementalRatePerMinute, 3));
		}
		else
		{
			Summary += FString::Printf(
				TEXT("• %s: %s/min\n"),
				*Plan.SupplementalDisplayName,
				*FSFPNumberFormatting::Decimal(Plan.SupplementalRatePerMinute, 3));
		}
	}

	Summary += TEXT("\nEXTERNE ROHSTOFFE\n");
	if (InputBudgets.IsEmpty())
	{
		Summary += TEXT("• Keine externen Eingänge ermittelt\n");
	}
	else
	{
		for (const TSharedPtr<FSFPInputBudgetOption>& Input : InputBudgets)
		{
			if (Input.IsValid())
			{
				Summary += FString::Printf(
					TEXT("• %s: %s/min\n"),
					*Input->ItemName,
					*FSFPNumberFormatting::Decimal(Input->RequiredRatePerMinute, 3));
			}
		}
	}

	Summary += TEXT("\nNEBENPRODUKTE & ABFALL\n");
	bool bHasByproducts = false;
	for (const FSFPPlanNode& Node : Plan.Nodes)
	{
		if (Node.Type != ESFPPlanNodeType::Byproduct || Node.RatePerMinute <= 0.0)
		{
			continue;
		}
		bHasByproducts = true;
		Summary += FString::Printf(
			TEXT("• %s: %s/min\n"),
			*Node.Title,
			*FSFPNumberFormatting::Decimal(Node.RatePerMinute, 3));
	}
	if (!bHasByproducts)
	{
		Summary += TEXT("• Keine Nebenprodukte ermittelt\n");
	}

	Summary += FString::Printf(
		TEXT("\nERZEUGUNGSKETTE\n"
			"• %s MW Komponenten- und Produktionsleistung bereits im Eigenverbrauch abgezogen\n"
			"• Vollständige Maschinen-, Transport-, Baukosten- und Rezeptdetails stehen in den übrigen Tabs."),
		*FSFPNumberFormatting::Decimal(Plan.SelfConsumptionPowerMW, 2));
	return SFPLocalization::Text(Summary);
}

bool SSFPPlannerWindow::IsPowerPlanRequestDirty() const
{
	if (!CurrentPlan.IsValid() || !CurrentPlan->bPowerProductionPlan)
	{
		return false;
	}
	const FString GeneratorPath = SelectedPowerGenerator.IsValid()
		? SelectedPowerGenerator->ClassPath : FString();
	const FString FuelPath = SelectedPowerFuel.IsValid()
		? SelectedPowerFuel->ClassPath : FString();
	return !FMath::IsNearlyEqual(CurrentPlan->RequestedNetPowerMW, ResolvePowerTargetNetMW(), 0.001)
		|| !FMath::IsNearlyEqual(CurrentPlan->PowerReservePercent, PowerReservePercent, 0.001)
		|| CurrentPlan->RequestedGeneratorClassPath != GeneratorPath
		|| CurrentPlan->RequestedFuelClassPath != FuelPath
		|| !FMath::IsNearlyEqual(CurrentPlan->ConfiguredGeneratorClockPercent, PowerGeneratorClockPercent, 0.001)
		|| CurrentPlan->PassiveAlienPowerAugmenters != PassiveAlienPowerAugmenters
		|| CurrentPlan->FueledAlienPowerAugmenters != FueledAlienPowerAugmenters
		|| CurrentPlan->bOnlyAvailableRecipes != bOnlyAvailable;
}

FText SSFPPlannerWindow::GetMachineSummaryText() const
{
	if (!CurrentPlan.IsValid())
	{
		return SFPLocalization::Text(TEXT("Noch kein Produktionsplan berechnet.\n\nIm Tab PLANUNG Endprodukte und ihre gewünschten Mengen hinzufügen."));
	}

	TArray<const FSFPPlanNode*> MachineNodes;
	TMap<FString, int32> RoutingByIngameName;
	int32 TotalWholeMachines = 0;
	for (const FSFPPlanNode& Node : CurrentPlan->Nodes)
	{
		if ((Node.Type == ESFPPlanNodeType::Splitter || Node.Type == ESFPPlanNodeType::Merger)
			&& Node.InfrastructureCount > 0)
		{
			RoutingByIngameName.FindOrAdd(Node.Title) += Node.InfrastructureCount;
			continue;
		}
		if ((Node.Type != ESFPPlanNodeType::Machine && Node.Type != ESFPPlanNodeType::Generator)
			|| Node.MachineCount <= 0.0)
		{
			continue;
		}
		const int32 WholeMachines = BuiltMachineCountForNode(Node);
		MachineNodes.Add(&Node);
		TotalWholeMachines += WholeMachines;
	}

	FString Summary = BuildTargetSummary(*CurrentPlan);
	if (CurrentPlan->bPowerProductionPlan)
	{
		Summary += FString::Printf(
			TEXT("\n\nGESAMT\n%d Maschinen und Generatoren tatsächlich bauen\n%s MW brutto | %s MW Eigenverbrauch | %s MW netto\n%d Routing-Bauwerke\n"),
			TotalWholeMachines,
			*FSFPNumberFormatting::Decimal(CurrentPlan->GrossPowerMW, 2),
			*FSFPNumberFormatting::Decimal(CurrentPlan->SelfConsumptionPowerMW, 2),
			*FSFPNumberFormatting::Decimal(CurrentPlan->NetPowerMW, 2),
			CurrentPlan->SplitterCount + CurrentPlan->MergerCount);
	}
	else
	{
		Summary += FString::Printf(
			TEXT("\n\nGESAMT\n%d Maschinen tatsächlich bauen\nStrombedarf bei geplanter Taktung: %s MW\n%d Routing-Bauwerke\n"),
			TotalWholeMachines,
			*FSFPNumberFormatting::Decimal(CurrentPlan->TotalBasePowerMW, 2),
			CurrentPlan->SplitterCount + CurrentPlan->MergerCount);
	}

	MachineNodes.Sort([](const FSFPPlanNode& Left, const FSFPPlanNode& Right)
	{
		const int32 TitleOrder = Left.Title.Compare(Right.Title, ESearchCase::IgnoreCase);
		return TitleOrder == 0
			? Left.ProducedItemClassPath < Right.ProducedItemClassPath
			: TitleOrder < 0;
	});
	Summary += TEXT("\nMASCHINEN UND GENERATOREN NACH ZWEIG\n");
	for (const FSFPPlanNode* MachineNode : MachineNodes)
	{
		if (MachineNode == nullptr)
		{
			continue;
		}
		FString ProductName = MachineNode->ProducedItemClassPath.IsEmpty()
			? TEXT("Anlagenkomponente")
			: FPaths::GetBaseFilename(MachineNode->ProducedItemClassPath);
		for (const FSFPPlanEdge& Edge : CurrentPlan->Edges)
		{
			if (Edge.SourceNodeId == MachineNode->Id
				&& Edge.ItemClassPath == MachineNode->ProducedItemClassPath)
			{
				ProductName = Edge.ItemName;
				break;
			}
		}
		FString OperatingDetail = BuildClockingBreakdown(*MachineNode);
		if (MachineNode->SomersloopCount > 0)
		{
			OperatingDetail += FString::Printf(
				TEXT(" | %d Somersloop(s)/Maschine | Boost %s%%"),
				MachineNode->SomersloopCount,
				*FSFPNumberFormatting::Decimal(MachineNode->ProductionBoost * 100.0, 1));
		}
		if (MachineNode->bFuelPowered && !MachineNode->FuelDisplayName.IsEmpty())
		{
			OperatingDetail += FString::Printf(
				TEXT(" | %s %s/min | Brennleistung %s MW"),
				*MachineNode->FuelDisplayName,
				*FSFPNumberFormatting::Decimal(MachineNode->FuelRatePerMinute, 3),
				*FSFPNumberFormatting::Decimal(MachineNode->PowerMW, 2));
		}
		Summary += FString::Printf(
			TEXT("• %s → %s: %d bauen  |  %s\n"),
			*MachineNode->Title,
			*ProductName,
			BuiltMachineCountForNode(*MachineNode),
			*OperatingDetail);
	}
	TArray<FString> RoutingNames;
	RoutingByIngameName.GenerateKeyArray(RoutingNames);
	RoutingNames.Sort([](const FString& Left, const FString& Right)
	{
		return Left.Compare(Right, ESearchCase::IgnoreCase) < 0;
	});
	if (!RoutingNames.IsEmpty())
	{
		Summary += TEXT("\nROUTING NACH INGAME-NAME\n");
		for (const FString& RoutingName : RoutingNames)
		{
			Summary += FString::Printf(
				TEXT("• %s: %d bauen\n"),
				*RoutingName,
				RoutingByIngameName[RoutingName]);
		}
	}
	return SFPLocalization::Text(Summary);
}

FText SSFPPlannerWindow::GetResourceSummaryText() const
{
	if (!CurrentPlan.IsValid())
	{
		return SFPLocalization::Text(TEXT("Noch kein Produktionsplan berechnet.\n\nNach der Berechnung erscheinen hier Rohstoffbedarf, Transportmengen und sämtliche Baukosten."));
	}

	FString Summary = BuildTargetSummary(*CurrentPlan);
	Summary += TEXT("\n\nBENÖTIGTE EXTERNE EINGÄNGE\n");
	if (InputBudgets.IsEmpty())
	{
		Summary += TEXT("• Keine externen Eingänge ermittelt\n");
	}
	else
	{
		for (const TSharedPtr<FSFPInputBudgetOption>& Input : InputBudgets)
		{
			if (!Input.IsValid())
			{
				continue;
			}
			Summary += FString::Printf(
				TEXT("• %s: %s/min benötigt  |  %s/min verfügbar\n"),
				*Input->ItemName,
				*FSFPNumberFormatting::Decimal(Input->RequiredRatePerMinute),
				*FSFPNumberFormatting::Decimal(Input->AvailableRatePerMinute));
		}
	}

	Summary += FString::Printf(
		TEXT("\nTRANSPORTSCHÄTZUNG\nFörderband: %s (%s/min)\nFörderlift: %s (%s/min)\nMittlere Länge: %s m je Graphverbindung\n• %d Förderbandlinien  •  ca. %s m\n• %d Rohrleitungen  •  ca. %s m\n"),
		CurrentPlan->SelectedConveyorDisplayName.IsEmpty() ? TEXT("nicht geladen") : *CurrentPlan->SelectedConveyorDisplayName,
		*FSFPNumberFormatting::Decimal(CurrentPlan->SelectedConveyorCapacityPerMinute, 0),
		CurrentPlan->SelectedConveyorLiftDisplayName.IsEmpty() ? TEXT("nicht geladen") : *CurrentPlan->SelectedConveyorLiftDisplayName,
		*FSFPNumberFormatting::Decimal(CurrentPlan->SelectedConveyorLiftCapacityPerMinute, 0),
		*FSFPNumberFormatting::Decimal(CurrentPlan->EstimatedConnectionLengthMeters, 1),
		CurrentPlan->ConveyorLineCount,
		*FSFPNumberFormatting::Decimal(CurrentPlan->EstimatedConveyorMeters, 1),
		CurrentPlan->PipelineLineCount,
		*FSFPNumberFormatting::Decimal(CurrentPlan->EstimatedPipelineMeters, 1));

	auto AppendCosts = [&Summary](const TCHAR* Heading, const TArray<FSFPConstructionCost>& Costs)
	{
		Summary += FString::Printf(TEXT("\n%s\n"), Heading);
		if (Costs.IsEmpty())
		{
			Summary += TEXT("• Keine Kosten ermittelt\n");
			return;
		}
		for (const FSFPConstructionCost& Cost : Costs)
		{
			Summary += FString::Printf(
				TEXT("• %s × %s\n"),
				*FSFPNumberFormatting::Decimal(Cost.Amount),
				*Cost.DisplayName);
		}
	};

	AppendCosts(TEXT("BAUMATERIAL FÜR MASCHINEN"), CurrentPlan->MachineConstructionCosts);
	AppendCosts(TEXT("GESCHÄTZTES MATERIAL FÜR BÄNDER, ROHRE UND VERTEILUNG"), CurrentPlan->InfrastructureConstructionCosts);
	AppendCosts(TEXT("GESAMTES BAUMATERIAL"), CurrentPlan->ConstructionCosts);
	Summary += TEXT("\nHinweis: Transportmaterial ist eine Schätzung. Förderlifte begrenzen die Linienkapazität; ihre Anzahl und Höhe können aus dem logischen Graphen nicht zuverlässig abgeleitet werden. Steigungen und Umwege werden nicht automatisch vermessen.");
	return SFPLocalization::Text(Summary);
}

FText SSFPPlannerWindow::GetGraphProgressText() const
{
	if (!CurrentPlan.IsValid() || CurrentPlan->Nodes.IsEmpty())
	{
		return SFPLocalization::Text(TEXT("BAUFORTSCHRITT –"));
	}
	int32 CompletedNodeCount = 0;
	for (const FSFPPlanNode& Node : CurrentPlan->Nodes)
	{
		CompletedNodeCount += Node.bCompleted ? 1 : 0;
	}
	return SFPLocalization::Text(FString::Printf(
		TEXT("BAUFORTSCHRITT %d/%d"),
		CompletedNodeCount,
		CurrentPlan->Nodes.Num()));
}

FText SSFPPlannerWindow::GetSelectedSavedPlanText() const
{
	if (!SelectedSavedPlan.IsValid())
	{
		if (bSharedPlanMode)
		{
			return SFPLocalization::Text(SavedPlans.IsEmpty()
				? TEXT("Noch keine Multiplayer-Pläne")
				: TEXT("Multiplayer-Plan auswählen ..."));
		}
		return SFPLocalization::Text(SavedPlans.IsEmpty()
			? TEXT("Noch keine benannten Pläne")
			: TEXT("Gespeicherten Plan auswählen ..."));
	}
	if (SelectedSavedPlan->bPowerProductionPlan)
	{
		return SFPLocalization::Text(FString::Printf(
			TEXT("%s — Stromversorgung (%s MW netto)"),
			*SelectedSavedPlan->Name,
			*FSFPNumberFormatting::Decimal(SelectedSavedPlan->TargetRatePerMinute, 2)));
	}
	return SelectedSavedPlan->TargetCount > 1
		? SFPLocalization::Text(FString::Printf(
			TEXT("%s — %d Endprodukte"),
			*SelectedSavedPlan->Name,
			SelectedSavedPlan->TargetCount))
		: SFPLocalization::Text(FString::Printf(
			TEXT("%s — %s (%s/min)"),
			*SelectedSavedPlan->Name,
			*SelectedSavedPlan->TargetName,
			*FSFPNumberFormatting::Decimal(SelectedSavedPlan->TargetRatePerMinute)));
}

FText SSFPPlannerWindow::GetPlanScopeText() const
{
	return SFPLocalization::Text(bSharedPlanMode ? TEXT("MULTIPLAYER") : TEXT("PERSÖNLICH"));
}

void SSFPPlannerWindow::HandlePlanNameChanged(const FText& NewText)
{
	if (PlanNameText != NewText.ToString()) bCarryCurrentPlanProgress = false;
	PlanNameText = NewText.ToString();
}

void SSFPPlannerWindow::HandleSavedPlanSelected(
	TSharedPtr<FSFPSavedPlanInfo> Plan,
	const ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct) bCarryCurrentPlanProgress = false;
	SelectedSavedPlan = MoveTemp(Plan);
	if (SelectedSavedPlan.IsValid())
	{
		PlanNameText = SelectedSavedPlan->Name;
		if (PlanNameInput.IsValid())
		{
			PlanNameInput->SetText(FText::FromString(PlanNameText));
		}
	}
}

TSharedRef<SWidget> SSFPPlannerWindow::HandleGenerateSavedPlanWidget(TSharedPtr<FSFPSavedPlanInfo> Plan)
{
	FString Label = TEXT("Ungültiger gespeicherter Plan");
	if (Plan.IsValid())
	{
		if (Plan->bPowerProductionPlan)
		{
			Label = FString::Printf(
				TEXT("%s — Stromversorgung (%s MW netto)"),
				*Plan->Name,
				*FSFPNumberFormatting::Decimal(Plan->TargetRatePerMinute, 2));
		}
		else if (Plan->TargetCount > 1)
		{
			Label = FString::Printf(TEXT("%s — %d Endprodukte"), *Plan->Name, Plan->TargetCount);
		}
		else
		{
			Label = FString::Printf(
				TEXT("%s — %s (%s/min)"),
				*Plan->Name,
				*Plan->TargetName,
				*FSFPNumberFormatting::Decimal(Plan->TargetRatePerMinute));
		}
		if (bSharedPlanMode)
		{
			const FString Owner = Plan->OwnerName.IsEmpty() ? TEXT("Server") : Plan->OwnerName;
			Label += FString::Printf(TEXT(" — erstellt von %s"), *Owner);
			if (!Plan->UpdatedBy.IsEmpty() && Plan->UpdatedBy != Owner)
			{
				Label += FString::Printf(TEXT(", zuletzt %s"), *Plan->UpdatedBy);
			}
		}
	}
	return SNew(STextBlock)
		.Text(SFPLocalization::Text(Label));
}

void SSFPPlannerWindow::RefreshNamedPlans(const FString& SelectFileName)
{
	if (bSharedPlanMode)
	{
		RequestSharedPlanCatalog(SelectFileName);
		return;
	}
	FString FileToSelect = SelectFileName;
	if (FileToSelect.IsEmpty() && SelectedSavedPlan.IsValid())
	{
		FileToSelect = SelectedSavedPlan->FileName;
	}

	if (SavedPlanCombo.IsValid())
	{
		SavedPlanCombo->ClearSelection();
	}
	SelectedSavedPlan.Reset();
	SavedPlans.Reset();
	FString Error;
	TArray<FSFPSavedPlanInfo> PlanInfos;
	FSFPPlannerPersistence::ListNamedPlans(PlanInfos, Error);
	SavedPlans.Reserve(PlanInfos.Num());
	for (FSFPSavedPlanInfo& PlanInfo : PlanInfos)
	{
		SavedPlans.Add(MakeShared<FSFPSavedPlanInfo>(MoveTemp(PlanInfo)));
	}
	if (SavedPlanCombo.IsValid())
	{
		SavedPlanCombo->RefreshOptions();
	}

	if (!FileToSelect.IsEmpty())
	{
		const TSharedPtr<FSFPSavedPlanInfo>* Match = SavedPlans.FindByPredicate(
			[&FileToSelect](const TSharedPtr<FSFPSavedPlanInfo>& Plan)
			{
				return Plan.IsValid() && Plan->FileName == FileToSelect;
			});
		if (Match != nullptr)
		{
			SelectedSavedPlan = *Match;
			if (SavedPlanCombo.IsValid())
			{
				SavedPlanCombo->SetSelectedItem(SelectedSavedPlan);
			}
		}
	}

	if (!Error.IsEmpty())
	{
		StatusText = Error;
	}
}

void SSFPPlannerWindow::RequestSharedPlanCatalog(const FString& SelectFileName)
{
	PendingSharedPlanSelection = SelectFileName;
	FString Error;
	bSharedPlanRequestPending = true;
	if (!USFPPlannerRemoteCallObject::RequestSharedPlanCatalog(PlayerController.Get(), Error))
	{
		bSharedPlanRequestPending = false;
		StatusText = FString::Printf(TEXT("Multiplayer-Plan-Liste konnte nicht geladen werden: %s"), *Error);
		return;
	}
}

void SSFPPlannerWindow::PopulateSharedPlans(
	const TArray<FSFPSharedPlanSummary>& Plans,
	const FString& SelectFileName)
{
	FString FileToSelect = SelectFileName;
	if (FileToSelect.IsEmpty() && SelectedSavedPlan.IsValid())
	{
		FileToSelect = SelectedSavedPlan->FileName;
	}
	if (SavedPlanCombo.IsValid())
	{
		SavedPlanCombo->ClearSelection();
	}
	SelectedSavedPlan.Reset();
	SavedPlans.Reset();
	SavedPlans.Reserve(Plans.Num());
	for (const FSFPSharedPlanSummary& Summary : Plans)
	{
		TSharedPtr<FSFPSavedPlanInfo> Info = MakeShared<FSFPSavedPlanInfo>();
		Info->Name = Summary.Name;
		Info->FileName = Summary.FileName;
		Info->TargetName = Summary.TargetName;
		Info->TargetRatePerMinute = Summary.TargetRatePerMinute;
		Info->TargetCount = Summary.TargetCount;
		Info->bPowerProductionPlan = Summary.bPowerProductionPlan;
		Info->Revision = Summary.Revision;
		Info->OwnerName = Summary.OwnerName;
		Info->UpdatedBy = Summary.UpdatedBy;
		Info->bCanDelete = Summary.bCanDelete;
		SavedPlans.Add(MoveTemp(Info));
	}
	SavedPlans.Sort([](const TSharedPtr<FSFPSavedPlanInfo>& Left, const TSharedPtr<FSFPSavedPlanInfo>& Right)
	{
		if (!Left.IsValid()) return false;
		if (!Right.IsValid()) return true;
		return Left->Name.Compare(Right->Name, ESearchCase::IgnoreCase) < 0;
	});
	if (SavedPlanCombo.IsValid())
	{
		SavedPlanCombo->RefreshOptions();
	}
	if (!FileToSelect.IsEmpty())
	{
		const TSharedPtr<FSFPSavedPlanInfo>* Match = SavedPlans.FindByPredicate(
			[&FileToSelect](const TSharedPtr<FSFPSavedPlanInfo>& Plan)
			{
				return Plan.IsValid() && Plan->FileName == FileToSelect;
			});
		if (Match != nullptr)
		{
			SelectedSavedPlan = *Match;
			if (SavedPlanCombo.IsValid())
			{
				SavedPlanCombo->SetSelectedItem(SelectedSavedPlan);
			}
		}
	}
}

void SSFPPlannerWindow::ReceiveSharedPlanCatalog(
	const TArray<FSFPSharedPlanSummary>& Plans,
	const FString& Error)
{
	bSharedPlanRequestPending = false;
	if (!bSharedPlanMode)
	{
		return;
	}
	PopulateSharedPlans(Plans, PendingSharedPlanSelection);
	PendingSharedPlanSelection.Reset();
	if (!Error.IsEmpty())
	{
		StatusText = Error;
	}
}

void SSFPPlannerWindow::ReceiveSharedPlan(
	const FSFPSharedPlanSummary& Summary,
	const FString& PlanJson,
	const FString& Error)
{
	bSharedPlanRequestPending = false;
	if (!Error.IsEmpty())
	{
		StatusText = FString::Printf(TEXT("Multiplayer-Plan laden fehlgeschlagen: %s"), *Error);
		return;
	}
	if (Summary.FileName.IsEmpty() || Summary.Revision <= 0 || PlanJson.IsEmpty())
	{
		StatusText = TEXT("Empfangener Multiplayer-Plan enthält keine gültigen Servermetadaten");
		return;
	}
	FString SerializedName;
	FString ParseError;
	TSharedPtr<FSFPPlanResult> Plan = FSFPPlannerPersistence::DeserializePlan(
		PlanJson,
		SerializedName,
		ParseError);
	if (!Plan.IsValid())
	{
		StatusText = FString::Printf(TEXT("Empfangener Multiplayer-Plan ist ungültig: %s"), *ParseError);
		return;
	}
	if (!bSharedPlanMode)
	{
		return;
	}

	ApplyPlanToUI(Plan);
	bSharedPlanContentDirty = false;
	PersistCurrentPlan(false);
	ActiveSharedPlanFileName = Summary.FileName;
	ActiveSharedPlanRevision = Summary.Revision;
	PlanNameText = Summary.Name.IsEmpty() ? SerializedName : Summary.Name;
	if (PlanNameInput.IsValid())
	{
		PlanNameInput->SetText(FText::FromString(PlanNameText));
	}
	TArray<FSFPSharedPlanSummary> UpdatedPlans;
	for (const TSharedPtr<FSFPSavedPlanInfo>& Existing : SavedPlans)
	{
		if (!Existing.IsValid() || Existing->FileName == Summary.FileName) continue;
		FSFPSharedPlanSummary ExistingSummary;
		ExistingSummary.Name = Existing->Name;
		ExistingSummary.FileName = Existing->FileName;
		ExistingSummary.TargetName = Existing->TargetName;
		ExistingSummary.TargetRatePerMinute = Existing->TargetRatePerMinute;
		ExistingSummary.TargetCount = Existing->TargetCount;
		ExistingSummary.bPowerProductionPlan = Existing->bPowerProductionPlan;
		ExistingSummary.Revision = Existing->Revision;
		ExistingSummary.OwnerName = Existing->OwnerName;
		ExistingSummary.UpdatedBy = Existing->UpdatedBy;
		ExistingSummary.bCanDelete = Existing->bCanDelete;
		UpdatedPlans.Add(MoveTemp(ExistingSummary));
	}
	UpdatedPlans.Add(Summary);
	PopulateSharedPlans(UpdatedPlans, Summary.FileName);
	StatusText = BuildPlanStatusText(
		*Plan,
		TOptional<double>(),
		FString::Printf(
			TEXT("Multiplayer-Plan „%s“ synchronisiert (Revision %lld)"),
			*PlanNameText,
			static_cast<long long>(Summary.Revision)));
}

void SSFPPlannerWindow::ReceiveSharedPlanSaveResult(
	const bool bSuccess,
	const FSFPSharedPlanSummary& Summary,
	const FString& Error)
{
	const bool bWasAutomatic = bAutomaticSharedProgressSave;
	bSharedPlanRequestPending = false;
	bAutomaticSharedProgressSave = false;
	if (!bSuccess)
	{
		bSharedProgressDirty = false;
		if (!ActiveSharedPlanFileName.IsEmpty())
		{
			bSharedPlanContentDirty = true;
		}
		StatusText = FString::Printf(
			TEXT("Multiplayer-Plan speichern fehlgeschlagen: %s"),
			*Error);
		return;
	}

	ActiveSharedPlanFileName = Summary.FileName;
	ActiveSharedPlanRevision = Summary.Revision;
	IgnoreNextSharedChangeFileName = Summary.FileName;
	PlanNameText = Summary.Name;
	if (PlanNameInput.IsValid())
	{
		PlanNameInput->SetText(FText::FromString(PlanNameText));
	}
	StatusText = bWasAutomatic
		? (SFPLocalization::IsGerman()
			? FString::Printf(TEXT("Baufortschritt mit Multiplayer-Plan „%s“ synchronisiert"), *Summary.Name)
			: FString::Printf(TEXT("Build progress synchronized with multiplayer plan “%s”"), *Summary.Name))
		: (SFPLocalization::IsGerman()
			? FString::Printf(TEXT("Multiplayer-Plan „%s“ gespeichert"), *Summary.Name)
			: FString::Printf(TEXT("Multiplayer plan “%s” saved"), *Summary.Name));

	if (bSharedProgressDirty)
	{
		bSharedProgressDirty = false;
		QueueSharedPlanSave(Summary.Name, Summary.Revision, true);
	}
}

void SSFPPlannerWindow::ReceiveSharedPlanDeleteResult(
	const bool bSuccess,
	const FString& FileName,
	const FString& PlanName,
	const FString& Error)
{
	bSharedPlanRequestPending = false;
	if (!bSuccess)
	{
		StatusText = FString::Printf(TEXT("Multiplayer-Plan löschen fehlgeschlagen: %s"), *Error);
		return;
	}
	StatusText = SFPLocalization::IsGerman()
		? FString::Printf(TEXT("Multiplayer-Plan „%s“ wurde gelöscht"), *PlanName)
		: FString::Printf(TEXT("Multiplayer plan “%s” was deleted"), *PlanName);
}

void SSFPPlannerWindow::NotifySharedPlanChanged(const FSFPSharedPlanSummary& Summary)
{
	if (!bSharedPlanMode)
	{
		return;
	}
	TArray<FSFPSharedPlanSummary> UpdatedPlans;
	UpdatedPlans.Reserve(SavedPlans.Num() + 1);
	for (const TSharedPtr<FSFPSavedPlanInfo>& Existing : SavedPlans)
	{
		if (!Existing.IsValid() || Existing->FileName == Summary.FileName) continue;
		FSFPSharedPlanSummary ExistingSummary;
		ExistingSummary.Name = Existing->Name;
		ExistingSummary.FileName = Existing->FileName;
		ExistingSummary.TargetName = Existing->TargetName;
		ExistingSummary.TargetRatePerMinute = Existing->TargetRatePerMinute;
		ExistingSummary.TargetCount = Existing->TargetCount;
		ExistingSummary.bPowerProductionPlan = Existing->bPowerProductionPlan;
		ExistingSummary.Revision = Existing->Revision;
		ExistingSummary.OwnerName = Existing->OwnerName;
		ExistingSummary.UpdatedBy = Existing->UpdatedBy;
		ExistingSummary.bCanDelete = Existing->bCanDelete;
		UpdatedPlans.Add(MoveTemp(ExistingSummary));
	}
	UpdatedPlans.Add(Summary);
	const FString FileToSelect = ActiveSharedPlanFileName == Summary.FileName
		? Summary.FileName
		: (SelectedSavedPlan.IsValid() ? SelectedSavedPlan->FileName : FString());
	PopulateSharedPlans(UpdatedPlans, FileToSelect);

	if (IgnoreNextSharedChangeFileName == Summary.FileName)
	{
		IgnoreNextSharedChangeFileName.Reset();
		ActiveSharedPlanRevision = Summary.Revision;
		return;
	}
	if (ActiveSharedPlanFileName == Summary.FileName && bSharedPlanContentDirty)
	{
		const FString EditorName = Summary.UpdatedBy.IsEmpty()
			? SFPLocalization::Select(TEXT("einem Spieler"), TEXT("another player"))
			: Summary.UpdatedBy;
		StatusText = SFPLocalization::IsGerman()
			? FString::Printf(
				TEXT("Multiplayer-Plan „%s“ wurde von %s geändert; lokale Änderungen bleiben erhalten — neu laden oder unter neuem Namen speichern"),
				*Summary.Name,
				*EditorName)
			: FString::Printf(
				TEXT("Multiplayer plan “%s” was changed by %s; local changes are preserved — reload or save under a new name"),
				*Summary.Name,
				*EditorName);
		return;
	}
	if (ActiveSharedPlanFileName == Summary.FileName && !bSharedPlanRequestPending)
	{
		FString RequestError;
		const FString EditorName = Summary.UpdatedBy.IsEmpty()
			? SFPLocalization::Select(TEXT("Ein Spieler"), TEXT("A player"))
			: Summary.UpdatedBy;
		StatusText = SFPLocalization::IsGerman()
			? FString::Printf(
				TEXT("%s aktualisierte den Multiplayer-Plan „%s“; neuer Stand wird geladen …"),
				*EditorName,
				*Summary.Name)
			: FString::Printf(
				TEXT("%s updated multiplayer plan “%s”; loading the new revision …"),
				*EditorName,
				*Summary.Name);
		bSharedPlanRequestPending = true;
		if (!USFPPlannerRemoteCallObject::RequestSharedPlanDownload(
			PlayerController.Get(),
			Summary.FileName,
			RequestError))
		{
			bSharedPlanRequestPending = false;
			StatusText = FString::Printf(TEXT("Multiplayer-Plan-Aktualisierung fehlgeschlagen: %s"), *RequestError);
		}
		return;
	}
	const FString EditorName = Summary.UpdatedBy.IsEmpty()
		? SFPLocalization::Select(TEXT("einem Spieler"), TEXT("another player"))
		: Summary.UpdatedBy;
	StatusText = SFPLocalization::IsGerman()
		? FString::Printf(TEXT("Multiplayer-Plan „%s“ wurde von %s aktualisiert"), *Summary.Name, *EditorName)
		: FString::Printf(TEXT("Multiplayer plan “%s” was updated by %s"), *Summary.Name, *EditorName);
}

void SSFPPlannerWindow::NotifySharedPlanDeleted(
	const FString& FileName,
	const FString& PlanName,
	const FString& DeletedBy)
{
	if (!bSharedPlanMode)
	{
		return;
	}
	if (SavedPlanCombo.IsValid())
	{
		SavedPlanCombo->ClearSelection();
	}
	SavedPlans.RemoveAll([&FileName](const TSharedPtr<FSFPSavedPlanInfo>& Plan)
	{
		return Plan.IsValid() && Plan->FileName == FileName;
	});
	SelectedSavedPlan.Reset();
	if (SavedPlanCombo.IsValid())
	{
		SavedPlanCombo->RefreshOptions();
	}
	if (ActiveSharedPlanFileName == FileName)
	{
		ActiveSharedPlanFileName.Reset();
		ActiveSharedPlanRevision = 0;
	}
	const FString EditorName = DeletedBy.IsEmpty()
		? SFPLocalization::Select(TEXT("einem Spieler"), TEXT("another player"))
		: DeletedBy;
	StatusText = SFPLocalization::IsGerman()
		? FString::Printf(
			TEXT("Multiplayer-Plan „%s“ wurde von %s gelöscht; der aktuelle Graph bleibt geöffnet"),
			*PlanName,
			*EditorName)
		: FString::Printf(
			TEXT("Multiplayer plan “%s” was deleted by %s; the current graph remains open"),
			*PlanName,
			*EditorName);
}

void SSFPPlannerWindow::ApplyPlanToUI(const TSharedPtr<FSFPPlanResult>& Plan)
{
	if (!Plan.IsValid())
	{
		return;
	}

	MachineGuidanceState = 0;
	CurrentPlan = Plan;
	if (Plan->bPowerProductionPlan)
	{
		bUseCurrentFactoryPowerDemand = false;
	}
	else
	{
		LastFactoryPowerDemandMW = FMath::Max(0.0, Plan->TotalBasePowerMW);
	}
	bCarryCurrentPlanProgress = true;
	bOnlyAvailable = Plan->bOnlyAvailableRecipes;
	RefreshProducts();
	RefreshTransportChoices(
		Plan->SelectedConveyorClassPath,
		Plan->SelectedConveyorLiftClassPath);
	EstimatedConnectionLengthMeters = FMath::Clamp(Plan->EstimatedConnectionLengthMeters, 0.5, 1000.0);
	PowerTargetNetMW = FMath::Max(0.1, Plan->RequestedNetPowerMW > 0.0
		? Plan->RequestedNetPowerMW
		: 1000.0);
	PowerReservePercent = FMath::Clamp(Plan->PowerReservePercent, 0.0, 500.0);
	PowerGeneratorClockPercent = FMath::Clamp(
		Plan->ConfiguredGeneratorClockPercent > 0.0 ? Plan->ConfiguredGeneratorClockPercent : 100.0,
		1.0,
		100000.0);
	PassiveAlienPowerAugmenters = FMath::Max(0, Plan->PassiveAlienPowerAugmenters);
	FueledAlienPowerAugmenters = FMath::Max(0, Plan->FueledAlienPowerAugmenters);
	RefreshPowerChoices(
		Plan->RequestedGeneratorClassPath,
		Plan->RequestedFuelClassPath);
	const TOptional<double> LoadedMinClock = GetPowerGeneratorMinClockPercent();
	const TOptional<double> LoadedMaxClock = GetPowerGeneratorMaxClockPercent();
	PowerGeneratorClockPercent = FMath::Clamp(
		PowerGeneratorClockPercent,
		LoadedMinClock.IsSet() ? LoadedMinClock.GetValue() : 1.0,
		LoadedMaxClock.IsSet() ? LoadedMaxClock.GetValue() : 250.0);
	RecipeOverrides = Plan->RecipeOverrides;
	MachineSettingsOverrides = Plan->MachineSettings;
	ResourceSourceMixes = Plan->ResourceSourceMixes;
    bInputPlanning = Plan->bInputPlanning;
    SelectedSupplies.Reset();
    for (const auto& Supply : Plan->Supplies) SelectedSupplies.Add(MakeShared<FSFPPlanSupply>(Supply));
    RefreshSupplyRows();
	SelectedTargets.Reset();
	if (Plan->bPowerProductionPlan)
	{
		SelectedProduct.Reset();
		if (ProductList.IsValid())
		{
			ProductList->ClearSelection();
		}
		if (TargetList.IsValid())
		{
			TargetList->RequestListRefresh();
		}
		if (GraphPanel.IsValid())
		{
			GraphPanel->SetPlan(Plan);
		}
		RefreshRecipeChoices(Plan);
		RefreshInputBudgets(Plan, false);
		ActiveTabIndex = PowerTabIndex;
		if (TabSwitcher.IsValid())
		{
			TabSwitcher->SetActiveWidgetIndex(ActiveTabIndex);
		}
		return;
	}
	TArray<FSFPPlanTarget> RestoredTargets = bInputPlanning ? Plan->InputPlanningTargets : Plan->Targets;
	if (RestoredTargets.IsEmpty())
	{
		for (const FSFPPlanNode& Node : Plan->Nodes)
		{
			if (Node.Type != ESFPPlanNodeType::Target)
			{
				continue;
			}
			FSFPPlanTarget Target;
			Target.ItemClassPath = Node.ClassPath;
			Target.DisplayName = Node.Title;
			Target.RatePerMinute = Node.RatePerMinute > 0.0
				? Node.RatePerMinute
				: Plan->TargetRatePerMinute;
			RestoredTargets.Add(MoveTemp(Target));
		}
	}

	for (const FSFPPlanTarget& SavedTarget : RestoredTargets)
	{
		const TSharedPtr<FSFPProductOption>* SavedProduct = Solver->GetProducts().FindByPredicate(
			[&SavedTarget](const TSharedPtr<FSFPProductOption>& Product)
			{
				return Product.IsValid() && Product->ClassPath == SavedTarget.ItemClassPath;
			});
		if (SavedProduct != nullptr)
		{
			TSharedPtr<FSFPSelectedTarget> TargetRow = MakeShared<FSFPSelectedTarget>();
			TargetRow->Product = *SavedProduct;
			TargetRow->RatePerMinute = FMath::Max(0.0, SavedTarget.RatePerMinute);
            TargetRow->bFixed = Plan->FixedOutputRates.Contains(SavedTarget.ItemClassPath);
            if (TargetRow->bFixed) TargetRow->RatePerMinute = Plan->FixedOutputRates[SavedTarget.ItemClassPath];
			SelectedTargets.Add(MoveTemp(TargetRow));
		}
	}
	if (TargetList.IsValid())
	{
		TargetList->RequestListRefresh();
	}
	if (!SelectedTargets.IsEmpty() && SelectedTargets[0].IsValid())
	{
		SelectedProduct = SelectedTargets[0]->Product;
		TargetRate = SelectedTargets[0]->RatePerMinute;
		if (ProductList.IsValid() && FilteredProducts.Contains(SelectedProduct))
		{
			ProductList->SetSelection(SelectedProduct);
		}
	}
	else
	{
		TargetRate = FMath::Max(0.001, Plan->TargetRatePerMinute);
		Plan->Warnings.AddUnique(TEXT("Mindestens ein gespeichertes Endprodukt ist im aktuellen Rezeptkatalog nicht mehr vorhanden"));
	}
	if (GraphPanel.IsValid())
	{
		GraphPanel->SetPlan(Plan);
	}
	RefreshRecipeChoices(Plan);
	RefreshInputBudgets(Plan, false);
}

void SSFPPlannerWindow::RestoreLastPlan()
{
	FString Path;
	FString Error;
	TSharedPtr<FSFPPlanResult> SavedPlan = FSFPPlannerPersistence::LoadLastPlan(Path, Error);
	if (!SavedPlan.IsValid())
	{
		if (!Error.IsEmpty())
		{
			StatusText = FString::Printf(TEXT("Gespeicherter Plan konnte nicht geladen werden: %s"), *Error);
		}
		return;
	}

	ApplyPlanToUI(SavedPlan);
	StatusText = BuildPlanStatusText(*SavedPlan, TOptional<double>(), TEXT("Gespeicherter Plan wiederhergestellt"));
}

bool SSFPPlannerWindow::PersistCurrentPlan(const bool bShowStatus)
{
	if (!CurrentPlan.IsValid())
	{
		if (bShowStatus)
		{
			StatusText = TEXT("Zuerst einen Produktionsplan berechnen");
		}
		return false;
	}
	CaptureInputBudgetsToPlan();

	FString Path;
	FString Error;
	if (!FSFPPlannerPersistence::SaveLastPlan(*CurrentPlan, Path, Error))
	{
		if (bShowStatus)
		{
			StatusText = FString::Printf(TEXT("Speichern fehlgeschlagen: %s"), *Error);
		}
		return false;
	}
	if (bShowStatus)
	{
		StatusText = FString::Printf(TEXT("Produktionsplan gespeichert: %s"), *Path);
	}
	return true;
}

FReply SSFPPlannerWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bCapturingPlannerHotkey)
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			bCapturingPlannerHotkey = false;
			SFPPlannerHotkey::SetCaptureInProgress(false);
			StatusText = TEXT("Hotkey-Änderung abgebrochen");
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::BackSpace || InKeyEvent.GetKey() == EKeys::Delete)
		{
			return HandleDisableHotkey();
		}

		FSFPPlannerHotkeyBinding Binding;
		Binding.Key = InKeyEvent.GetKey();
		Binding.bShift = InKeyEvent.IsShiftDown();
		Binding.bControl = InKeyEvent.IsControlDown();
		Binding.bAlt = InKeyEvent.IsAltDown();
		Binding.bCommand = InKeyEvent.IsCommandDown();

		FString Error;
		if (!SFPPlannerHotkey::Set(Binding, Error))
		{
			StatusText = FString::Printf(TEXT("Hotkey konnte nicht gespeichert werden: %s"), *Error);
			return FReply::Handled();
		}

		bCapturingPlannerHotkey = false;
		SFPPlannerHotkey::SetCaptureInProgress(false);
		StatusText = FString::Printf(
			TEXT("Planner-Hotkey gespeichert: %s"),
			*SFPPlannerHotkey::GetDisplayName());
		return FReply::Handled();
	}

	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		return HandleClose();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}
