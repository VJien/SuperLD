// Copyright Recursoft LLC 2019-2021. All Rights Reserved.

#include "SMTextPropertyGraph.h"
#include "Nodes/PropertyNodes/SMGraphK2Node_TextPropertyNode.h"
#include "Text/SMTextParsing.h"
#include "STextPropertyEditableTextBox.h"
#include "Utilities/SMTextGraphUtils.h"

#include "Utilities/SMBlueprintEditorUtils.h"

#include "GraphEditAction.h"
#include "K2Node_FormatText.h"
#include "K2Node_VariableGet.h"
#include "ScopedTransaction.h"


/** SGraphPinText.cpp contains a private definition. We really just need access to the protected StaticStableTextId of IEditableTextProperty */
class FOurEditableTextGraphPin : public IEditableTextProperty
{
public:
	FOurEditableTextGraphPin(UEdGraphPin* InGraphPinObj)
		: GraphPinObj(InGraphPinObj)
	{
	}

	virtual bool IsMultiLineText() const override
	{
		return true;
	}

	virtual bool IsPassword() const override
	{
		return false;
	}

	virtual bool IsReadOnly() const override
	{
		return GraphPinObj->bDefaultValueIsReadOnly;
	}

	virtual bool IsDefaultValue() const override
	{
		FString TextAsString;
		FTextStringHelper::WriteToBuffer(TextAsString, GraphPinObj->DefaultTextValue);
		return TextAsString.Equals(GraphPinObj->AutogeneratedDefaultValue, ESearchCase::CaseSensitive);
	}

	virtual FText GetToolTipText() const override
	{
		return FText::GetEmpty();
	}

	virtual int32 GetNumTexts() const override
	{
		return 1;
	}

	virtual FText GetText(const int32 InIndex) const override
	{
		check(InIndex == 0);
		return GraphPinObj->DefaultTextValue;
	}

	virtual void SetText(const int32 InIndex, const FText& InText) override
	{
		check(InIndex == 0);
		GraphPinObj->Modify();
		GraphPinObj->GetSchema()->TrySetDefaultText(*GraphPinObj, InText);
	}

	virtual bool IsValidText(const FText& InText, FText& OutErrorMsg) const override
	{
		return true;
	}

#if USE_STABLE_LOCALIZATION_KEYS
	virtual void GetStableTextId(const int32 InIndex, const ETextPropertyEditAction InEditAction, const FString& InTextSource, const FString& InProposedNamespace, const FString& InProposedKey, FString& OutStableNamespace, FString& OutStableKey) const override
	{
		check(InIndex == 0);
		return StaticStableTextId(GraphPinObj->GetOwningNodeUnchecked(), InEditAction, InTextSource, InProposedNamespace, InProposedKey, OutStableNamespace, OutStableKey);
	}
#endif // USE_STABLE_LOCALIZATION_KEYS

	virtual void RequestRefresh() override
	{
	}

	UEdGraphPin* GraphPinObj;
};


USMTextPropertyGraph::USMTextPropertyGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), FormatTextNode(nullptr)
{
}

UEdGraphPin* USMTextPropertyGraph::GetFormatTextNodePin() const
{
	check(FormatTextNode);
	return FormatTextNode->FindPinChecked(TEXT("Format")); // Can't use GetFormatPin because it uses cached data which could be invalidated on edit undo.
}

void USMTextPropertyGraph::PostEditUndo()
{
	bIsEditUndo = true;
	Super::PostEditUndo();
	bIsEditUndo = false;
}

void USMTextPropertyGraph::NotifyGraphChanged()
{
	Super::NotifyGraphChanged();
	SetTextFromFormatTextNode();
}

void USMTextPropertyGraph::NotifyGraphChanged(const FEdGraphEditAction& Action)
{
	Super::NotifyGraphChanged(Action);

	if((Action.Action & EEdGraphActionType::GRAPHACTION_AddNode) && Action.bUserInvoked && Action.Graph == this && !IsGraphBeingUsedToEdit())
	{
		/*
		 * A user drag dropping a text variable directly on the localization pin will try and connect the variable to the format pin.
		 * To avoid that we're adding the format pin as a restriction. After add node completes it will call the schema to try and create the connection
		 * at which point we will cancel it.
		*/
		if (Action.Nodes.Num() > 0)
		{
			PreventConnections.Reset();
			PreventConnections.Add(GetFormatTextNodePin());
		}
	}
}

