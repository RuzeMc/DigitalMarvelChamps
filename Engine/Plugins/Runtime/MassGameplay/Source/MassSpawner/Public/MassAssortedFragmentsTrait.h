// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "StructUtils/InstancedStruct.h"
#include "MassAssortedFragmentsTrait.generated.h"

/**
* Mass Agent Feature which appends a list of specified fragments.  
*/
UCLASS(meta=(DisplayName="Assorted Fragments"))
class MASSSPAWNER_API UMassAssortedFragmentsTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

	UPROPERTY(Category="Mass", EditAnywhere, meta = (BaseStruct = "/Script/MassEntity.MassFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> Fragments;

	UPROPERTY(Category="Mass", EditAnywhere, meta = (BaseStruct = "/Script/MassEntity.MassTag", ExcludeBaseStruct))
	TArray<FInstancedStruct> Tags;
};
