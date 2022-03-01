// Copyright Recursoft LLC 2019-2021. All Rights Reserved.

#include "SSMPreviewModeEditorView.h"
#include "SMPreviewObject.h"

#include "Utilities/SMPreviewUtils.h"
#include "Views/Viewport/SMPreviewModeViewportClient.h"
#include "SSMPreviewModeOutlinerView.h"
#include "Views/Widgets/SSMAddActorCombo.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Blueprints/SMBlueprintEditorModes.h"

#include "Blueprints/SMBlueprint.h"

#include "EditorStyle.h"
#include "ICustomSceneOutliner.h"
#include "SceneOutlinerPublicTypes.h"
#include "SKismetInspector.h"
#include "Engine/Selection.h"


#define LOCTEXT_NAMESPACE "SSMPreviewModeEditorView"

SSMPreviewModeEditorView::SSMPreviewModeEditorView(): CurrentMode(ESMPreviewModeType::SM_OutlineMode)
{
}

SSMPreviewModeEditorView::~SSMPreviewModeEditorView()
{
	if (Blueprint.IsValid())
	{
		if (BlueprintChangedHandle.IsValid())
		{
			Blueprint->OnChanged().Remove(BlueprintChangedHandle);
		}
		if (PreviewObjectChangedHandle.IsValid())
		{
			Blueprint->GetPreviewObject()->OnPreviewObjectChangedEvent.Remove(PreviewObjectChangedHandle);
		}
		if (PreviewWorldChangedHandle.IsValid())
		{
			Blueprint->GetPreviewObject()->OnCurrentWorldChangedEvent.Remove(PreviewWorldChangedHandle);
		}
	}
	
	if (BlueprintSavedHandle.IsValid())
	{
		UPackage::PackageSavedEvent.Remove(BlueprintSavedHandle);
	}

	if (SelectionChangedHandle.IsValid())
	{
		USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
	}
}

