// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementSelectionColumns.generated.h"

/**
 * Column to represent that a row is selected
 */
USTRUCT(meta = (DisplayName = "Selected"))
struct FTypedElementSelectionColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	// The selection set this row belongs to, empty = default selection set
	UPROPERTY()
	FName SelectionSet;
};
