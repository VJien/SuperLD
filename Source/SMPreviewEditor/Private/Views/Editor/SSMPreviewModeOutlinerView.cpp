// Copyright Recursoft LLC 2019-2021. All Rights Reserved.

#include "SSMPreviewModeOutlinerView.h"
#include "Views/Viewport/SMPreviewModeViewportClient.h"
#include "SMPreviewObject.h"
#include "Utilities/SMPreviewUtils.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Blueprints/SMBlueprint.h"

#include "ActorTreeItem.h"
#include "EditorStyle.h"
#include "ICustomSceneOutliner.h"
#include "ISceneOutlinerColumn.h"
#include "SceneOutlinerModule.h"


#define LOCTEXT_NAMESPACE "SSMPreviewModeOutlinerView"

using namespace SceneOutliner;

class FPreviewModeOutlinerContextColumn : public ISceneOutlinerColumn
{
public:
	FPreviewModeOutlinerContextColumn(ISceneOutliner& Outliner, USMPreviewObject* InPreviewObject)
	{
		WeakPreviewObject = InPreviewObject;
		WeakOutliner = StaticCastSharedRef<ISceneOutliner>(Outliner.AsShared());
	}

	virtual ~FPreviewModeOutlinerContextColumn() {}

	static FName GetID() { return FName("Context"); }

	// ISceneOutlinerColumn
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FTreeItemRef TreeItem, const STableRow<FTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return true; }
	virtual void SortItems(TArray<FTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const override;
	// ~ISceneOutlinerColumn

protected:
	bool IsColumnEnabled() const
	{
		if (WeakPreviewObject.IsValid())
		{
			return !WeakPreviewObject.Get()->IsSimulationRunning();
		}

		return false;
	}

	bool IsTreeItemContext(const FTreeItemPtr& TreeItem) const
	{
		const TWeakPtr<FActorTreeItem> ActorItem = StaticCastSharedRef<FActorTreeItem>(TreeItem.ToSharedRef());
		if (ActorItem.IsValid() && ActorItem.Pin()->Actor.IsValid() && WeakPreviewObject.IsValid())
		{
			return WeakPreviewObject.Get()->GetContextActor() == ActorItem.Pin()->Actor.Get();
		}

		return false;
	}

private:
	TWeakPtr<ISceneOutliner> WeakOutliner;
	TWeakObjectPtr<USMPreviewObject> WeakPreviewObject;
};

SHeaderRow::FColumn::FArguments FPreviewModeOutlinerContextColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
	       .FillWidth(1.1f)
	       .DefaultLabel(LOCTEXT("ItemLabel_HeaderText", "Context"))
	       .DefaultTooltip(LOCTEXT("ItemLabel_TooltipText", "Set the actor as the state machine context."));
}

const TSharedRef<SWidget> FPreviewModeOutlinerContextColumn::ConstructRowWidget(FTreeItemRef TreeItem,
	const STableRow<FTreeItemPtr>& Row)
{
	const TWeakPtr<FActorTreeItem> ActorItem = StaticCastSharedRef<FActorTreeItem>(TreeItem);
	if (!ActorItem.IsValid() || !ActorItem.Pin()->Actor.IsValid() || ActorItem.Pin()->Actor->IsA<UWorld>())
	{
		return SNullWidget::NullWidget;
	}
	
	auto IsChecked = [this, ActorItem]() -> ECheckBoxState
	{
		if (WeakPreviewObject.IsValid() && ActorItem.IsValid())
		{
			return WeakPreviewObject.Get()->GetContextActor() == ActorItem.Pin()->Actor.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
		
		return ECheckBoxState::Unchecked;
	};
	
	auto OnCheckChanged = [this, ActorItem](ECheckBoxState NewState)
	{
		if (IsColumnEnabled())
		{
			if (WeakPreviewObject.IsValid() && ActorItem.IsValid())
			{
				AActor* ActorToSet = NewState == ECheckBoxState::Checked ? ActorItem.Pin()->Actor.Get() : nullptr;
				WeakPreviewObject.Get()->SetContextActor(ActorToSet);
			}
		}
	};
	
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SCheckBox)
			.IsEnabled(this, &FPreviewModeOutlinerContextColumn::IsColumnEnabled)
			.IsChecked(MakeAttributeLambda(IsChecked))
			.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda(OnCheckChanged))
		];
}

void FPreviewModeOutlinerContextColumn::SortItems(TArray<FTreeItemPtr>& RootItems,
	const EColumnSortMode::Type SortMode) const
{
	RootItems.Sort([this, SortMode](const FTreeItemPtr& lhs, const FTreeItemPtr& rhs)
	{
		return IsTreeItemContext(SortMode == EColumnSortMode::Ascending ? lhs : rhs);
	});
}

