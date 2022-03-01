// Copyright Recursoft LLC 2019-2021. All Rights Reserved.

#include "SMGraphFactory.h"
#include "Schema/SMGraphK2Schema.h"
#include "Nodes/SMGraphNode_TransitionEdge.h"
#include "Nodes/SlateNodes/SGraphNode_StateNode.h"
#include "Nodes/SlateNodes/SGraphNode_StateMachineNode.h"
#include "Nodes/SlateNodes/SGraphNode_StateMachineStateNode.h"
#include "Nodes/SlateNodes/SGraphNode_StateEntryNode.h"
#include "Nodes/SlateNodes/SGraphNode_StateMachineEntryNode.h"
#include "Nodes/SlateNodes/SGraphNode_TransitionEdge.h"
#include "Nodes/SMGraphK2Node_StateMachineNode.h"
#include "Nodes/SMGraphNode_StateNode.h"
#include "Nodes/SMGraphNode_ConduitNode.h"
#include "Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Nodes/SMGraphNode_StateMachineEntryNode.h"
#include "Nodes/RootNodes/SMGraphK2Node_StateEntryNode.h"
#include "Pins/SGraphPin_StateMachinePin.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "KismetPins/SGraphPinExec.h"
#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "SMGraphFactory"


TSharedPtr<class SGraphNode> FSMGraphPanelNodeFactory::CreateNode(class UEdGraphNode* Node) const
{
	if (USMGraphK2Node_StateMachineNode* StateMachineNode = Cast<USMGraphK2Node_StateMachineNode>(Node))
	{
		return SNew(SGraphNode_StateMachineNode, StateMachineNode);
	}

	if (USMGraphNode_StateNode* StateNode = Cast<USMGraphNode_StateNode>(Node))
	{
		const USMEditorSettings* Settings = FSMBlueprintEditorUtils::GetEditorSettings();
		return SNew(SGraphNode_StateNode, StateNode).ContentPadding(Settings->StateContentPadding);
	}

	if (USMGraphNode_AnyStateNode* AnyStateNode = Cast<USMGraphNode_AnyStateNode>(Node))
	{
		const USMEditorSettings* Settings = FSMBlueprintEditorUtils::GetEditorSettings();
		return SNew(SGraphNode_StateNode, AnyStateNode).ContentPadding(Settings->StateContentPadding);
	}

	if (USMGraphNode_TransitionEdge* EdgeNode = Cast<USMGraphNode_TransitionEdge>(Node))
	{
		return SNew(SGraphNode_TransitionEdge, EdgeNode);
	}

	if (USMGraphNode_StateMachineEntryNode* EntryNode = Cast<USMGraphNode_StateMachineEntryNode>(Node))
	{
		return SNew(SGraphNode_StateMachineEntryNode, EntryNode);
	}

	if (USMGraphK2Node_StateEntryNode* EntryNode = Cast<USMGraphK2Node_StateEntryNode>(Node))
	{
		return SNew(SGraphNode_StateEntryNode, EntryNode);
	}

	if(USMGraphNode_ConduitNode* ConduitNode = Cast<USMGraphNode_ConduitNode>(Node))
	{
		return SNew(SGraphNode_ConduitNode, ConduitNode);
	}

	if(USMGraphNode_StateMachineStateNode* StateMachineStateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
	{
		return SNew(SGraphNode_StateMachineStateNode, StateMachineStateNode);
	}

	return FGraphPanelNodeFactory::CreateNode(Node);
}

TSharedPtr<class SGraphPin> FSMGraphPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return SNew(SGraphPinExec, InPin);
	}
	if(InPin->PinType.PinCategory == USMGraphK2Schema::PC_StateMachine)
	{
		return SNew(SSMGraphPin_StateMachinePin, InPin);
	}

	return FGraphPanelPinFactory::CreatePin(InPin);
}

#undef LOCTEXT_NAMESPACE
