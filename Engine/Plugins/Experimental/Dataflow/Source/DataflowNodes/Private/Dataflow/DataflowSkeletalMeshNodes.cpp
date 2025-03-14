// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSkeletalMeshNodes.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowEngineTypes.h"
#include "Logging/LogMacros.h"
#include "UObject/UnrealTypePrivate.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowSkeletalMeshNodes)

namespace UE::Dataflow
{
	void RegisterSkeletalMeshNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetSkeletalMeshDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetSkeletonDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSkeletalMeshBoneDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSkeletalMeshReferenceTransformDataflowNode);
	}
}

void FGetSkeletalMeshDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeletalMesh> DataType;
	if (Out->IsA<DataType>(&SkeletalMesh))
	{
		SetValue(Context, SkeletalMesh, &SkeletalMesh);

		if (!SkeletalMesh)
		{
			if (const UE::Dataflow::FEngineContext* EngineContext = Context.AsType<UE::Dataflow::FEngineContext>())
			{
				if (const USkeletalMesh* SkeletalMeshFromOwner = UE::Dataflow::Reflection::FindObjectPtrProperty<USkeletalMesh>(
					EngineContext->Owner, PropertyName))
				{
					SetValue<DataType>(Context, DataType(SkeletalMeshFromOwner), &SkeletalMesh);
				}
			}
		}
	}
}


void FGetSkeletonDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeleton> DataType;
	if (Out->IsA<DataType>(&Skeleton))
	{
		SetValue(Context, Skeleton, &Skeleton);

		if (!Skeleton)
		{
			if (const UE::Dataflow::FEngineContext* EngineContext = Context.AsType<UE::Dataflow::FEngineContext>())
			{
				if (const USkeleton* SkeletonFromOwner = UE::Dataflow::Reflection::FindObjectPtrProperty<USkeleton>(
					EngineContext->Owner, PropertyName))
				{
					SetValue<DataType>(Context, DataType(SkeletonFromOwner), &Skeleton);
				}
			}
		}
	}
}

void FSkeletalMeshBoneDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeletalMesh> InDataType;
	if (Out->IsA<int>(&BoneIndexOut))
	{
		SetValue<int>(Context, INDEX_NONE, &BoneIndexOut);

		if( InDataType InSkeletalMesh = GetValue<InDataType>(Context, &SkeletalMesh) )
		{
			FName LocalBoneName = BoneName;
			if (LocalBoneName.IsNone())
			{
				if (const UE::Dataflow::FEngineContext* EngineContext = Context.AsType<UE::Dataflow::FEngineContext>())
				{
					LocalBoneName = FName(UE::Dataflow::Reflection::FindOverrideProperty< FString >(EngineContext->Owner, PropertyName, FName("BoneName")));
				}
			}

			int32 Index = InSkeletalMesh->GetRefSkeleton().FindBoneIndex(LocalBoneName);
			SetValue(Context, Index, &BoneIndexOut);
		}

	}
}


void FSkeletalMeshReferenceTransformDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const USkeletalMesh> InDataType;
	if (Out->IsA<FTransform>(&TransformOut))
	{
		SetValue(Context, FTransform::Identity, &TransformOut);
		
		int32 BoneIndex = GetValue<int32>(Context, &BoneIndexIn);
		if (0 <= BoneIndex)
		{
			if (InDataType SkeletalMesh = GetValue<InDataType>(Context, &SkeletalMeshIn))
			{
				TArray<FTransform> ComponentPose;
				UE::Dataflow::Animation::GlobalTransforms(SkeletalMesh->GetRefSkeleton(), ComponentPose);
				if (BoneIndex < ComponentPose.Num())
				{
					SetValue(Context, ComponentPose[BoneIndex], &TransformOut);
				}
			}
		}
	}
}





