// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/AvaInteractiveToolsActorPointToolBase.h"
#include "AvaClonerActorTool.generated.h"

UCLASS()
class UAvaClonerActorTool : public UAvaInteractiveToolsActorPointToolBase
{
	GENERATED_BODY()

public:
	UAvaClonerActorTool();

	//~ Begin UAvaInteractiveToolsToolBase
	virtual FName GetCategoryName() override;
	virtual FAvaInteractiveToolsToolParameters GetToolParameters() const override;
	//~ End UAvaInteractiveToolsToolBase
};
