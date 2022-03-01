// Copyright Recursoft LLC 2019-2021. All Rights Reserved.

#pragma once

#include "ISMExtendedEditorModule.h"


#define LOCTEXT_NAMESPACE "SMSystemEditorModule"

class FExtensibilityManager;

class FSMExtendedEditorModule : public ISMExtendedEditorModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

protected:
	void RegisterSettings();
	void UnregisterSettings();

private:
	FDelegateHandle RenameVariableReferencesDelegateHandle;
	FDelegateHandle RenameGraphsDelegateHandle;
	FDelegateHandle OnEditorCommandsCreatedHandle;
	FDelegateHandle OnBlueprintPostConditionallyCompiledHandle;
	FDelegateHandle OnPropertyChangedHandle;
};

IMPLEMENT_MODULE(FSMExtendedEditorModule, SMExtendedEditor)


#undef LOCTEXT_NAMESPACE

