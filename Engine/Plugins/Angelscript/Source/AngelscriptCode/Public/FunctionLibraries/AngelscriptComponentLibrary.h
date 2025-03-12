#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "AngelscriptManager.h"

#if WITH_EDITOR
#include "UObject/UObjectThreadContext.h"
#endif

#include "AngelscriptComponentLibrary.generated.h"

UCLASS(Meta = (ScriptMixin = "USceneComponent"))
class UAngelscriptComponentLibrary : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static FVector GetRelativeLocation(const USceneComponent* Component)
	{
		return Component->GetRelativeLocation();
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static FRotator GetRelativeRotation(const USceneComponent* Component)
	{
		return Component->GetRelativeRotation();
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static FVector GetRelativeScale3D(const USceneComponent* Component)
	{
		return Component->GetRelativeScale3D();
	}

	UFUNCTION(ScriptCallable)
	static void SetRelativeLocation(USceneComponent* Component, const FVector& NewLocation)
	{
		Component->SetRelativeLocation(NewLocation);
	}

	UFUNCTION(ScriptCallable)
	static void SetRelativeRotation(USceneComponent* Component, const FRotator& NewRotation)
	{
		Component->SetRelativeRotation(NewRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "SetRelativeRotation", NotAngelscriptProperty))
	static void SetRelativeRotationQuat(USceneComponent* Component, const FQuat& NewRotation)
	{
		Component->SetRelativeRotation(NewRotation);
	}

	UFUNCTION(ScriptCallable)
	static void SetRelativeTransform(USceneComponent* Component, const FTransform& NewTransform)
	{
		Component->SetRelativeTransform(NewTransform);
	}

	UFUNCTION(ScriptCallable)
	static void SetRelativeLocationAndRotation(USceneComponent* Component, const FVector& NewLocation, const FRotator& NewRotation)
	{
		Component->SetRelativeLocationAndRotation(NewLocation, NewRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "SetRelativeLocationAndRotation"))
	static void SetRelativeLocationAndRotationQuat(USceneComponent* Component, const FVector& NewLocation, const FQuat& NewRotation)
	{
		Component->SetRelativeLocationAndRotation(NewLocation, NewRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static FQuat GetSocketQuaternion(USceneComponent* Component, const FName& SocketName)
	{
		return Component->GetSocketQuaternion(SocketName);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static void SetComponentQuat(USceneComponent* Component, const FQuat& NewRotation)
	{
		Component->SetWorldRotation(NewRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static FQuat GetComponentQuat(const USceneComponent* Component)
	{
		return Component->GetComponentQuat();
	}

	UFUNCTION(ScriptCallable)
	static void AddRelativeLocation(USceneComponent* Component, const FVector& DeltaLocation)
	{
		Component->AddRelativeLocation(DeltaLocation);
	}

	UFUNCTION(ScriptCallable)
	static void AddRelativeRotation(USceneComponent* Component, const FRotator& DeltaRotation)
	{
		Component->AddRelativeRotation(DeltaRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "AddRelativeRotation"))
	static void AddRelativeRotationQuat(USceneComponent* Component, const FQuat& DeltaRotation)
	{
		Component->AddRelativeRotation(DeltaRotation);
	}

	UFUNCTION(ScriptCallable)
	static void AddLocalOffset(USceneComponent* Component, const FVector& DeltaLocation)
	{
		Component->AddLocalOffset(DeltaLocation);
	}

	UFUNCTION(ScriptCallable)
	static void AddLocalRotation(USceneComponent* Component, const FRotator& DeltaRotation)
	{
		Component->AddLocalRotation(DeltaRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "AddLocalRotation"))
	static void AddLocalRotationQuat(USceneComponent* Component, const FQuat& DeltaRotation)
	{
		Component->AddLocalRotation(DeltaRotation);
	}

	UFUNCTION(ScriptCallable)
	static void AddLocalTransform(USceneComponent* Component, const FTransform& DeltaTransform)
	{
		Component->AddLocalTransform(DeltaTransform);
	}

	UFUNCTION(ScriptCallable)
	static void SetWorldLocation(USceneComponent* Component, const FVector& NewLocation)
	{
		Component->SetWorldLocation(NewLocation);
	}

	UFUNCTION(ScriptCallable)
	static void SetWorldRotation(USceneComponent* Component, const FRotator& NewRotation)
	{
		Component->SetWorldRotation(NewRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "SetWorldRotation", NotAngelscriptProperty))
	static void SetWorldRotationQuat(USceneComponent* Component, const FQuat& NewRotation)
	{
		Component->SetWorldRotation(NewRotation);
	}

	UFUNCTION(ScriptCallable)
	static void SetWorldTransform(USceneComponent* Component, const FTransform& NewTransform)
	{
		Component->SetWorldTransform(NewTransform);
	}

	UFUNCTION(ScriptCallable)
	static void SetWorldLocationAndRotation(USceneComponent* Component, const FVector& NewLocation, const FRotator& NewRotation)
	{
		Component->SetWorldLocationAndRotation(NewLocation, NewRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "SetWorldLocationAndRotation"))
	static void SetWorldLocationAndRotationQuat(USceneComponent* Component, const FVector& NewLocation, const FQuat& NewRotation)
	{
		Component->SetWorldLocationAndRotation(NewLocation, NewRotation);
	}

	UFUNCTION(ScriptCallable)
	static void AddWorldOffset(USceneComponent* Component, const FVector& DeltaLocation)
	{
		Component->AddWorldOffset(DeltaLocation);
	}

	UFUNCTION(ScriptCallable)
	static void AddWorldRotation(USceneComponent* Component, const FRotator& DeltaRotation)
	{
		Component->AddWorldRotation(DeltaRotation);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "AddWorldRotation"))
	static void AddWorldRotationQuat(USceneComponent* Component, const FQuat& DeltaRotation)
	{
		Component->AddWorldRotation(DeltaRotation);
	}

	UFUNCTION(ScriptCallable)
	static void AddWorldTransform(USceneComponent* Component, const FTransform& DeltaTransform)
	{
		Component->AddWorldTransform(DeltaTransform);
	}

	UFUNCTION(ScriptCallable)
	static void AttachToComponent(USceneComponent* Component, USceneComponent* Parent, const FName& SocketName = NAME_None, EAttachmentRule AttachmentRule = EAttachmentRule::SnapToTarget)
	{
#if WITH_EDITOR
		FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
		if (ThreadContext.IsInConstructor)
		{
			FAngelscriptManager::Throw("Calling AttachToComponent in a default statement is invalid. Please use the 'Attach =' and 'AttachSocket = ' UPROPERTY specifiers instead.");
			return;
		}
#endif
		Component->K2_AttachToComponent(Parent, SocketName, AttachmentRule, AttachmentRule, EAttachmentRule::KeepWorld, false);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static void SetbVisualizeComponent(USceneComponent* Component, bool bVisualize)
	{
#if WITH_EDITORONLY_DATA
		Component->bVisualizeComponent = bVisualize;
#endif
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static bool IsAttachedTo(const USceneComponent* Component, const USceneComponent* CheckComponent)
	{
		return Component->IsAttachedTo(CheckComponent);
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptName = "IsAttachedTo", ScriptTrivial))
	static bool IsAttachedTo_Actor(const USceneComponent* Component, const AActor* CheckActor)
	{
		const USceneComponent* CheckComp = Component;
		while(CheckComp != nullptr)
		{
			if (CheckComp->GetOwner() == CheckActor)
				return true;
			CheckComp = CheckComp->GetAttachParent();
		}

		return false;
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial))
	static FBoxSphereBounds GetBounds(const USceneComponent* Component)
	{
		return Component->Bounds;
	}

	UFUNCTION(ScriptCallable, Meta = (ScriptTrivial, DeprecatedFunction, DeprecationMessage = "Get Bounds.Origin instead"))
	static FVector GetShapeCenter(const USceneComponent* Component)
	{
		return Component->Bounds.Origin;
	}
};