void USMTextPropertyGraph::RefreshProperty(bool bModify, bool bSetFromPinFirst)
{
	Super::RefreshProperty(bModify, bSetFromPinFirst);
	bEditable = IsGraphBeingUsedToEdit();
	
	RefreshTextBody(bModify);
	PruneDisconnectedNodes();
}

void USMTextPropertyGraph::ResetGraph()
{
	if(IsGraphBeingUsedToEdit())
	{
		return;
	}

	bIsUpdatingGraph = true;
	
	// Backup the default value so we can save localization settings.
	FText DefaultValue;
	if (UEdGraphPin* Pin = GetFormatTextNodePin())
	{
		DefaultValue = Pin->DefaultTextValue;
	}
	
	Super::ResetGraph();

	// Restore the default value.
	if (UEdGraphPin* Pin = GetFormatTextNodePin())
	{
		Pin->DefaultTextValue = DefaultValue;
	}

	bIsUpdatingGraph = false;
}

void USMTextPropertyGraph::SetUsingGraphToEdit(bool bValue, bool bModify)
{
	auto MethodBody = [&]()
	{
		Super::SetUsingGraphToEdit(bValue, bModify);
		if (bValue)
		{
			SetTextEditMode(false);
		}
	};
	
	if (bModify)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "GraphEdit", "Graph Edit"));
		MethodBody();
	}
	else
	{
		MethodBody();
	}
}

bool USMTextPropertyGraph::IsGraphBeingUsedToEdit() const
{
	if (IsVariableReadOnly())
	{
		return false;
	}
	return bUpdatingFromFormatTextNode ? false : Super::IsGraphBeingUsedToEdit();
}

void USMTextPropertyGraph::OnGraphManuallyCloned(USMPropertyGraph* OldGraph)
{
	Super::OnGraphManuallyCloned(OldGraph);
	PruneDisconnectedNodes();
	FindAndSetFormatTextNode();
	USMTextPropertyGraph* OldTextGraph = CastChecked<USMTextPropertyGraph>(OldGraph);
	PlainTextBody = OldTextGraph->PlainTextBody;
	RichTextBody = OldTextGraph->RichTextBody;
	StoredFunctions = OldTextGraph->StoredFunctions;
	StoredProperties = OldTextGraph->StoredProperties;
	bEditable = OldGraph->bEditable;
}

void USMTextPropertyGraph::SetNewText(const FText& PlainText, bool bReformatGraph, bool bModify)
{
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SetText", "Set Text"));
	SetNewText_NoTransaction(PlainText, bReformatGraph, bModify);
}

void USMTextPropertyGraph::SetNewText_NoTransaction(const FText& PlainText, bool bReformatGraph, bool bModify)
{
	if(bModify)
	{
		Modify();
	}
	if(bReformatGraph)
	{
		ResetGraph();
	}
	
	SetTextBody(PlainText, bModify, bReformatGraph);
}

void USMTextPropertyGraph::RefreshTextBody(bool bModify, bool bResetGraph)
{
	if (bResetGraph)
	{
		ResetGraph();
	}
	
	SetTextBody(PlainTextBody, bModify);
}

