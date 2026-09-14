#include "SSFPPlannerWindow.h"
#include "SFPPlannerSolver.h"
#include "SFPInputCapacitySearch.h"
#include "SFPLocalization.h"
#include "SFPNumberFormatting.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> SSFPPlannerWindow::BuildInputPlanningControls()
{
    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
    [ SNew(SCheckBox)
      .IsChecked_Lambda([this]() { return bInputPlanning ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
      .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bInputPlanning = State == ECheckBoxState::Checked; bCarryCurrentPlanProgress = false; })
      [ SNew(STextBlock).Text(SFPLocalization::Text(TEXT("Aus vorhandenen Eingängen planen"))) ] ]
    + SVerticalBox::Slot().AutoHeight()
    [ SNew(SVerticalBox).Visibility_Lambda([this]() { return bInputPlanning ? EVisibility::Visible : EVisibility::Collapsed; })
      + SVerticalBox::Slot().AutoHeight().Padding(0, 3)
      [ SNew(STextBlock).AutoWrapText(true).Text(SFPLocalization::Text(TEXT("Freie Ausgaben wachsen gleichmäßig. Eine Mengenänderung setzt einen festen Wunsch. Eingänge begrenzen die Berechnung; weitere Rohstoffe werden ausgewiesen."))) ]
      + SVerticalBox::Slot().AutoHeight()
      [ SNew(SButton).Text(SFPLocalization::Text(TEXT("Markiertes Produkt als Eingang hinzufügen")))
        .OnClicked_Lambda([this]() {
            if (!SelectedProduct.IsValid() || SelectedSupplies.Num() >= 32) return FReply::Handled();
            auto Supply = MakeShared<FSFPPlanSupply>();
            Supply->ItemClassPath = SelectedProduct->ClassPath;
            Supply->DisplayName = SelectedProduct->DisplayName; Supply->Form = SelectedProduct->Form;
            Supply->RatePerLine = FMath::Max(0.0, TargetRate);
            SelectedSupplies.Add(Supply); bCarryCurrentPlanProgress = false; RefreshSupplyRows();
            return FReply::Handled();
        }) ]
      + SVerticalBox::Slot().AutoHeight()
      [ SNew(SBox).MaxDesiredHeight(140)
        [ SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(SupplyRows, SVerticalBox)] ] ]
    ];
}

void SSFPPlannerWindow::RefreshSupplyRows()
{
    if (!SupplyRows.IsValid()) return;
    SupplyRows->ClearChildren();
    for (const auto& Supply : SelectedSupplies)
    {
        SupplyRows->AddSlot().AutoHeight().Padding(0, 3)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1)
          [ SNew(STextBlock).Text(FText::FromString(Supply->DisplayName)) ]
          + SHorizontalBox::Slot().AutoWidth()
          [ SNew(SBox).WidthOverride(50)[SNew(SNumericEntryBox<int32>).MinValue(1).MaxValue(1000)
            .Value_Lambda([Supply]() -> TOptional<int32> { return Supply->Lines; })
            .OnValueChanged_Lambda([this, Supply](int32 Value) { Supply->Lines = FMath::Clamp(Value, 1, 1000); bCarryCurrentPlanProgress = false; })] ]
          + SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT(" × ")))]
          + SHorizontalBox::Slot().AutoWidth()
          [ SNew(SBox).WidthOverride(90)[SNew(SNumericEntryBox<double>).MinValue(0).MaxValue(1000000)
            .Value_Lambda([Supply]() -> TOptional<double> { return Supply->RatePerLine; })
            .OnValueChanged_Lambda([this, Supply](double Value) { if (FMath::IsFinite(Value)) Supply->RatePerLine = FMath::Clamp(Value, 0.0, 1000000.0); bCarryCurrentPlanProgress = false; })] ]
          + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
          [ SNew(STextBlock).Text_Lambda([Supply]() { return FText::FromString(TEXT("/min = ") + FSFPNumberFormatting::Decimal(Supply->Lines * Supply->RatePerLine)); }) ]
          + SHorizontalBox::Slot().AutoWidth()
          [ SNew(SButton).Text(FText::FromString(TEXT("×"))).OnClicked_Lambda([this, Supply]() { SelectedSupplies.Remove(Supply); bCarryCurrentPlanProgress = false; RefreshSupplyRows(); return FReply::Handled(); }) ]
        ];
    }
}

