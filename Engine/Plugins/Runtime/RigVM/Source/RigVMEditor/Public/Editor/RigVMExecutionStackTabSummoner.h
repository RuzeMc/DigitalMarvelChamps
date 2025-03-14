// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FRigVMEditor;

struct RIGVMEDITOR_API FRigVMExecutionStackTabSummoner : public FWorkflowTabFactory
{
public:

	static inline const FLazyName TabID = FLazyName(TEXT("Execution Stack"));
	
public:
	FRigVMExecutionStackTabSummoner(const TSharedRef<FRigVMEditor>& InRigVMEditor);
	
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	
protected:
	TWeakPtr<FRigVMEditor> RigVMEditor;
};