void USMTextPropertyGraph::SetTextBody(const FText& PlainText, bool bModify, bool bReformatGraph)
{
	if(IsGraphBeingUsedToEdit())
	{
		return;
	}

	bIsUpdatingGraph = true;
	
	if (bModify)
	{
		Modify();
	}
	
	UBlueprint* Blueprint = FSMBlueprintEditorUtils::FindBlueprintForGraphChecked(this);
	const SMTextParser::FParserResults ParserResult = SMTextParser::ConvertToRichText(PlainText, Blueprint, &StoredProperties, &StoredFunctions);

	// Reset after parse now that they have been checked against.
	StoredProperties.Reset();
	StoredFunctions.Reset();
	ResultNode->OwningGraphNode->ResetLogMessages();

	RichTextBody = ParserResult.RichText;
	PlainTextBody = PlainText.IsFromStringTable() ? PlainText : ParserResult.PlainText;
	PlainStringBody = PlainTextBody.ToString();
	
	if (FormatTextNode != nullptr && bReformatGraph)
	{
		if (bModify)
		{
			FormatTextNode->Modify();
		}

		// Will get the format text node to display variable options.
		SetFormatTextNodeText(PlainTextBody);
		
		FSMTextGraphProperty* PropertyNode = (FSMTextGraphProperty*)ResultNode->GetPropertyNodeChecked();
		const bool bHasCustomSerializer = PropertyNode->TextSerializer.HasToTextFunctions();
		for (UFunction* Function : ParserResult.Functions)
		{
			FGuid FunctionGuid;
			if (Blueprint->GetFunctionGuidFromClassByFieldName(Blueprint->SkeletonGeneratedClass, Function->GetFName(), FunctionGuid))
			{
				StoredFunctions.Add(Function->GetFName(), FunctionGuid);
			}
			UEdGraphPin* ArgumentPin = FormatTextNode->FindArgumentPin(Function->GetFName());
			if (!ArgumentPin)
			{
				FSMGraphNodeLog NodeLog(EMessageSeverity::Error);
				NodeLog.ConsoleMessage = TEXT("Could not find argument pin for node @@.");
				NodeLog.NodeMessage = FString::Printf(TEXT("Referenced function %s could not be placed!"), *Function->GetName());
				NodeLog.ReferenceList.Add(FormatTextNode);
				ResultNode->OwningGraphNode->AddNodeLogMessage(NodeLog);
			}
			else if (ArgumentPin->LinkedTo.Num() == 0)
			{
				UEdGraphNode* PlacedNode = nullptr;
				UEdGraphPin* FunctionArgumentPin = nullptr;
				if (!FSMBlueprintEditorUtils::PlaceFunctionOnGraph(this, Function, ArgumentPin, &PlacedNode, &FunctionArgumentPin, -100.f, 100.f, !bHasCustomSerializer))
				{
					bool bManualTextConversionSuccess = false;
					if (FunctionArgumentPin)
					{
						bManualTextConversionSuccess = FSMTextGraphUtils::CreateTextConversionNode(this, FunctionArgumentPin, ArgumentPin, PropertyNode->TextSerializer) != nullptr;
					}

					if (!bManualTextConversionSuccess)
					{
						FSMGraphNodeLog NodeLog(EMessageSeverity::Error);
						NodeLog.ConsoleMessage = TEXT("Function node @@ could not be wired as a text argument.");
						NodeLog.NodeMessage = FString::Printf(TEXT("Referenced function %s could not be converted to text!"), *Function->GetName());
						NodeLog.ReferenceList.Add(PlacedNode);
						ResultNode->OwningGraphNode->AddNodeLogMessage(NodeLog);
					}
				}
			}
		}

		for (const auto& Variable : ParserResult.Variables)
		{
			// Store the found properties to be used as references in case a name changes.
			StoredProperties.Add(Variable.Key, Variable.Value);

			FProperty* Property = Variable.Value.IsValid() ? FSMBlueprintEditorUtils::GetPropertyForVariable(Blueprint, Variable.Key) : nullptr;
			if (Property == nullptr)
			{
				FSMGraphNodeLog NodeLog(EMessageSeverity::Error);
				NodeLog.ConsoleMessage = FString::Printf(TEXT("Referenced property %s in node @@ does not exist!"), *Variable.Key.ToString());
				NodeLog.NodeMessage = FString::Printf(TEXT("Referenced property %s does not exist!"), *Variable.Key.ToString());
				NodeLog.ReferenceList.Add(this);
				ResultNode->OwningGraphNode->AddNodeLogMessage(NodeLog);
				continue;
			}

			// The in pin to the text format node.
			UEdGraphPin* ArgumentPin = FormatTextNode->FindArgumentPin(Variable.Key);
			if (!ArgumentPin)
			{
				FSMGraphNodeLog NodeLog(EMessageSeverity::Error);
				NodeLog.ConsoleMessage = TEXT("Could not find argument pin for node @@.");
				NodeLog.NodeMessage = FString::Printf(TEXT("Referenced variable %s could not be placed!"), *Variable.Key.ToString());
				NodeLog.ReferenceList.Add(FormatTextNode);
				ResultNode->OwningGraphNode->AddNodeLogMessage(NodeLog);
			}
			else if (ArgumentPin->LinkedTo.Num() == 0)
			{
				UK2Node_VariableGet* PlacedNode = nullptr;
				if (!FSMBlueprintEditorUtils::PlacePropertyOnGraph(this, Property, ArgumentPin, &PlacedNode, -100, !bHasCustomSerializer))
				{
					bool bManualTextConversionSuccess = false;
					if (PlacedNode)
					{
						bManualTextConversionSuccess = FSMTextGraphUtils::CreateTextConversionNode(this, PlacedNode->GetValuePin(), ArgumentPin, PropertyNode->TextSerializer) != nullptr;
					}

					if (!bManualTextConversionSuccess)
					{
						FSMGraphNodeLog NodeLog(EMessageSeverity::Error);
						NodeLog.ConsoleMessage = TEXT("Variable node @@ could not be wired as a text argument.");
						NodeLog.NodeMessage = FString::Printf(TEXT("Referenced property %s could not be converted to text!"), *Variable.Key.ToString());
						NodeLog.ReferenceList.Add(PlacedNode);
						ResultNode->OwningGraphNode->AddNodeLogMessage(NodeLog);
					}
				}
			}
		}
	}
	
	if (bModify)
	{
		FSMBlueprintEditorUtils::ConditionallyCompileBlueprint(Blueprint);
	}

	bIsUpdatingGraph = false;
}

