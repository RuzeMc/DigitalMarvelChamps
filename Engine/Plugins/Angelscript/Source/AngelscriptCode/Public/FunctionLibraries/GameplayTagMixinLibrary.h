#pragma once

#include "CoreMinimal.h"

#include "Runtime/GameplayTags/Classes/GameplayTagContainer.h"

#include "GameplayTagMixinLibrary.generated.h"

/**
 * ScriptMixin library to bind functions on FGameplayTag
 * that are not BlueprintCallable by default.
 */
UCLASS(Meta = (ScriptMixin = "FGameplayTag"))
class UGameplayTagMixinLibrary : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(ScriptCallable)
	static bool MatchesTag(const FGameplayTag& GameplayTag, const FGameplayTag& TagToCheck)
	{
		return GameplayTag.MatchesTag(TagToCheck);
	}

	UFUNCTION(ScriptCallable)
	static bool MatchesTagExact(const FGameplayTag& GameplayTag, const FGameplayTag& TagToCheck)
	{
		return GameplayTag.MatchesTagExact(TagToCheck);
	}

	UFUNCTION(ScriptCallable)
	static int32 MatchesTagDepth(const FGameplayTag& GameplayTag, const FGameplayTag& TagToCheck)
	{
		return GameplayTag.MatchesTagDepth(TagToCheck);
	}

	UFUNCTION(ScriptCallable)
	static bool MatchesAny(const FGameplayTag& GameplayTag, const FGameplayTagContainer& ContainerToCheck)
	{
		return GameplayTag.MatchesAny(ContainerToCheck);
	}

	UFUNCTION(ScriptCallable)
	static bool MatchesAnyExact(const FGameplayTag& GameplayTag, const FGameplayTagContainer& ContainerToCheck)
	{
		return GameplayTag.MatchesAnyExact(ContainerToCheck);
	}

	UFUNCTION(ScriptCallable)
	static bool IsValid(const FGameplayTag& GameplayTag)
	{
		return GameplayTag.IsValid();
	}

	UFUNCTION(ScriptCallable)
	static FGameplayTagContainer GetSingleTagContainer(const FGameplayTag& GameplayTag)
	{
		return GameplayTag.GetSingleTagContainer();
	}

	UFUNCTION(ScriptCallable)
	static FGameplayTag RequestDirectParent(const FGameplayTag& GameplayTag)
	{
		return GameplayTag.RequestDirectParent();
	}

	UFUNCTION(ScriptCallable)
	static FGameplayTagContainer GetGameplayTagParents(const FGameplayTag& GameplayTag)
	{
		return GameplayTag.GetGameplayTagParents();
	}
};
