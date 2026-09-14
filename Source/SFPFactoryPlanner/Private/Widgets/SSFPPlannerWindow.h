#pragma once

#include "CoreMinimal.h"
#include "SFPPlannerPersistence.h"
#include "SFPPlannerTypes.h"
#include "SFPSharedPlanTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Views/SListView.h"

class AFGPlayerController;
class FSFPPlannerSolver;
class SSFPGraphPanel;
class SWidgetSwitcher;
class SVerticalBox;

struct FSFPInputBudgetOption
{
	FString ItemName;
	FString ItemClassPath;
	double RequiredRatePerMinute = 0.0;
	double AvailableRatePerMinute = 0.0;
};

struct FSFPRecipeChoiceRow
{
	int32 PlanNodeId = INDEX_NONE;
	FString ItemName;
	FString ItemClassPath;
	TArray<TSharedPtr<FSFPRecipeOption>> Options;
	TSharedPtr<FSFPRecipeOption> Selected;
	/** Immediate raw-extraction inputs rendered inside this processing card. */
	TArray<TSharedPtr<FSFPRecipeChoiceRow>> GroupedRawExtractions;
};

struct FSFPSelectedTarget
{
	TSharedPtr<FSFPProductOption> Product;
	double RatePerMinute = 60.0;
	bool bFixed = false;
};

struct FSFPTransportChoice
{
	FSFPTransportTier Tier;
};

class SSFPPlannerWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSFPPlannerWindow) {}
		SLATE_ARGUMENT(AFGPlayerController*, PlayerController)
		SLATE_ARGUMENT(TSharedPtr<FSFPPlannerSolver>, Solver)
		SLATE_ARGUMENT(FString, InitializeError)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	virtual ~SSFPPlannerWindow() override;
	void Construct(const FArguments& InArgs);
	void ReceiveSharedPlanCatalog(const TArray<FSFPSharedPlanSummary>& Plans, const FString& Error);
	void ReceiveSharedPlan(const FSFPSharedPlanSummary& Summary, const FString& PlanJson, const FString& Error);
	void ReceiveSharedPlanSaveResult(bool bSuccess, const FSFPSharedPlanSummary& Summary, const FString& Error);
	void ReceiveSharedPlanDeleteResult(
		bool bSuccess,
		const FString& FileName,
		const FString& PlanName,
		const FString& Error);
	void NotifySharedPlanChanged(const FSFPSharedPlanSummary& Summary);
	void NotifySharedPlanDeleted(
		const FString& FileName,
		const FString& PlanName,
		const FString& DeletedBy);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
    bool bInputPlanning = false;
    bool bShowProductSearch = true;
    TSharedPtr<SVerticalBox> SupplyRows;
    TArray<TSharedPtr<FSFPPlanSupply>> SelectedSupplies;
    TSharedRef<SWidget> BuildInputPlanningControls();
    void RefreshSupplyRows();
    void CaptureInputPlanning(FSFPPlanResult& Plan) const;
    TSharedPtr<FSFPPlanResult> SolveAvailableInputs(const TArray<FSFPPlanTarget>& Targets);
	void RefreshProducts();
	void RefreshTransportChoices(const FString& PreferredConveyorPath = FString(), const FString& PreferredLiftPath = FString());
	void RefreshPowerChoices(
		const FString& PreferredGeneratorPath = FString(),
		const FString& PreferredFuelPath = FString());
	void HandleSearchChanged(const FText& NewText);
	void HandleOnlyAvailableChanged(ECheckBoxState NewState);
	void HandleConveyorSelected(TSharedPtr<FSFPTransportChoice> Choice, ESelectInfo::Type SelectInfo);
	void HandleConveyorLiftSelected(TSharedPtr<FSFPTransportChoice> Choice, ESelectInfo::Type SelectInfo);
	void HandlePowerGeneratorSelected(TSharedPtr<FSFPPowerGeneratorOption> Choice, ESelectInfo::Type SelectInfo);
	void HandlePowerFuelSelected(TSharedPtr<FSFPPowerFuelOption> Choice, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> HandleGenerateTransportWidget(TSharedPtr<FSFPTransportChoice> Choice);
	TSharedRef<SWidget> HandleGeneratePowerGeneratorWidget(TSharedPtr<FSFPPowerGeneratorOption> Choice);
	TSharedRef<SWidget> HandleGeneratePowerFuelWidget(TSharedPtr<FSFPPowerFuelOption> Choice);
	void HandleProductSelected(TSharedPtr<FSFPProductOption> Product, ESelectInfo::Type SelectInfo);
	TSharedRef<ITableRow> HandleGenerateProductRow(
		TSharedPtr<FSFPProductOption> Product,
		const TSharedRef<STableViewBase>& OwnerTable);
	FReply HandleAddTarget();
	FReply HandleRemoveTarget(TSharedPtr<FSFPSelectedTarget> Target);
	TSharedRef<ITableRow> HandleGenerateTargetRow(
		TSharedPtr<FSFPSelectedTarget> Target,
		const TSharedRef<STableViewBase>& OwnerTable);
	FReply HandleCalculate();
	FReply HandleCalculatePower();
	FReply HandleCalculateFromInputs();
	TSharedRef<ITableRow> HandleGenerateInputBudgetRow(
		TSharedPtr<FSFPInputBudgetOption> Input,
		const TSharedRef<STableViewBase>& OwnerTable);
	void CalculateForTargets(double RateScale, bool bPreserveInputBudgets, const FString& StatusPrefix = FString());
	void RefreshInputBudgets(const TSharedPtr<FSFPPlanResult>& Plan, bool bPreserveExistingValues);
	void CaptureInputBudgetsToPlan();
	void RefreshMachineSettings();
	void RefreshRecipeChoices(const TSharedPtr<FSFPPlanResult>& Plan);
	TSharedRef<ITableRow> HandleGenerateRecipeChoiceRow(
		TSharedPtr<FSFPRecipeChoiceRow> Row,
		const TSharedRef<STableViewBase>& OwnerTable);
	FReply HandleResetRecipeChoices();
	FReply HandleSavePlan();
	FReply HandleLoadSavedPlan();
	FReply HandleDeleteSavedPlan();
	FReply HandleTogglePlanScope();
	FReply HandleClose();
	FReply HandleBeginHotkeyCapture();
	FReply HandleDisableHotkey();
	FReply HandleResetGraph();
	FReply HandleAutoArrangeGraph();
	void HandleNodeCompletionChanged(int32 NodeId, bool bCompleted);
	FReply HandleSelectTab(int32 TabIndex);
	TSharedRef<SWidget> BuildPlanManagementBar();
	TSharedRef<SWidget> BuildTabBar();
	TSharedRef<SWidget> BuildTabButton(int32 TabIndex, const FString& Label, const FString& Hint);
	TSharedRef<SWidget> BuildPlanningTab();
	TSharedRef<SWidget> BuildPowerTab();
	TSharedRef<SWidget> BuildMachinesTab();
	TSharedRef<SWidget> BuildResourcesTab();
	TSharedRef<SWidget> BuildGraphTab();
	TOptional<double> GetTargetRate() const;
	void HandleTargetRateChanged(double NewValue);
	TOptional<double> GetPowerTargetNetMW() const;
	void HandlePowerTargetNetMWChanged(double NewValue);
	TOptional<double> GetPowerReservePercent() const;
	void HandlePowerReservePercentChanged(double NewValue);
	TOptional<double> GetPowerGeneratorClockPercent() const;
	TOptional<double> GetPowerGeneratorMinClockPercent() const;
	TOptional<double> GetPowerGeneratorMaxClockPercent() const;
	void HandlePowerGeneratorClockPercentChanged(double NewValue);
	TOptional<int32> GetPassiveAlienPowerAugmenters() const;
	void HandlePassiveAlienPowerAugmentersChanged(int32 NewValue);
	TOptional<int32> GetFueledAlienPowerAugmenters() const;
	void HandleFueledAlienPowerAugmentersChanged(int32 NewValue);
	TOptional<double> GetEstimatedConnectionLength() const;
	void HandleEstimatedConnectionLengthChanged(double NewValue);
	ECheckBoxState GetOnlyAvailableState() const;
	FText GetSelectedConveyorText() const;
	FText GetSelectedConveyorLiftText() const;
	FText GetSelectedPowerGeneratorText() const;
	FText GetSelectedPowerFuelText() const;
	FText GetStatusText() const;
	FText GetPowerSummaryText() const;
	FText GetMachineSummaryText() const;
	FText GetResourceSummaryText() const;
	FText GetGraphProgressText() const;
	FText GetSelectedSavedPlanText() const;
	FText GetPlanScopeText() const;
	FText GetHotkeyButtonText() const;
	void HandlePlanNameChanged(const FText& NewText);
	void HandleSavedPlanSelected(TSharedPtr<FSFPSavedPlanInfo> Plan, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> HandleGenerateSavedPlanWidget(TSharedPtr<FSFPSavedPlanInfo> Plan);
	void RefreshNamedPlans(const FString& SelectFileName = FString());
	void RequestSharedPlanCatalog(const FString& SelectFileName = FString());
	void PopulateSharedPlans(
		const TArray<FSFPSharedPlanSummary>& Plans,
		const FString& SelectFileName = FString());
	bool QueueSharedPlanSave(const FString& Name, int64 ExpectedRevision, bool bAutomaticProgressSave);
	void ApplyPlanToUI(const TSharedPtr<FSFPPlanResult>& Plan);
	void RestoreLastPlan();
	bool PersistCurrentPlan(bool bShowStatus);

	TWeakObjectPtr<AFGPlayerController> PlayerController;
	TSharedPtr<FSFPPlannerSolver> Solver;
	TArray<TSharedPtr<FSFPProductOption>> FilteredProducts;
	TSharedPtr<FSFPProductOption> SelectedProduct;
	TSharedPtr<SListView<TSharedPtr<FSFPProductOption>>> ProductList;
	TArray<TSharedPtr<FSFPSelectedTarget>> SelectedTargets;
	TSharedPtr<SListView<TSharedPtr<FSFPSelectedTarget>>> TargetList;
	TArray<TSharedPtr<FSFPInputBudgetOption>> InputBudgets;
	TSharedPtr<SListView<TSharedPtr<FSFPInputBudgetOption>>> InputBudgetList;
	TArray<TSharedPtr<FSFPRecipeChoiceRow>> RecipeChoiceRows;
	TSharedPtr<SListView<TSharedPtr<FSFPRecipeChoiceRow>>> RecipeChoiceList;
	TSharedPtr<SVerticalBox> MachineSettingsBox;
	int32 MachineGuidanceState = 0; // Idle, needs calculation, calculated, saved.
	TArray<TSharedPtr<FSFPTransportChoice>> ConveyorChoices;
	TArray<TSharedPtr<FSFPTransportChoice>> ConveyorLiftChoices;
	TSharedPtr<FSFPTransportChoice> SelectedConveyor;
	TSharedPtr<FSFPTransportChoice> SelectedConveyorLift;
	TSharedPtr<SComboBox<TSharedPtr<FSFPTransportChoice>>> ConveyorCombo;
	TSharedPtr<SComboBox<TSharedPtr<FSFPTransportChoice>>> ConveyorLiftCombo;
	TArray<TSharedPtr<FSFPPowerGeneratorOption>> PowerGeneratorChoices;
	TArray<TSharedPtr<FSFPPowerFuelOption>> PowerFuelChoices;
	TSharedPtr<FSFPPowerGeneratorOption> SelectedPowerGenerator;
	TSharedPtr<FSFPPowerFuelOption> SelectedPowerFuel;
	TSharedPtr<SComboBox<TSharedPtr<FSFPPowerGeneratorOption>>> PowerGeneratorCombo;
	TSharedPtr<SComboBox<TSharedPtr<FSFPPowerFuelOption>>> PowerFuelCombo;
	TMap<FString, FString> RecipeOverrides;
	TMap<FString, FSFPMachinePlanSettings> MachineSettingsOverrides;
	TArray<TSharedPtr<FSFPSavedPlanInfo>> SavedPlans;
	TSharedPtr<FSFPSavedPlanInfo> SelectedSavedPlan;
	TSharedPtr<SComboBox<TSharedPtr<FSFPSavedPlanInfo>>> SavedPlanCombo;
	TSharedPtr<SEditableTextBox> PlanNameInput;
	TSharedPtr<SSFPGraphPanel> GraphPanel;
	TSharedPtr<SWidgetSwitcher> TabSwitcher;
	TSharedPtr<FSFPPlanResult> CurrentPlan;
	bool bCarryCurrentPlanProgress = true;
	FSimpleDelegate OnClose;
	FString SearchText;
	FString PlanNameText;
	FString StatusText;
	FString PendingSharedPlanSelection;
	FString ActiveSharedPlanFileName;
	FString IgnoreNextSharedChangeFileName;
	int64 ActiveSharedPlanRevision = 0;
	double TargetRate = 60.0;
	double PowerTargetNetMW = 1000.0;
	double PowerReservePercent = 10.0;
	double PowerGeneratorClockPercent = 100.0;
	int32 PassiveAlienPowerAugmenters = 0;
	int32 FueledAlienPowerAugmenters = 0;
	double EstimatedConnectionLengthMeters = 10.0;
	bool bOnlyAvailable = true;
	bool bCapturingPlannerHotkey = false;
	bool bSharedPlanMode = false;
	bool bSharedPlanRequestPending = false;
	bool bAutomaticSharedProgressSave = false;
	bool bSharedProgressDirty = false;
	bool bSharedPlanContentDirty = false;
	int32 ActiveTabIndex = 0;
};