FText USMTextPropertyGraph::GetTextBody() const
{
	return RichTextBody;
}

void USMTextPropertyGraph::SetTextEditMode(bool bValue)
{
	SwitchTextEditAction.ExecuteIfBound(bValue);
}

bool USMTextPropertyGraph::ContainsProperty(const FName& InName) const
{
	return StoredProperties.Contains(InName);
}

bool USMTextPropertyGraph::ContainsFunction(const FName& InName) const
{
	return StoredFunctions.Contains(InName);
}

FText MultipleValuesText(NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values"));

void USMTextPropertyGraph::SetFormatTextNodeText(const FText& NewText)
{
	if (!GIsEditor)
	{
		// GetStableTextId will fail if not in editor. This can happen if using play as stand alone game.
		// Normal UE4 behavior only has similar logic happening when committing new text, but we call
		// this on compile.
		return;
	}

	CastChecked<USMGraphK2Node_TextPropertyNode>(ResultNode)->NotifyFormatTextUpdated(NewText);
	
	UEdGraphPin* FormatPin = GetFormatTextNodePin();
	
	if (!EditableTextProperty.IsValid() || EditableTextProperty->GraphPinObj != FormatPin)
	{
		EditableTextProperty = MakeShared<FOurEditableTextGraphPin>(FormatPin);
	}

	const bool bIsStringTable = NewText.IsFromStringTable();
	
	const int32 NumTexts = EditableTextProperty->GetNumTexts();

	// Don't commit the Multiple Values text if there are multiple properties being set
	if (NumTexts > 0 && (NumTexts == 1 || !NewText.ToString().Equals(MultipleValuesText.ToString(), ESearchCase::CaseSensitive)))
	{
		FText TextErrorMsg;
		if (!EditableTextProperty->IsValidText(NewText, TextErrorMsg))
		{
			return;
		}

		for (int32 TextIndex = 0; TextIndex < NumTexts; ++TextIndex)
		{
			const FText PropertyValue = EditableTextProperty->GetText(TextIndex);

			// Is the new text is empty, just use the empty instance
			if (NewText.IsEmpty())
			{
				EditableTextProperty->SetText(TextIndex, FText::GetEmpty());
				continue;
			}

			// Maintain culture invariance when editing the text
			if (PropertyValue.IsCultureInvariant())
			{
				EditableTextProperty->SetText(TextIndex, FText::AsCultureInvariant(NewText.ToString()));
				continue;
			}

			FString NewNamespace;
			FString NewKey;

			if (!bIsStringTable)
			{
#if USE_STABLE_LOCALIZATION_KEYS
				{
					// Get the stable namespace and key that we should use for this property
					const FString* TextSource = FTextInspector::GetSourceString(PropertyValue);

 					EditableTextProperty->GetStableTextId(
						TextIndex,
						IEditableTextProperty::ETextPropertyEditAction::EditedSource,
						TextSource ? *TextSource : FString(),
						FTextInspector::GetNamespace(PropertyValue).Get(FString()),
						FTextInspector::GetKey(PropertyValue).Get(FString()),
						NewNamespace,
						NewKey
					);
				}
#else	// USE_STABLE_LOCALIZATION_KEYS
				{
					// We want to preserve the namespace set on this property if it's *not* the default value
					if (!EditableTextProperty->IsDefaultValue())
					{
						// Some properties report that they're not the default, but still haven't been set from a property, so we also check the property key to see if it's a valid GUID before allowing the namespace to persist
						FGuid TmpGuid;
						if (FGuid::Parse(FTextInspector::GetKey(PropertyValue).Get(FString()), TmpGuid))
						{
							NewNamespace = FTextInspector::GetNamespace(PropertyValue).Get(FString());
						}
					}

					NewKey = FGuid::NewGuid().ToString();
				}
				
#endif	// USE_STABLE_LOCALIZATION_KEYS
			}
			
			EditableTextProperty->SetText(TextIndex, bIsStringTable ? NewText : FText::ChangeKey(NewNamespace, NewKey, NewText));
		}
	}
}

void USMTextPropertyGraph::FindAndSetFormatTextNode()
{
	// The first connected node to the result pin should be the format text node.
	UEdGraphPin** FormatTextPin = ResultNode->GetResultPinChecked()->LinkedTo.FindByPredicate([&](const UEdGraphPin* GraphPin)
	{
		return GraphPin->GetOwningNode()->IsA<UK2Node_FormatText>();
	});

	if (FormatTextPin)
	{
		FormatTextNode = Cast<UK2Node_FormatText>((*FormatTextPin)->GetOwningNode());
		return;
	}

	// Look back through connected nodes.
	TSet<UEdGraphNode*> ConnectedNodes;
	FSMBlueprintEditorUtils::GetAllConnectedNodes(ResultNode, EEdGraphPinDirection::EGPD_Input, ConnectedNodes);

	for (UEdGraphNode* ConnectedNode : ConnectedNodes)
	{
		if (UK2Node_FormatText* FormatNode = Cast<UK2Node_FormatText>(ConnectedNode))
		{
			FormatTextNode = FormatNode;
			return;
		}
	}

	// Last resort, take any node.
	FormatTextNode = FSMBlueprintEditorUtils::GetFirstNodeOfClassNested<UK2Node_FormatText>(this);
}

void USMTextPropertyGraph::SetTextFromFormatTextNode(bool bForce)
{
	if (!bIsUpdatingGraph && !HasAnyFlags(RF_NeedPostLoad) && !HasAnyFlags(RF_NeedPostLoadSubobjects) && GetPackage() != GetTransientPackage() && !ResultNode->GetOwningGraphNodeChecked()->IsEditUndo())
	{
		if(UEdGraphPin* FormatTextPin = GetFormatTextNodePin())
		{
			bUpdatingFromFormatTextNode = true;

			const FText FormatPinText = FormatTextPin->GetDefaultAsText();
			const FString FormatPinString = FormatPinText.ToString();
			const bool bIsStringTable = FormatPinText.IsFromStringTable();
			
			if (bForce || ((bIsStringTable || bEditable) && FormatPinString != PlainStringBody))
			{
				// Don't physically regenerate the graph unless this is from a string table.
				// We don't want to accidentally destroy customized editable graphs.
				const bool bReformatGraph = bIsStringTable;
				if (bIsStringTable && !bIsEditUndo)
				{
					SetNewText(FormatPinText, bReformatGraph, true);
				}
				else
				{
					SetNewText_NoTransaction(FormatPinText, bReformatGraph, false);
				}
				if (bIsStringTable)
				{
					SetUsingGraphToEdit(true);
				}
			}
			
			bUpdatingFromFormatTextNode = false;
		}
	}
}