SSMPreviewModeOutlinerView::~SSMPreviewModeOutlinerView()
{
	if (SceneOutliner.IsValid() && SceneOutlinerSelectionChanged.IsValid())
	{
		SceneOutliner->GetOnItemSelectionChanged().Remove(SceneOutlinerSelectionChanged);
	}
}

void SSMPreviewModeOutlinerView::Construct(const FArguments& InArgs, TSharedPtr<FSMBlueprintEditor> InStateMachineEditor, UWorld* InWorld)
{
	check(InStateMachineEditor.IsValid());
	BlueprintEditor = InStateMachineEditor;

	CreateWorldOutliner(InWorld);
}

void SSMPreviewModeOutlinerView::CreateWorldOutliner(UWorld* World)
{
	if (!BlueprintEditor.IsValid())
	{
		// This could be called during a bp editor shutdown sequence.
		return;
	}
	
	USMPreviewObject* PreviewObject = BlueprintEditor.Pin()->GetStateMachineBlueprint()->GetPreviewObject();
	
	auto OutlinerFilterPredicate = [PreviewObject](const AActor* InActor)
	{
		// HACK: Only check preview world actors. Other actors spawned in aren't needed and can crash when selected (such as network manager)
		// This unfortunately prevents user spawned in actors from showing up in the outliner.
		UWorld* PreviewWorld = PreviewObject->GetPreviewWorld();
		return FSMPreviewUtils::DoesWorldContainActor(PreviewWorld, InActor, true);
	};
	
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	FInitializationOptions SceneOutlinerOptions;
	SceneOutlinerOptions.SpecifiedWorldToDisplay = World;
	SceneOutlinerOptions.Filters->AddFilterPredicate(FActorFilterPredicate::CreateLambda(OutlinerFilterPredicate));

	if (SceneOutliner.IsValid() && SceneOutlinerSelectionChanged.IsValid())
	{
		SceneOutliner->GetOnItemSelectionChanged().Remove(SceneOutlinerSelectionChanged);
	}
	
	SceneOutliner.Reset();
	SceneOutlinerSelectionChanged.Reset();

	SceneOutliner = SceneOutlinerModule.CreateCustomSceneOutliner(SceneOutlinerOptions);
	SceneOutlinerSelectionChanged = SceneOutliner->GetOnItemSelectionChanged().AddRaw(this, &SSMPreviewModeOutlinerView::OnOutlinerSelectionChanged);

	FColumnInfo ColumnInfo;
	ColumnInfo.Visibility = EColumnVisibility::Visible;
	ColumnInfo.PriorityIndex = 0; // 10, 20
	ColumnInfo.Factory.BindLambda([PreviewObject](ISceneOutliner& Outliner)
	{
		return TSharedRef<ISceneOutlinerColumn>(MakeShared<FPreviewModeOutlinerContextColumn>(Outliner, PreviewObject));
	});

	SceneOutliner->AddColumn(FPreviewModeOutlinerContextColumn::GetID(), ColumnInfo);
	
	UpdateWidget();
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility | EInvalidateWidgetReason::ChildOrder);
}

void SSMPreviewModeOutlinerView::AddObjectToSelection(UObject* Object)
{
	if (SceneOutliner.IsValid())
	{
		FSMPreviewUtils::DeselectEngineLevelEditor();
		SceneOutliner->ClearSelection();
		SceneOutliner->AddObjectToSelection(Object);
	}
}

void SSMPreviewModeOutlinerView::UpdateWidget()
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		[
			SceneOutliner.ToSharedRef()
		]
	];
}

void SSMPreviewModeOutlinerView::OnOutlinerSelectionChanged(SceneOutliner::FTreeItemPtr TreeItem,
                                                            ESelectInfo::Type Type)
{
	FSMPreviewUtils::DeselectEngineLevelEditor();
	
	using namespace SceneOutliner;
	check(BlueprintEditor.IsValid());

	FSMBlueprintEditor* BPEditor = BlueprintEditor.Pin().Get();

	const TWeakPtr<FSMPreviewModeViewportClient> PreviewClient = StaticCastSharedPtr<FSMPreviewModeViewportClient>(BPEditor->GetPreviewClient().Pin());
	if (!PreviewClient.IsValid())
	{
		return;
	}

	TSharedPtr<FActorTreeItem> ActorItem = StaticCastSharedPtr<FActorTreeItem>(TreeItem);
	AActor* ActorSelected = nullptr;
	if (ActorItem.IsValid())
	{
		ActorSelected = ActorItem->Actor.Get();
	}

	PreviewClient.Pin()->SelectActor(ActorSelected);
}

#undef LOCTEXT_NAMESPACE
