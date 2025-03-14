#pragma once

#include "CoreMinimal.h"

#include "AngelscriptManager.h"

#include "ASStruct.generated.h"

UCLASS()
class ANGELSCRIPTCODE_API UASStruct : public UScriptStruct
{
	GENERATED_BODY()
public:
	UASStruct* NewerVersion = nullptr;
	class asITypeInfo* ScriptType = nullptr;
	FGuid Guid;

	UScriptStruct* GetNewestVersion() override
	{
#if !AS_CAN_HOTRELOAD
		return this;
#else
		if (NewerVersion == nullptr)
			return this;

		UASStruct* NewerStruct = NewerVersion;
		while (NewerStruct->NewerVersion != nullptr)
			NewerStruct = NewerStruct->NewerVersion;
		return NewerStruct;
#endif
	}

	asIScriptFunction* GetToStringFunction() const;

	FGuid GetCustomGuid() const override
	{
		return Guid;
	}

	void SetGuid(FName FromName);

	UASStruct(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void UpdateScriptType();

	void PrepareCppStructOps() override;
	ICppStructOps* CreateCppStructOps();
};