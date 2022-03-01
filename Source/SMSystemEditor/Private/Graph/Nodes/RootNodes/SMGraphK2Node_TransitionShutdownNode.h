// Copyright Recursoft LLC 2019-2021. All Rights Reserved.

#pragma once

#include "SMGraphK2Node_RuntimeNodeContainer.h"
#include "SMGraphK2Node_TransitionShutdownNode.generated.h"


/**
 * Can be used in Transition graphs and Conduit graphs when they're configured as transitions.
 */
UCLASS(MinimalAPI)
class USMGraphK2Node_TransitionShutdownNode : public USMGraphK2Node_RuntimeNodeReference
{
	GENERATED_UCLASS_BODY()

	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual void PostPlacedNewNode() override;
	virtual FText GetMenuCategory() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	/** Required to show up in BP right click context menu. */
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	/** Limit blueprints this shows up in. */
	virtual bool IsActionFilteredOut(class FBlueprintActionFilter const& Filter) override;
	virtual bool IsCompatibleWithGraph(UEdGraph const* Graph) const override;
	/** User can replace node. */
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	//~ End UEdGraphNode Interface
};