void SSMPreviewModeEditorView::Construct(const FArguments& InArgs, TSharedPtr<FSMBlueprintEditor> InStateMachineEditor, const FName& InTabID)
{
	check(InStateMachineEditor.IsValid());
	BlueprintEditor = InStateMachineEditor;

	Blueprint = MakeWeakObjectPtr<USMBlueprint>(InStateMachineEditor->GetStateMachineBlueprint());

	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FNotifyHook* NotifyHook = this;
	FDetailsViewArgs PreviewDetailsViewArgs;
	PreviewDetailsViewArgs.bUpdatesFromSelection = false;
	PreviewDetailsViewArgs.bLockable = false;
	PreviewDetailsViewArgs.bAllowSearch = true;
	PreviewDetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	PreviewDetailsViewArgs.bHideSelectionTip = true;
	PreviewDetailsViewArgs.NotifyHook = NotifyHook;
	PreviewDetailsViewArgs.bSearchInitialKeyFocus = false;
	PreviewDetailsViewArgs.ViewIdentifier = InTabID;
	PreviewDetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Show;
	PreviewDetailsView = EditModule.CreateDetailView(PreviewDetailsViewArgs);

	SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &SSMPreviewModeEditorView::OnEditorSelectionChanged);
	
	BlueprintChangedHandle = Blueprint->OnChanged().AddRaw(this, &SSMPreviewModeEditorView::OnBlueprintChanged);
	PreviewObjectChangedHandle = Blueprint->GetPreviewObject()->OnPreviewObjectChangedEvent.AddRaw(this, &SSMPreviewModeEditorView::OnPreviewObjectChanged);
	PreviewWorldChangedHandle = Blueprint->GetPreviewObject()->OnCurrentWorldChangedEvent.AddRaw(this, &SSMPreviewModeEditorView::OnPreviewWorldChanged);
	BlueprintSavedHandle = UPackage::PackageSavedEvent.AddRaw(this, &SSMPreviewModeEditorView::OnPackageSaved);
	UpdateSelection();

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(FMargin( 0.f, 0.f, 2.f, 0.f ))
			[
				SNew(SBorder)
				.BorderImage(this, &SSMPreviewModeEditorView::GetBorderBrushByMode, ESMPreviewModeType::SM_OutlineMode)
				.Padding(0)
				[
					SNew(SCheckBox)
					.Style(FEditorStyle::Get(), "RadioButton")
					.IsChecked(this, &SSMPreviewModeEditorView::IsChecked, ESMPreviewModeType::SM_OutlineMode)
					.OnCheckStateChanged(const_cast<SSMPreviewModeEditorView*>(this), &SSMPreviewModeEditorView::OnCheckedChanged, ESMPreviewModeType::SM_OutlineMode)
					[
						SNew( STextBlock )
						.Font( FCoreStyle::GetDefaultFontStyle("Bold", 9) )
						.Text( LOCTEXT("LogicDriverOutlineMode", "Edit World") )
					]
				]
			]
			+SHorizontalBox::Slot()
			.Padding(FMargin( 2.f, 0.f, 0.f, 0.f ))
			[
				SNew(SBorder)
				.BorderImage(this, &SSMPreviewModeEditorView::GetBorderBrushByMode, ESMPreviewModeType::SM_DetailsMode)
				.Padding(0)
				[
					SNew(SCheckBox)
					.Style(FEditorStyle::Get(), "RadioButton")
					.IsChecked(this, &SSMPreviewModeEditorView::IsChecked, ESMPreviewModeType::SM_DetailsMode)
					.OnCheckStateChanged(const_cast<SSMPreviewModeEditorView*>(this), &SSMPreviewModeEditorView::OnCheckedChanged, ESMPreviewModeType::SM_DetailsMode)
					[
						SNew( STextBlock )
						.Font( FCoreStyle::GetDefaultFontStyle("Bold", 9) )
						.Text( LOCTEXT("LogicDriverSimulationMode", "Edit Simulation") )
					]
				]
			]
		]
		+SVerticalBox::Slot()
		[
			SNew(SOverlay)
			+SOverlay::Slot()
			[
				SNew(SBorder)
				.Padding(FMargin(2.0f, 5.0f))
				.BorderImage( FEditorStyle::GetBrush("NoBorder") )
				.Visibility(this, &SSMPreviewModeEditorView::IsEditorVisible, ESMPreviewModeType::SM_OutlineMode)
				[
					/*
					 * Add Actor | Outliner
					 */

					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Left)
					.Padding(0.f, 0.f, 0.f, 2.5f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SSMAddActorCombo, InStateMachineEditor)
							.IsEnabled(this, &SSMPreviewModeEditorView::IsSimulationNotRunning)
							.OnActorSelected(this, &SSMPreviewModeEditorView::OnActorSelectedToSpawn)
						]
					]
					+ SVerticalBox::Slot()
					[
						SAssignNew(OutlinerView, SSMPreviewModeOutlinerView, InStateMachineEditor, Blueprint->GetPreviewObject()->GetCurrentWorld())
					]
				]
			]
			+SOverlay::Slot()
			[
				SNew(SBorder)
				.Padding(FMargin(2.0f, 5.0f))
				.BorderImage( FEditorStyle::GetBrush("NoBorder") )
				.Visibility(this, &SSMPreviewModeEditorView::IsEditorVisible, ESMPreviewModeType::SM_DetailsMode)
				[
					/*
					 * Details view
					 */

					PreviewDetailsView.ToSharedRef()
				]
			]
		]
	];
}

void SSMPreviewModeEditorView::UpdateSelection()
{
	if (!BlueprintEditor.IsValid() || BlueprintEditor.Pin()->GetCurrentMode() != FSMBlueprintEditorModes::SMPreviewMode)
	{
		return;
	}
	
	if (USMPreviewObject* PreviewObject = FSMPreviewUtils::GetPreviewObject(BlueprintEditor))
	{
		if (!PreviewObject->IsSimulationRunning())
		{
			// This will shutdown the state machine which shouldn't happen unless the simulation has stopped.
			PreviewObject->SetFromBlueprint(BlueprintEditor.Pin()->GetBlueprintObj());
		}
		
		PreviewDetailsView->SetObject(PreviewObject, true);
		{
			TArray<UObject*> SelectedActors;
			
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				AActor* Actor = static_cast<AActor*>(*It);
				checkSlow(Actor->IsA(AActor::StaticClass()));

				if (!Actor->IsPendingKill() && Actor->GetWorld() == PreviewObject->GetCurrentWorld())
				{
					// Only add valid actors that exist in this preview world.
					SelectedActors.Add(Actor);
				}
			}

			if (SelectedActors.Num() > 0)
			{
				SKismetInspector::FShowDetailsOptions Options;
				Options.bForceRefresh = true;
				BlueprintEditor.Pin()->GetInspector()->ShowDetailsForObjects(SelectedActors, Options);

				if (OutlinerView.IsValid())
				{
					for (UObject* Actor : SelectedActors)
					{
						OutlinerView->AddObjectToSelection(Actor);
					}
				}
			}
		}
	}
}

