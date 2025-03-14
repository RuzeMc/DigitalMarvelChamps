// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Math/Color.h"
#include "RigVMTemplateNode.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "RigVMIfNode.generated.h"

class UObject;
struct FRigVMTemplate;

/**
 * A if node is used to pick between two values
 */
UCLASS(BlueprintType, Deprecated)
class RIGVMDEVELOPER_API UDEPRECATED_RigVMIfNode : public URigVMTemplateNode
{
	GENERATED_BODY()

public:

	// Override from URigVMTemplateNode
	virtual FName GetNotation() const override;
	virtual const FRigVMTemplate* GetTemplate() const override;
	virtual bool IsSingleton() const override { return false; }

	// Override from URigVMNode
	virtual FString GetNodeTitle() const override { return IfName; }
	virtual FLinearColor GetNodeColor() const override { return FLinearColor::Black; }
private:

	static const inline TCHAR* IfName = TEXT("If");
	static const inline TCHAR* ConditionName = TEXT("Condition");
	static const inline TCHAR* TrueName = TEXT("True");
	static const inline TCHAR* FalseName = TEXT("False");
	static const inline TCHAR* ResultName = TEXT("Result");

	friend class URigVMController;
	friend class URigVMCompiler;
	friend struct FRigVMAddIfNodeAction;
	friend class URigVMEdGraphIfNodeSpawner;
	friend class FRigVMParserAST;
	friend struct FRigVMRemoveNodeAction;
};