void SSFPPlannerWindow::CaptureInputPlanning(FSFPPlanResult& Plan) const
{
    Plan.bInputPlanning = bInputPlanning;
    Plan.Supplies.Reset(); Plan.FixedOutputRates.Reset(); Plan.InputPlanningTargets.Reset();
    for (const auto& Supply : SelectedSupplies) Plan.Supplies.Add(*Supply);
    for (const auto& Row : SelectedTargets)
    {
        if (!Row.IsValid() || !Row->Product.IsValid()) continue;
        FSFPPlanTarget Target;
        Target.ItemClassPath = Row->Product->ClassPath; Target.ItemClass = Row->Product->ItemClass;
        Target.DisplayName = Row->Product->DisplayName; Target.Form = Row->Product->Form;
        Target.RatePerMinute = Row->RatePerMinute;
        Plan.InputPlanningTargets.Add(Target);
        if (Row->bFixed) Plan.FixedOutputRates.Add(Target.ItemClassPath, Row->RatePerMinute);
    }
}

TSharedPtr<FSFPPlanResult> SSFPPlannerWindow::SolveAvailableInputs(const TArray<FSFPPlanTarget>& Targets)
{
    auto Failure = [](const FString& Message) { auto P = MakeShared<FSFPPlanResult>(); P->ErrorMessage = Message; return P; };
    if (SelectedSupplies.IsEmpty()) return Failure(TEXT("Mindestens einen vorhandenen Eingang hinzufügen."));
    TMap<FString, double> Limits;
    for (const auto& Supply : SelectedSupplies)
    {
        if (Supply->Lines < 1 || Supply->Lines > 1000 || !FMath::IsFinite(Supply->RatePerLine) || Supply->RatePerLine < 0 || Supply->RatePerLine > 1000000)
            return Failure(TEXT("Ungültige Eingangsmenge."));
        Limits.FindOrAdd(Supply->ItemClassPath) += Supply->Lines * Supply->RatePerLine;
    }
    TMap<FString, double> Fixed;
    int32 FreeCount = 0;
    for (const auto& Row : SelectedTargets)
    {
        if (!FMath::IsFinite(Row->RatePerMinute) || Row->RatePerMinute < 0 || Row->RatePerMinute > 1000000) return Failure(TEXT("Ungültige Zielmenge."));
        if (Row->bFixed) Fixed.Add(Row->Product->ClassPath, Row->RatePerMinute); else ++FreeCount;
    }
    auto Evaluate = [&](double FreeRate, bool Enforce) {
        TArray<FSFPPlanTarget> Trial;
        for (auto Target : Targets)
        {
            const double* Wish = Fixed.Find(Target.ItemClassPath);
            Target.RatePerMinute = Wish ? *Wish : FreeRate;
            if (Target.RatePerMinute > 1e-7) Trial.Add(Target);
        }
        if (Trial.IsEmpty()) { auto P = MakeShared<FSFPPlanResult>(); P->bSuccess = true; return P; }
        return MakeShared<FSFPPlanResult>(Solver->Solve(Trial, bOnlyAvailable, RecipeOverrides,
            EstimatedConnectionLengthMeters, SelectedConveyor->Tier.ClassPath, SelectedConveyorLift->Tier.ClassPath, Limits, Enforce, MachineSettingsOverrides));
    };
    // Capacity planning must be driven by the material that is actually
    // withdrawn from the selected input nodes. The solver also has LP supply
    // constraints, but recipe/byproduct balancing may choose a mathematically
    // equivalent activity scaling that makes a pure success/failure probe a
    // poor capacity oracle. Measure the balanced source withdrawal directly
    // and binary-search against the user's entered limits instead.
    auto CollectSelectedInputUsage = [&](const FSFPPlanResult& Plan)
    {
        TMap<FString, double> Usage;
        for (const FSFPPlanNode& Node : Plan.Nodes)
        {
            if ((Node.Type == ESFPPlanNodeType::Source || Node.Type == ESFPPlanNodeType::Cycle)
                && Limits.Contains(Node.ClassPath)
                && Node.RatePerMinute > 0.0)
            {
                Usage.FindOrAdd(Node.ClassPath) += Node.RatePerMinute;
            }
        }
        return Usage;
    };
    auto FitsSelectedInputs = [&](const FSFPPlanResult& Plan)
    {
        const TMap<FString, double> Usage = CollectSelectedInputUsage(Plan);
        for (const auto& Limit : Limits)
        {
            const double Allowed = FMath::Max(0.0, Limit.Value);
            const double Tolerance = FMath::Max(1e-5, Allowed * 1e-7);
            if (Usage.FindRef(Limit.Key) > Allowed + Tolerance)
            {
                return false;
            }
        }
        return true;
    };

    // Build the zero-free-output baseline without clamping it first. This lets
    // us distinguish an actually impossible recipe graph from fixed wishes
    // that merely exceed one of the selected input capacities.
    auto Best = Evaluate(0, false);
    if (!Best->bSuccess) return Best;
    bool FixedInfeasible = !FitsSelectedInputs(*Best);
    double Low = 0;
    if (FixedInfeasible)
    {
        Best->bInputFeasible = false;
    }
    else if (FreeCount > 0)
    {
        const auto Search = SFPInputCapacitySearch::Maximize([&](double Rate) {
            auto Probe = Evaluate(Rate, false);
            if (!Probe->bSuccess || !FitsSelectedInputs(*Probe)) return false;
            // This plan is capacity-checked from its actual balanced source
            // withdrawals, so mark it as supply-feasible for persistence/UI.
            Probe->bEnforceSupplyLimits = true;
            Probe->bInputFeasible = true;
            Best = Probe;
            return true;
        });
        if (!Search.Bounded)
        {
            // A genuinely unbounded result now means the selected inputs are
            // not consumed by the chosen free-output chains. Keep a finite
            // diagnostic graph rather than pretending that the entered input
            // can determine an output it is not connected to.
            TArray<FSFPPlanTarget> DiagnosticTargets = Targets;
            for (FSFPPlanTarget& Target : DiagnosticTargets)
            {
                if (const double* Wish = Fixed.Find(Target.ItemClassPath))
                {
                    Target.RatePerMinute = *Wish;
                }
            }
            Best = MakeShared<FSFPPlanResult>(Solver->Solve(
                DiagnosticTargets,
                bOnlyAvailable,
                RecipeOverrides,
                EstimatedConnectionLengthMeters,
                SelectedConveyor->Tier.ClassPath,
                SelectedConveyorLift->Tier.ClassPath,
                Limits,
                false,
                MachineSettingsOverrides));
            if (!Best->bSuccess) return Best;
            Best->bInputFeasible = false;
            Best->ErrorMessage = TEXT("NICHT BEGRENZT: Die gewählten freien Ausgaben verbrauchen keinen der eingetragenen Eingänge. Der Graph zeigt die aktuell gewählten Rezeptketten und ihre externen Quellen. Rezeptwahl prüfen oder einen Eingang wählen, der in diesen Ketten tatsächlich verbraucht wird.");
            CaptureInputPlanning(*Best);
            return Best;
        }
        Low = Search.Rate;
    }
    if (Best->Targets.IsEmpty()) return Failure(TEXT("Mit diesen Eingängen ist keine positive Ausgabe möglich. Eingänge oder Rezeptwahl anpassen."));
    Best->ErrorMessage = FixedInfeasible
        ? TEXT("NICHT ERFÜLLBAR: Feste Wünsche überschreiten die Eingänge. Der Graph zeigt den Bedarf, nicht eine versorgte Fabrik.")
        : FreeCount > 0
            ? TEXT("Berechnet für die gewählten Rezeptketten. Freie Ausgaben wurden gemeinsam maximiert.")
            : TEXT("Berechnet: Alle Ausgaben sind fest vorgegeben; die vorhandenen Eingänge wurden nur auf Machbarkeit geprüft.");
    TMap<FString, double> Used;
    for (const auto& Node : Best->Nodes)
        if (Node.Type == ESFPPlanNodeType::Source || Node.Type == ESFPPlanNodeType::Cycle)
        {
            Used.FindOrAdd(Node.ClassPath) += Node.RatePerMinute;
            if (!Limits.Contains(Node.ClassPath) && Node.RatePerMinute > 1e-6)
                Best->ErrorMessage += TEXT("\n") + SFPLocalization::Text(TEXT("Zusätzlich erforderlich")).ToString() + TEXT(": ") + Node.Title + TEXT(" ") + FSFPNumberFormatting::Decimal(Node.RatePerMinute) + TEXT("/min");
        }
    for (const auto& Limit : Limits)
    {
        FString Name = Limit.Key;
        for (const auto& Supply : SelectedSupplies) if (Supply->ItemClassPath == Limit.Key) { Name = Supply->DisplayName; break; }
        const double Consumption = Used.FindRef(Limit.Key);
        Best->ErrorMessage += TEXT("\n") + Name + TEXT(": ") + FSFPNumberFormatting::Decimal(Consumption) + TEXT(" / ") + FSFPNumberFormatting::Decimal(Limit.Value) + TEXT(" /min; ")
            + SFPLocalization::Text(Consumption > Limit.Value + 1e-5 ? TEXT("Fehlbedarf") : TEXT("Rest")).ToString() + TEXT(": ") + FSFPNumberFormatting::Decimal(FMath::Abs(Limit.Value - Consumption));
    }
    for (const auto& Row : SelectedTargets) if (!Row->bFixed) Row->RatePerMinute = FixedInfeasible ? 0 : Low;
    CaptureInputPlanning(*Best);
    return Best;
}
