#include "Components/SkeletalMeshComponent.h"

#include "AngelscriptBinds.h"

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_USkeletalMeshComponent(FAngelscriptBinds::EOrder::Late, []
{
	FAngelscriptBinds USkeletalMeshComponent_ = FAngelscriptBinds::ExistingClass("USkeletalMeshComponent");

	USkeletalMeshComponent_.Method("const TArray<UAnimInstance>& GetLinkedAnimInstances() const",
	[](const USkeletalMeshComponent* SkeletalMeshComponent) -> const TArray<UAnimInstance*>&
	{
		return SkeletalMeshComponent->GetLinkedAnimInstances();
	});
});