bool SSMPreviewModeEditorView::IsSimulationNotRunning() const
{
	if (USMPreviewObject* PreviewObject = FSMPreviewUtils::GetPreviewObject(BlueprintEditor))
	{
		return !PreviewObject->IsSimulationRunning();
	}
	return true;
}

void SSMPreviewModeEditorView::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	UpdateSelection();
}

void SSMPreviewModeEditorView::OnPreviewObjectChanged(USMPreviewObject* InPreviewObject)
{
	UpdateSelection();
}

void SSMPreviewModeEditorView::OnPreviewWorldChanged(UWorld* InWorld)
{
	// Fully refresh the outliner to display the updated world.
	OutlinerView->CreateWorldOutliner(InWorld);
}

void SSMPreviewModeEditorView::OnEditorSelectionChanged(UObject* NewObject)
{
	UpdateSelection();
}

void SSMPreviewModeEditorView::OnPackageSaved(const FString& PackageFileName, UObject* Outer)
{
	// TODO: Maybe check the package name to make sure it's the correct package before saving, but this isn't very expensive.
	if (USMPreviewObject* PreviewObject = Blueprint->GetPreviewObject())
	{
		PreviewObject->RefreshPreviewWorldActors();
	}
}

void SSMPreviewModeEditorView::OnActorSelectedToSpawn(TSubclassOf<AActor> ActorClass)
{
	check(Blueprint.IsValid());
	
	if (USMPreviewObject* PreviewObject = Blueprint->GetPreviewObject())
	{
		FSMPreviewObjectSpawner Spawner;
		Spawner.Class = ActorClass;
		PreviewObject->AddPreviewActor(Spawner);
		if (Spawner.SpawnedActor)
		{
			// Select the actor on spawn.
			
			if (BlueprintEditor.Pin().IsValid())
			{
				const TWeakPtr<FSMPreviewModeViewportClient> PreviewClient = StaticCastSharedPtr<FSMPreviewModeViewportClient>(BlueprintEditor.Pin()->GetPreviewClient().Pin());
				if (!PreviewClient.IsValid())
				{
					return;
				}

				PreviewClient.Pin()->SelectActor(Spawner.SpawnedActor);
			}
		}
	}
}

void SSMPreviewModeEditorView::OnOutlinerSelectionChanged(SceneOutliner::FTreeItemPtr TreeItem, ESelectInfo::Type Type)
{
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

ECheckBoxState SSMPreviewModeEditorView::IsChecked(ESMPreviewModeType Mode) const
{
	return CurrentMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

EVisibility SSMPreviewModeEditorView::IsEditorVisible(ESMPreviewModeType Mode) const
{
	return CurrentMode == Mode ? EVisibility::Visible : EVisibility::Hidden;
}

const FSlateBrush* SSMPreviewModeEditorView::GetBorderBrushByMode(ESMPreviewModeType Mode) const
{
	if (Mode == CurrentMode)
	{
		return FEditorStyle::GetBrush("ModeSelector.ToggleButton.Pressed");
	}
	
	return FEditorStyle::GetBrush("ModeSelector.ToggleButton.Normal");
}

void SSMPreviewModeEditorView::OnCheckedChanged(ECheckBoxState NewType, ESMPreviewModeType Mode)
{
	if (NewType == ECheckBoxState::Checked)
	{
		CurrentMode = Mode;
	}
}

#undef LOCTEXT_NAMESPACE
