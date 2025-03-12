#include "AngelscriptBinds.h"
#include "AngelscriptManager.h"

#include "Engine/CollisionProfile.h"

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_ConfigEnums((int32)FAngelscriptBinds::EOrder::Early - 1, []
{
	auto ETraceTypeQuery_ = FAngelscriptBinds::Enum("ETraceTypeQuery");
	auto ECollisionChannel_ = FAngelscriptBinds::Enum("ECollisionChannel");
	auto EObjectTypeQuery_ = FAngelscriptBinds::Enum("EObjectTypeQuery");

	UCollisionProfile* Collision = UCollisionProfile::Get();

	for (FCustomChannelSetup& Profile : Collision->DefaultChannelResponses)
	{
		const FString BPName = Profile.Name.ToString().Replace(TEXT(" "), TEXT("_"));

		if (Profile.bTraceType)
		{
			int32 BPValue = (int32)Collision->ConvertToTraceType(Profile.Channel);
			ETraceTypeQuery_[BPName] = BPValue;
		}
		else
		{
			int32 BPValue = (int32)Collision->ConvertToObjectType(Profile.Channel);
			EObjectTypeQuery_[BPName] = BPValue;
		}

		ECollisionChannel_[BPName] = (int32)Profile.Channel;
	}

	ETraceTypeQuery_["Visibility"] = (int32)Collision->ConvertToTraceType(ECC_Visibility);
	ETraceTypeQuery_["Camera"] = (int32)Collision->ConvertToTraceType(ECC_Camera);

	EObjectTypeQuery_["WorldStatic"] = (int32)Collision->ConvertToObjectType(ECC_WorldStatic);
	EObjectTypeQuery_["WorldDynamic"] = (int32)Collision->ConvertToObjectType(ECC_WorldDynamic);
	EObjectTypeQuery_["Pawn"] = (int32)Collision->ConvertToObjectType(ECC_Pawn);
	EObjectTypeQuery_["PhysicsBody"] = (int32)Collision->ConvertToObjectType(ECC_PhysicsBody);
	EObjectTypeQuery_["Vehicle"] = (int32)Collision->ConvertToObjectType(ECC_Vehicle);
	EObjectTypeQuery_["Destructible"] = (int32)Collision->ConvertToObjectType(ECC_Destructible);
});