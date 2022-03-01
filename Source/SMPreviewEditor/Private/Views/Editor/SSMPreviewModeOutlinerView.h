// Copyright Recursoft LLC 2019-2021. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "SGraphPanel.h"

class USMPreviewObject;
class FSMBlueprintEditor;
class ICustomSceneOutliner;

/**
 * Custom outliner allowing a context to be selected and filtering the world and actor list.
 */
class SSMPreviewModeOutlinerView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSMPreviewModeOutlinerView) {}
	SLATE_END_ARGS()

	~SSMPreviewModeOutlinerView();
	void Construct(const FArguments& InArgs, TSharedPtr<FSMBlueprintEditor> InStateMachineEditor, UWorld* InWorld);
	void CreateWorldOutliner(UWorld* World);
	void AddObjectToSelection(UObject* Object);

protected:
	void UpdateWidget();
	void OnOutlinerSelectionChanged(SceneOutliner::FTreeItemPtr TreeItem, ESelectInfo::Type Type);

protected:
	TSharedPtr<ICustomSceneOutliner> SceneOutliner;
	TWeakPtr<FSMBlueprintEditor> BlueprintEditor;
	
	FDelegateHandle SceneOutlinerSelectionChanged;
};
