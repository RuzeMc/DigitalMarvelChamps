// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeSkeletalMeshFactory.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "ClothingAsset.h"
#include "Components.h"
#include "CoreGlobals.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/SkinnedAssetAsyncCompileUtils.h"
#include "Engine/SkinnedAssetCommon.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GPUSkinPublicDefs.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeCommonPipelineDataFactoryNode.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeManager.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeMeshUtilities.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSkeletonHelper.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Math/GenericOctree.h"
#include "Mesh/InterchangeMeshHelper.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalMeshOperations.h"
#include "SkinnedAssetCompiler.h"

#if WITH_EDITOR
#include "LODUtilities.h"
#include "SkinWeightsUtilities.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeSkeletalMeshFactory)

#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA

#if WITH_EDITOR
namespace UE
{
	namespace Interchange
	{
		FInterchangeMeshPayLoadKey FMeshNodeContext::GetTranslatorAndTransformPayloadKey() const
		{
			FInterchangeMeshPayLoadKey GlobalPayloadKey = TranslatorPayloadKey;
			GlobalPayloadKey.UniqueId = GetUniqueId();
			return GlobalPayloadKey;
		}
		FInterchangeMeshPayLoadKey FMeshNodeContext::GetMorphTargetAndTransformPayloadKey(const FInterchangeMeshPayLoadKey& MorphTargetKey) const
		{
			FInterchangeMeshPayLoadKey GlobalPayloadKey = MorphTargetKey;
			if (SceneGlobalTransform.IsSet())
			{
				GlobalPayloadKey.UniqueId += FInterchangeMeshPayLoadKey::GetTransformString(SceneGlobalTransform.GetValue());
			}
			return GlobalPayloadKey;
		}

		//Return the translator key merge with the transform
		FString FMeshNodeContext::GetUniqueId() const
		{
			FString UniqueId = TranslatorPayloadKey.UniqueId;
			if (SceneGlobalTransform.IsSet())
			{
				UniqueId += FInterchangeMeshPayLoadKey::GetTransformString(SceneGlobalTransform.GetValue());
			}
			return UniqueId;
		}

		namespace Private
		{
			void FillMorphTargetMeshDescriptionsPerMorphTargetName(const FMeshNodeContext& MeshNodeContext
																 , TMap<FString, TOptional<UE::Interchange::FMeshPayloadData>>& MorphTargetMeshDescriptionsPerMorphTargetName
																 , UInterchangeSkeletalMeshFactory::FLodPayloads& LodPayloads
																 , const int32 VertexOffset
																 , const UInterchangeBaseNodeContainer* NodeContainer
																 , FString AssetName)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FillMorphTargetMeshDescriptionsPerMorphTargetName)
				TArray<FString> MorphTargetUids;
				MeshNodeContext.MeshNode->GetMorphTargetDependencies(MorphTargetUids);
				TMap<FString, TOptional<UE::Interchange::FMeshPayloadData>> TempMorphTargetMeshDescriptionsPerMorphTargetName;
				TempMorphTargetMeshDescriptionsPerMorphTargetName.Reserve(MorphTargetUids.Num());
				for (const FString& MorphTargetUid : MorphTargetUids)
				{
					if (const UInterchangeMeshNode* MorphTargetMeshNode = Cast<UInterchangeMeshNode>(NodeContainer->GetNode(MorphTargetUid)))
					{
						TOptional<FInterchangeMeshPayLoadKey> OptionalPayLoadKey = MorphTargetMeshNode->GetPayLoadKey();
						if (!OptionalPayLoadKey.IsSet())
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Empty LOD morph target mesh reference payload when importing SkeletalMesh asset %s."), *AssetName);
							continue;
						}
						FInterchangeMeshPayLoadKey& PayLoadKey = OptionalPayLoadKey.GetValue();
						FInterchangeMeshPayLoadKey GlobalMorphPayLoadKey = MeshNodeContext.GetMorphTargetAndTransformPayloadKey(PayLoadKey);
						//Add the map entry key, the translator will be call after to bulk get all the needed payload
						TempMorphTargetMeshDescriptionsPerMorphTargetName.Add(PayLoadKey.UniqueId, LodPayloads.MorphPayloadPerKey.FindChecked(GlobalMorphPayLoadKey));
					}
				}

				for (const FString& MorphTargetUid : MorphTargetUids)
				{
					if (const UInterchangeMeshNode* MorphTargetMeshNode = Cast<UInterchangeMeshNode>(NodeContainer->GetNode(MorphTargetUid)))
					{
						
						TOptional<FInterchangeMeshPayLoadKey> OptionalPayLoadKey = MorphTargetMeshNode->GetPayLoadKey();
						if (!OptionalPayLoadKey.IsSet())
						{
							continue;
						}
						FInterchangeMeshPayLoadKey& PayLoadKey = OptionalPayLoadKey.GetValue();

						const FString& MorphTargetPayloadKeyString = PayLoadKey.UniqueId;
						if (!ensure(TempMorphTargetMeshDescriptionsPerMorphTargetName.Contains(MorphTargetPayloadKeyString)))
						{
							continue;
						}

						TOptional<UE::Interchange::FMeshPayloadData> MorphTargetMeshPayload = TempMorphTargetMeshDescriptionsPerMorphTargetName.FindChecked(MorphTargetPayloadKeyString);
						if (!MorphTargetMeshPayload.IsSet())
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid skeletal mesh morph target payload key [%s] for SkeletalMesh asset %s."), *MorphTargetPayloadKeyString, *AssetName);
							continue;
						}
						MorphTargetMeshPayload->VertexOffset = VertexOffset;
						//The Mesh node parent bake transform was pass to the payload request.
						MorphTargetMeshPayload->GlobalTransform.Reset();

						if (!MorphTargetMeshNode->GetMorphTargetName(MorphTargetMeshPayload->MorphTargetName))
						{
							MorphTargetMeshPayload->MorphTargetName = MorphTargetPayloadKeyString;
						}
						//Add the morph target to the morph target map
						MorphTargetMeshDescriptionsPerMorphTargetName.Add(MorphTargetPayloadKeyString, MorphTargetMeshPayload);
					}
				}
			}

			void CopyMorphTargetsMeshDescriptionToSkeletalMeshImportData(const TMap<FString, TOptional<UE::Interchange::FMeshPayloadData>>& LodMorphTargetMeshDescriptions, FSkeletalMeshImportData& DestinationSkeletalMeshImportData, const bool bMergeMorphTargetWithSameName)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(CopyMorphTargetsMeshDescriptionToSkeletalMeshImportData)
				const int32 OriginalMorphTargetCount = LodMorphTargetMeshDescriptions.Num();
				TArray < TPair<FString, TArray<FString>>> KeysPerName;
				auto FindOrAdd = [&KeysPerName](const FString& MorphTargetName)->TPair<FString, TArray<FString>>&
					{
						for (TPair<FString, TArray<FString>>& Pair : KeysPerName)
						{
							if (Pair.Key.Equals(MorphTargetName))
							{
								return Pair;
							}
						}

						TPair<FString, TArray<FString>>& NewPair = KeysPerName.AddDefaulted_GetRef();
						NewPair.Key = MorphTargetName;
						return NewPair;
					};
				for (const TPair<FString, TOptional<UE::Interchange::FMeshPayloadData>>& Pair : LodMorphTargetMeshDescriptions)
				{
					const FString MorphTargetUniqueId(Pair.Key);
					const TOptional<UE::Interchange::FMeshPayloadData>& MorphTargetPayloadData = Pair.Value;
					if (!MorphTargetPayloadData.IsSet())
					{
						UE_LOG(LogInterchangeImport, Error, TEXT("Empty morph target optional payload data [%s]."), *MorphTargetUniqueId);
						continue;
					}

					const FMeshDescription& SourceMeshDescription = MorphTargetPayloadData.GetValue().MeshDescription;
					const int32 VertexOffset = MorphTargetPayloadData->VertexOffset;
					const int32 SourceMeshVertexCount = SourceMeshDescription.Vertices().Num();
					const int32 DestinationVertexIndexMax = VertexOffset + SourceMeshVertexCount;
					if (!DestinationSkeletalMeshImportData.Points.IsValidIndex(DestinationVertexIndexMax-1))
					{
						UE_LOG(LogInterchangeImport, Error, TEXT("Corrupted morph target optional payload data [%s]."), *MorphTargetUniqueId);
						continue;
					}

					if (bMergeMorphTargetWithSameName)
					{
						TPair<FString, TArray<FString>>& PairMorphNameWithKeys = FindOrAdd(MorphTargetPayloadData->MorphTargetName);
						PairMorphNameWithKeys.Value.Add(MorphTargetUniqueId);
					}
					else
					{
						TPair<FString, TArray<FString>>& NewPair = KeysPerName.AddDefaulted_GetRef();
						NewPair.Key = MorphTargetPayloadData->MorphTargetName;
						NewPair.Value.Add(MorphTargetUniqueId);
					}
				}
				
				//Adjust the count from the merge context
				const int32 MorphTargetCount = KeysPerName.Num();

				//No morph target to import
				if (MorphTargetCount == 0)
				{
					return;
				}

				//Allocate the data
				DestinationSkeletalMeshImportData.MorphTargetNames.AddDefaulted(MorphTargetCount);
				DestinationSkeletalMeshImportData.MorphTargetModifiedPoints.AddDefaulted(MorphTargetCount);
				DestinationSkeletalMeshImportData.MorphTargets.AddDefaulted(MorphTargetCount);

				int32 NumMorphGroup = FMath::Min(FPlatformMisc::NumberOfWorkerThreadsToSpawn(), MorphTargetCount);
				const int32 MorphTargetGroupSize = FMath::Max(FMath::CeilToInt(static_cast<float>(MorphTargetCount) / static_cast<float>(NumMorphGroup)), 1);
				//Re-Adjust the group Number in case we have a reminder error (exemple MorphTargetGroupSize = 4.8 -> 5 so the number of group can be lower if there is a large amount of Group)
				NumMorphGroup = FMath::CeilToInt(static_cast<float>(MorphTargetCount) / static_cast<float>(MorphTargetGroupSize));

				ParallelFor(NumMorphGroup, [MorphTargetGroupSize,
							MorphTargetCount,
							NumMorphGroup,
							&LodMorphTargetMeshDescriptions,
							&KeysPerName,
							bMergeMorphTargetWithSameName,
							&DestinationSkeletalMeshImportData](const int32 MorphTargetGroupIndex)
				{
					const int32 MorphTargetIndexOffset = MorphTargetGroupIndex * MorphTargetGroupSize;
					const int32 MorphTargetEndLoopCount = MorphTargetIndexOffset + MorphTargetGroupSize;
					for (int32 MorphTargetIndex = MorphTargetIndexOffset; MorphTargetIndex < MorphTargetEndLoopCount; ++MorphTargetIndex)
					{
						if (!KeysPerName.IsValidIndex(MorphTargetIndex))
						{
							ensure(MorphTargetGroupIndex + 1 == NumMorphGroup);
							//Executing the last morph target group, in case we do not have a full last group.
							break;
						}
						//Create the morph target source data
						FString& MorphTargetName = DestinationSkeletalMeshImportData.MorphTargetNames[MorphTargetIndex];
						MorphTargetName = KeysPerName[MorphTargetIndex].Key;
						TSet<uint32>& ModifiedPoints = DestinationSkeletalMeshImportData.MorphTargetModifiedPoints[MorphTargetIndex];
						FSkeletalMeshImportData& MorphTargetData = DestinationSkeletalMeshImportData.MorphTargets[MorphTargetIndex];

						TArray<FVector3f> CompressPoints;
						for (const FString& MorphTargetKey : KeysPerName[MorphTargetIndex].Value)
						{
							const TOptional<UE::Interchange::FMeshPayloadData>& MorphTargetPayloadData = LodMorphTargetMeshDescriptions.FindChecked(MorphTargetKey);
							if (!ensure(MorphTargetPayloadData.IsSet()))
							{
								//This error was suppose to be catch in the pre parallel for loop
								break;
							}

							const FMeshDescription& SourceMeshDescription = MorphTargetPayloadData.GetValue().MeshDescription;
							const FTransform GlobalTransform = MorphTargetPayloadData->GlobalTransform.IsSet() ? MorphTargetPayloadData->GlobalTransform.GetValue() : FTransform::Identity;
							const int32 VertexOffset = MorphTargetPayloadData->VertexOffset;
							const int32 SourceMeshVertexCount = SourceMeshDescription.Vertices().Num();
							const int32 DestinationVertexIndexMax = VertexOffset + SourceMeshVertexCount;
							if (!ensure(DestinationSkeletalMeshImportData.Points.IsValidIndex(DestinationVertexIndexMax - 1)))
							{
								//This error was suppose to be catch in the pre parallel for loop
								break;
							}
							
							CompressPoints.Reserve(CompressPoints.Num() + SourceMeshVertexCount);
							FStaticMeshConstAttributes Attributes(SourceMeshDescription);
							TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

							//Reserve the point and influences
							MorphTargetData.Points.Reset(SourceMeshVertexCount);
							MorphTargetData.Points.AddZeroed(SourceMeshVertexCount);

							for (FVertexID VertexID : SourceMeshDescription.Vertices().GetElementIDs())
							{
								//We can use GetValue because the Meshdescription was compacted before the copy
								MorphTargetData.Points[VertexID.GetValue()] = (FVector3f)GlobalTransform.TransformPosition((FVector)VertexPositions[VertexID]);
							}

							for (int32 PointIdx = VertexOffset; PointIdx < DestinationSkeletalMeshImportData.Points.Num(); ++PointIdx)
							{
								int32 OriginalPointIdx = DestinationSkeletalMeshImportData.PointToRawMap[PointIdx] - VertexOffset;
								if (!MorphTargetData.Points.IsValidIndex(OriginalPointIdx))
								{
									//We break if we get over a valid point it mean we are inspecting a different part of the mesh
									break;
								}
								//Rebuild the data with only the modified point
								if ((MorphTargetData.Points[OriginalPointIdx] - DestinationSkeletalMeshImportData.Points[PointIdx]).SizeSquared() > FMath::Square(THRESH_POINTS_ARE_SAME))
								{
									ModifiedPoints.Add(PointIdx);
									CompressPoints.Add(MorphTargetData.Points[OriginalPointIdx]);
								}
							}
						}
						MorphTargetData.Points = CompressPoints;
					}
				}
				, EParallelForFlags::BackgroundPriority);
				return;
			}

			const UInterchangeSceneNode* RecursiveFindJointByName(const UInterchangeBaseNodeContainer* NodeContainer, const FString& ParentJointNodeId, const FString& JointName)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RecursiveFindJointByName)
				if (const UInterchangeSceneNode* JointNode = Cast<UInterchangeSceneNode>(NodeContainer->GetNode(ParentJointNodeId)))
				{
					if (JointNode->GetDisplayLabel().Equals(JointName))
					{
						return JointNode;
					}
				}
				TArray<FString> NodeChildrenUids = NodeContainer->GetNodeChildrenUids(ParentJointNodeId);
				for (int32 ChildIndex = 0; ChildIndex < NodeChildrenUids.Num(); ++ChildIndex)
				{
					if (const UInterchangeSceneNode* JointNode = RecursiveFindJointByName(NodeContainer, NodeChildrenUids[ChildIndex], JointName))
					{
						return JointNode;
					}
				}

				return nullptr;
			}

			//We assume normalize weight method in this bind pose conversion
			void SkinVertexPositionToTimeZero(UE::Interchange::FMeshPayloadData& LodMeshPayload
				, const UInterchangeBaseNodeContainer* NodeContainer
				, const FString& RootJointNodeId
				, const UInterchangeMeshNode* MeshNode
				, const UInterchangeSceneNode* SceneNode
				, const FTransform& SceneNodeTransform)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SkinVertexPositionToTimeZero)
				FMeshDescription& MeshDescription = LodMeshPayload.MeshDescription;
				const int32 VertexCount = MeshDescription.Vertices().Num();
				const TArray<FString>& JointNames = LodMeshPayload.JointNames;
				// Create a copy of the vertex array to receive vertex deformations.
				TArray<FVector3f> DestinationVertexPositions;
				DestinationVertexPositions.AddZeroed(VertexCount);

				FSkeletalMeshAttributes Attributes(MeshDescription);
				TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
				FSkinWeightsVertexAttributesRef VertexSkinWeights = Attributes.GetVertexSkinWeights();

				for (FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
				{
					//We can use GetValue because the Meshdescription was compacted before the copy
					DestinationVertexPositions[VertexID.GetValue()] = VertexPositions[VertexID];
				}

				// Deform the vertex array with the links contained in the mesh.
				TArray<FMatrix> SkinDeformations;
				SkinDeformations.AddZeroed(VertexCount);

				TArray<double> SkinWeights;
				SkinWeights.AddZeroed(VertexCount);

				FTransform GlobalOffsetTransform = FTransform::Identity;
				if (UInterchangeCommonPipelineDataFactoryNode* CommonPipelineDataFactoryNode = UInterchangeCommonPipelineDataFactoryNode::GetUniqueInstance(NodeContainer))
				{
					CommonPipelineDataFactoryNode->GetCustomGlobalOffsetTransform(GlobalOffsetTransform);
				}

				//If we use transform, some scaling are not well supported, so we must do all the math with matrix
				FMatrix SceneNodeMatrixInverse = SceneNodeTransform.ToMatrixWithScale().Inverse();
				FMatrix GeometricMatrix = FMatrix::Identity;

				//Scope any transform variable to be sure they are not use to create the vertex transformation matrix
				{
					FTransform GeometricTransform;
					if (SceneNode->GetCustomGeometricTransform(GeometricTransform))
					{
						GeometricMatrix = GeometricTransform.ToMatrixWithScale();
					}
				}

				const int32 JointCount = JointNames.Num();
				for (int32 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
				{
					const FString& JointName = JointNames[JointIndex];

					const UInterchangeSceneNode* JointNode = RecursiveFindJointByName(NodeContainer, RootJointNodeId, JointName);
					if (!JointNode)
					{
						continue;
					}

					FMatrix JointBindPoseMatrix = FMatrix::Identity;;
					FMatrix JointT0Matrix = FMatrix::Identity;;
					FMatrix JointReferenceMatrix = FMatrix::Identity;;

					//Scope any transform variable to be sure they are not use to create the vertex transformation matrix
					{
						FString AttributeKey = TEXT("JointBindPosePerMesh_") + MeshNode->GetUniqueID();
						if (!JointNode->GetAttribute<FMatrix>(AttributeKey, JointBindPoseMatrix))
						{
							FTransform JointBindPose;
							if (!ensure(JointNode->GetCustomBindPoseGlobalTransform(NodeContainer, FTransform::Identity, JointBindPose)))
							{
								//BindPose will fall back on LocalTransform in case its not present.
								//If neither is present: No Value to convert from, skip this joint.
								continue;
							}
							JointBindPoseMatrix = JointBindPose.ToMatrixWithScale();
						}
					
						FTransform JointT0;
						if (!JointNode->GetCustomTimeZeroGlobalTransform(NodeContainer, FTransform::Identity, JointT0))
						{
							//If there is no time zero global transform we cannot set the bind pose to time zero.
							//We must skip this joint.
							continue;
						}
						JointT0Matrix = JointT0.ToMatrixWithScale();

						if (!JointNode->GetGlobalBindPoseReferenceForMeshUID(MeshNode->GetUniqueID(), JointReferenceMatrix))
						{
							//Skip this joint
							continue;
						}

						//We must add the geometric node transform
						JointReferenceMatrix = GeometricMatrix * JointReferenceMatrix;
					}
					
					FMatrix ReferenceMatrixRelativeToBindPose = JointReferenceMatrix * JointBindPoseMatrix.Inverse();
					FMatrix jointTimeZeroRelativeToMeshNodeMatrix = JointT0Matrix * SceneNodeMatrixInverse;
					//Compute the transform to apply to vertex to change the bind pose from the existing one to use time zero pose instead
					FMatrix VertexTransformMatrix = SceneNodeMatrixInverse * (ReferenceMatrixRelativeToBindPose * jointTimeZeroRelativeToMeshNodeMatrix);

					//Iterate all bone vertices
					for (FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
					{
						const int32 VertexIndex = VertexID.GetValue();
						const FVertexBoneWeights BoneWeights = VertexSkinWeights.Get(VertexID);
						const int32 InfluenceCount = BoneWeights.Num();
						float Weight = 0.0f;
						for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
						{
							FBoneIndexType BoneIndex = BoneWeights[InfluenceIndex].GetBoneIndex();
							if (JointIndex == BoneIndex)
							{
								Weight = BoneWeights[InfluenceIndex].GetWeight();
								break;
							}
						}
						if (FMath::IsNearlyZero(Weight))
						{
							continue;
						}

						//The weight multiply the vertex transform matrix so we can have multiple joint affecting this vertex.
						const FMatrix Influence = VertexTransformMatrix * Weight;
						//Add the weighted result
						SkinDeformations[VertexIndex] += Influence;
						//Add the total weight so we can normalize the result in case the accumulated weight is different then 1
						SkinWeights[VertexIndex] += Weight;
					}
				}

				for (FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
				{
					const int32 VertexIndex = VertexID.GetValue();
					const FVector lSrcVertex = FVector(DestinationVertexPositions[VertexIndex]);
					FVector3f& lDstVertex = DestinationVertexPositions[VertexIndex];
					double Weight = SkinWeights[VertexIndex];

					// Deform the vertex if there was at least a link with an influence on the vertex,
					if (!FMath::IsNearlyZero(Weight))
					{
						//Apply skinning of all joints
						lDstVertex = FVector4f(SkinDeformations[VertexIndex].TransformPosition(lSrcVertex));
						//Normalized, in case the weight is different then 1
						lDstVertex /= Weight;
						//Set the new vertex position in the mesh description
						VertexPositions[VertexID] = FVector3f(FVector4f(SceneNodeTransform.TransformFVector4(FVector(lDstVertex))));
					}
				}
			}

			void RetrieveAllSkeletalMeshPayloadsAndFillImportData(const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode
																  , FSkeletalMeshImportData& DestinationImportData
																  , TArray<FMeshNodeContext>& MeshReferences
																  , UInterchangeSkeletalMeshFactory::FLodPayloads& LodPayloads
																  , TArray<SkeletalMeshImportData::FBone>& RefBonesBinary
																  , const UInterchangeSkeletalMeshFactory::FImportAssetObjectParams& Arguments
																  , const bool bSkinControlPointToTimeZero
																  , const UInterchangeBaseNodeContainer* NodeContainer
																  , const FString& RootJointNodeId)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RetrieveAllSkeletalMeshPayloadsAndFillImportData)
				FMeshDescription LodMeshDescription;
				FSkeletalMeshAttributes SkeletalMeshAttributes(LodMeshDescription);
				SkeletalMeshAttributes.Register();
				FStaticMeshOperations::FAppendSettings AppendSettings;
				for (int32 ChannelIdx = 0; ChannelIdx < FStaticMeshOperations::FAppendSettings::MAX_NUM_UV_CHANNELS; ++ChannelIdx)
				{
					AppendSettings.bMergeUVChannels[ChannelIdx] = true;
				}
				
				bool bKeepSectionsSeparate = false;
				SkeletalMeshFactoryNode->GetCustomKeepSectionsSeparate(bKeepSectionsSeparate);

				bool bImportVertexAttributes = false;
				SkeletalMeshFactoryNode->GetCustomImportVertexAttributes(bImportVertexAttributes);

				bool bImportMorphTarget = true;
				SkeletalMeshFactoryNode->GetCustomImportMorphTarget(bImportMorphTarget);

				TMap<const FMeshNodeContext*, TOptional<UE::Interchange::FMeshPayloadData>> LodMeshPayloadPerTranslatorPayloadKey;
				LodMeshPayloadPerTranslatorPayloadKey.Reserve(MeshReferences.Num());

				TMap<FString, TOptional<UE::Interchange::FMeshPayloadData>> MorphTargetMeshDescriptionsPerMorphTargetName;
				int32 MorphTargetCount = 0;

				struct FInternalInstanceData
				{
					bool ScaleGreaterThenOne = false;
					int32 Count = 0;
					bool ShouldFetchWithTransform() const
					{
						return Count == 1 || ScaleGreaterThenOne;
					}
				};
				TMap<FString, FInternalInstanceData> MeshInstancesDatas;
				for (const FMeshNodeContext& MeshNodeContext : MeshReferences)
				{
					FInternalInstanceData& InstanceData = MeshInstancesDatas.FindOrAdd(MeshNodeContext.TranslatorPayloadKey.UniqueId);
					InstanceData.Count++;
					InstanceData.ScaleGreaterThenOne |= MeshNodeContext.SceneGlobalTransform->GetScale3D().GetAbs().GetMax() > 1.0;
				}

				for (const FMeshNodeContext& MeshNodeContext : MeshReferences)
				{
					//Add the payload entry key, the payload data will be fill later in bulk by the translator
					LodMeshPayloadPerTranslatorPayloadKey.Add(&MeshNodeContext, LodPayloads.MeshPayloadPerKey.FindChecked(MeshNodeContext.GetTranslatorAndTransformPayloadKey()));
					//Count the morph target dependencies so we can reserve the right amount
					MorphTargetCount += (bImportMorphTarget && MeshNodeContext.MeshNode) ? MeshNodeContext.MeshNode->GetMorphTargetDependeciesCount() : 0;
				}
				MorphTargetMeshDescriptionsPerMorphTargetName.Reserve(MorphTargetCount);

				//Fill the lod mesh description using all combined mesh part
				TArray<SkeletalMeshImportData::FMeshInfo> MeshInfos;
				int32 MeshInfoVertexOffset = 0;
				for(TPair<const FMeshNodeContext*, TOptional<UE::Interchange::FMeshPayloadData>>& MeshNodeContextAndFuture: LodMeshPayloadPerTranslatorPayloadKey)
				{
					if (!MeshNodeContextAndFuture.Key)
					{
						continue;
					}
					const FMeshNodeContext& MeshNodeContext = *MeshNodeContextAndFuture.Key;
					TRACE_CPUPROFILER_EVENT_SCOPE(RetrieveAllSkeletalMeshPayloadsAndFillImportData::GetPayload)
					TOptional<UE::Interchange::FMeshPayloadData> LodMeshPayload = MeshNodeContextAndFuture.Value;
					if (!LodMeshPayload.IsSet())
					{
						UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid skeletal mesh payload key [%s] for SkeletalMesh asset %s."), *MeshNodeContext.TranslatorPayloadKey.UniqueId, *Arguments.AssetName);
						continue;
					}

					// Generate the mesh info.
					if (MeshNodeContext.SceneNode && MeshNodeContext.MeshNode)
					{
						SkeletalMeshImportData::FMeshInfo MeshInfo;
						MeshInfo.Name = FName(MeshNodeContext.SceneNode->GetDisplayLabel());
						if (MeshNodeContext.MeshNode->GetCustomVertexCount(MeshInfo.NumVertices))
						{
							MeshInfo.StartImportedVertex = MeshInfoVertexOffset;
							MeshInfoVertexOffset += MeshInfo.NumVertices;
							MeshInfos.Add(MeshInfo);
						}
					}
					else
					{
						int32 NumVerts = 0;
						if (MeshNodeContext.MeshNode && MeshNodeContext.MeshNode->GetCustomVertexCount(NumVerts))
						{
							MeshInfoVertexOffset += NumVerts;
						}
					}
					
					const int32 VertexOffset = LodMeshDescription.Vertices().Num();

					FSkeletalMeshOperations::FSkeletalMeshAppendSettings SkeletalMeshAppendSettings;
					SkeletalMeshAppendSettings.bAppendVertexAttributes = bImportVertexAttributes;
					SkeletalMeshAppendSettings.SourceVertexIDOffset = VertexOffset;
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(RetrieveAllSkeletalMeshPayloadsAndFillImportData::CompactPayload)
						FElementIDRemappings ElementIDRemappings;
						LodMeshPayload->MeshDescription.Compact(ElementIDRemappings);
					}

					const bool bIsRigidMesh = LodMeshPayload->JointNames.Num() <= 0 && MeshNodeContext.SceneNode;
					if (bSkinControlPointToTimeZero && !bIsRigidMesh)
					{
						//We need to rebind the mesh at time 0. Skeleton joint have the time zero transform, so we need to apply the skinning to the mesh
						//With the skeleton transform at time zero
						SkinVertexPositionToTimeZero(LodMeshPayload.GetValue(), NodeContainer, RootJointNodeId, MeshNodeContext.MeshNode, MeshNodeContext.SceneNode, MeshNodeContext.SceneGlobalTransform.Get(FTransform::Identity));
					}

					const int32 RefBoneCount = RefBonesBinary.Num();
					
					//Remap the influence vertex index to point on the correct index
					if (LodMeshPayload->JointNames.Num() > 0)
					{
						const int32 LocalJointCount = LodMeshPayload->JointNames.Num();
						
						SkeletalMeshAppendSettings.SourceRemapBoneIndex.AddZeroed(LocalJointCount);
						for (int32 LocalJointIndex = 0; LocalJointIndex < LocalJointCount; ++LocalJointIndex)
						{
							SkeletalMeshAppendSettings.SourceRemapBoneIndex[LocalJointIndex] = LocalJointIndex;
							const FString& LocalJointName = LodMeshPayload->JointNames[LocalJointIndex];
							for (int32 RefBoneIndex = 0; RefBoneIndex < RefBoneCount; ++RefBoneIndex)
							{
								const SkeletalMeshImportData::FBone& Bone = RefBonesBinary[RefBoneIndex];
								if (Bone.Name.Equals(LocalJointName))
								{
									SkeletalMeshAppendSettings.SourceRemapBoneIndex[LocalJointIndex] = RefBoneIndex;
									break;
								}
							}
						}
					}
					else if(bIsRigidMesh)
					{
						// We have a rigid mesh instance (a scene node point on the mesh, the scene node will be the bone on which the rigid mesh is skin).
						// We must add skinning to the mesh description on bone 0 and remap it to the correct RefBonesBinary in the append settings
						const FString ToSkinBoneName = MeshNodeContext.SceneNode->GetDisplayLabel();
						for (int32 RefBoneIndex = 0; RefBoneIndex < RefBoneCount; ++RefBoneIndex)
						{
							const SkeletalMeshImportData::FBone& Bone = RefBonesBinary[RefBoneIndex];
							if (Bone.Name.Equals(ToSkinBoneName))
							{
								SkeletalMeshAppendSettings.SourceRemapBoneIndex.AddZeroed_GetRef() = RefBoneIndex;
								break;
							}
						}
						//Add the skinning in the mesh description
						{
							FSkeletalMeshAttributes PayloadSkeletalMeshAttributes(LodMeshPayload->MeshDescription);
							constexpr bool bKeepExistingAttribute = true;
							PayloadSkeletalMeshAttributes.Register(bKeepExistingAttribute);
							using namespace UE::AnimationCore;
							TArray<FBoneWeight> BoneWeights;
							FBoneWeight& BoneWeight = BoneWeights.AddDefaulted_GetRef();
							BoneWeight.SetBoneIndex(0);
							BoneWeight.SetWeight(1.0f);
							FSkinWeightsVertexAttributesRef PayloadVertexSkinWeights = PayloadSkeletalMeshAttributes.GetVertexSkinWeights();
							for (const FVertexID& PayloadVertexID : LodMeshPayload->MeshDescription.Vertices().GetElementIDs())
							{
								PayloadVertexSkinWeights.Set(PayloadVertexID, BoneWeights);
							}
						}
					}
					
					//The Mesh node parent bake transform can be pass to the payload request or not, it depend on the count of instance and the scale of the transform.
					const FInternalInstanceData & InstanceData = MeshInstancesDatas.FindChecked(MeshNodeContext.TranslatorPayloadKey.UniqueId);
					AppendSettings.MeshTransform = InstanceData.ShouldFetchWithTransform() ? FTransform::Identity : MeshNodeContext.SceneGlobalTransform.Get(FTransform::Identity);
					if (bKeepSectionsSeparate)
					{
						AppendSettings.PolygonGroupsDelegate = FAppendPolygonGroupsDelegate::CreateLambda([](const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, PolygonGroupMap& RemapPolygonGroup)
							{
								UE::Interchange::Private::MeshHelper::RemapPolygonGroups(SourceMesh, TargetMesh, RemapPolygonGroup);
							});
					}

					FStaticMeshOperations::AppendMeshDescription(LodMeshPayload->MeshDescription, LodMeshDescription, AppendSettings);
					if (MeshNodeContext.MeshNode->IsSkinnedMesh() || bIsRigidMesh)
					{
						FSkeletalMeshOperations::AppendSkinWeight(LodMeshPayload->MeshDescription, LodMeshDescription, SkeletalMeshAppendSettings);
					}
					if (bImportMorphTarget)
					{
						FillMorphTargetMeshDescriptionsPerMorphTargetName(MeshNodeContext
																		, MorphTargetMeshDescriptionsPerMorphTargetName
																		, LodPayloads
																		, VertexOffset
																		, Arguments.NodeContainer
																		, Arguments.AssetName);
					}
				}

				//The color data is linearized twice by the translator, we need to convert to sRGB to have proper linear
				//TODO: change the translator to put linear instead of linear of linear
				//      remove the ToFColor in StaticMeshBuilder
				//      version the meshdescription properly to force the ToFColor when loading old static meshdescription
				{
					TVertexInstanceAttributesRef<FVector4f> VertexColor = SkeletalMeshAttributes.GetVertexInstanceColors();
					for (FVertexInstanceID VertexInstanceID : LodMeshDescription.VertexInstances().GetElementIDs())
					{
						const FColor LinearUint8Color = FLinearColor(VertexColor[VertexInstanceID]).ToFColor(true);
						constexpr float ColorScale = (1.0f / 255.0f);
						VertexColor[VertexInstanceID] = FVector4f(static_cast<float>(LinearUint8Color.R) * ColorScale
							, static_cast<float>(LinearUint8Color.G) * ColorScale
							, static_cast<float>(LinearUint8Color.B) * ColorScale
							, static_cast<float>(LinearUint8Color.A) * ColorScale);
					}
				}

				DestinationImportData = FSkeletalMeshImportData::CreateFromMeshDescription(LodMeshDescription);
				DestinationImportData.RefBonesBinary = RefBonesBinary;
				DestinationImportData.MeshInfos = MoveTemp(MeshInfos);

				bool bMergeMorphTargetWithSameName = false;
				SkeletalMeshFactoryNode->GetCustomMergeMorphTargetShapeWithSameName(bMergeMorphTargetWithSameName);
				//Copy all the lod morph targets data to the DestinationImportData.
				CopyMorphTargetsMeshDescriptionToSkeletalMeshImportData(MorphTargetMeshDescriptionsPerMorphTargetName, DestinationImportData, bMergeMorphTargetWithSameName);
			}

			void ProcessImportMeshInfluences(const int32 WedgeCount, TArray<SkeletalMeshImportData::FRawBoneInfluence>& Influences)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ProcessImportMeshInfluences)
				// Sort influences by vertex index.
				struct FCompareVertexIndex
				{
					bool operator()(const SkeletalMeshImportData::FRawBoneInfluence& A, const SkeletalMeshImportData::FRawBoneInfluence& B) const
					{
						if (A.VertexIndex > B.VertexIndex) return false;
						else if (A.VertexIndex < B.VertexIndex) return true;
						else if (A.Weight < B.Weight) return false;
						else if (A.Weight > B.Weight) return true;
						else if (A.BoneIndex > B.BoneIndex) return false;
						else if (A.BoneIndex < B.BoneIndex) return true;
						else									  return  false;
					}
				};
				Influences.Sort(FCompareVertexIndex());

				TArray <SkeletalMeshImportData::FRawBoneInfluence> NewInfluences;
				int32	LastNewInfluenceIndex = 0;
				int32	LastVertexIndex = INDEX_NONE;
				int32	InfluenceCount = 0;

				float TotalWeight = 0.f;
				int MaxVertexInfluence = 0;
				float MaxIgnoredWeight = 0.0f;

				//We have to normalize the data before filtering influences
				//Because influence filtering is base on the normalize value.
				//Some DCC like Daz studio don't have normalized weight
				for (int32 i = 0; i < Influences.Num(); i++)
				{
					// if less than min weight, or it's more than 8, then we clear it to use weight
					InfluenceCount++;
					TotalWeight += Influences[i].Weight;
					// we have all influence for the same vertex, normalize it now
					if (i + 1 >= Influences.Num() || Influences[i].VertexIndex != Influences[i + 1].VertexIndex)
					{
						// Normalize the last set of influences.
						if (InfluenceCount && (TotalWeight != 1.0f))
						{
							float OneOverTotalWeight = 1.f / TotalWeight;
							for (int r = 0; r < InfluenceCount; r++)
							{
								Influences[i - r].Weight *= OneOverTotalWeight;
							}
						}

						if (MaxVertexInfluence < InfluenceCount)
						{
							MaxVertexInfluence = InfluenceCount;
						}

						// clear to count next one
						InfluenceCount = 0;
						TotalWeight = 0.f;
					}

					if (InfluenceCount > MAX_TOTAL_INFLUENCES && Influences[i].Weight > MaxIgnoredWeight)
					{
						MaxIgnoredWeight = Influences[i].Weight;
					}
				}

				// warn about too many influences
				if (MaxVertexInfluence > MAX_TOTAL_INFLUENCES)
				{
					//TODO log a display message to the user
					//UE_LOG(LogLODUtilities, Display, TEXT("Skeletal mesh (%s) influence count of %d exceeds max count of %d. Influence truncation will occur. Maximum Ignored Weight %f"), *MeshName, MaxVertexInfluence, MAX_TOTAL_INFLUENCES, MaxIgnoredWeight);
				}

				for (int32 i = 0; i < Influences.Num(); i++)
				{
					// we found next verts, normalize it now
					if (LastVertexIndex != Influences[i].VertexIndex)
					{
						// Normalize the last set of influences.
						if (InfluenceCount && (TotalWeight != 1.0f))
						{
							float OneOverTotalWeight = 1.f / TotalWeight;
							for (int r = 0; r < InfluenceCount; r++)
							{
								NewInfluences[LastNewInfluenceIndex - r].Weight *= OneOverTotalWeight;
							}
						}

						// now we insert missing verts
						if (LastVertexIndex != INDEX_NONE)
						{
							int32 CurrentVertexIndex = Influences[i].VertexIndex;
							for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
							{
								// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
								LastNewInfluenceIndex = NewInfluences.AddUninitialized();
								NewInfluences[LastNewInfluenceIndex].VertexIndex = j;
								NewInfluences[LastNewInfluenceIndex].BoneIndex = 0;
								NewInfluences[LastNewInfluenceIndex].Weight = 1.f;
							}
						}

						// clear to count next one
						InfluenceCount = 0;
						TotalWeight = 0.f;
						LastVertexIndex = Influences[i].VertexIndex;
					}

					// if less than min weight, or it's more than 12, then we clear it to use weight
					if (Influences[i].Weight >= UE::AnimationCore::BoneWeightThreshold &&
						InfluenceCount < MAX_TOTAL_INFLUENCES)
					{
						LastNewInfluenceIndex = NewInfluences.Add(Influences[i]);
						InfluenceCount++;
						TotalWeight += Influences[i].Weight;
					}
				}

				Influences = NewInfluences;

				// Ensure that each vertex has at least one influence as e.g. CreateSkinningStream relies on it.
				// The below code relies on influences being sorted by vertex index.
				if (Influences.Num() == 0)
				{
					// warn about no influences
					//TODO add a user log
					//UE_LOG(LogLODUtilities, Warning, TEXT("Warning skeletal mesh (%s) has no vertex influences"), *MeshName);
					// add one for each wedge entry
					Influences.AddUninitialized(WedgeCount);
					for (int32 WedgeIdx = 0; WedgeIdx < WedgeCount; WedgeIdx++)
					{
						Influences[WedgeIdx].VertexIndex = WedgeIdx;
						Influences[WedgeIdx].BoneIndex = 0;
						Influences[WedgeIdx].Weight = 1.0f;
					}
					for (int32 i = 0; i < Influences.Num(); i++)
					{
						int32 CurrentVertexIndex = Influences[i].VertexIndex;

						if (LastVertexIndex != CurrentVertexIndex)
						{
							for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
							{
								// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
								Influences.InsertUninitialized(i, 1);
								Influences[i].VertexIndex = j;
								Influences[i].BoneIndex = 0;
								Influences[i].Weight = 1.f;
							}
							LastVertexIndex = CurrentVertexIndex;
						}
					}
				}
			}

			/** Helper struct for the mesh component vert position octree */
			struct FSkeletalMeshVertPosOctreeSemantics
			{
				enum { MaxElementsPerLeaf = 16 };
				enum { MinInclusiveElementsPerNode = 7 };
				enum { MaxNodeDepth = 12 };

				typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

				/**
				 * Get the bounding box of the provided octree element. In this case, the box
				 * is merely the point specified by the element.
				 *
				 * @param	Element	Octree element to get the bounding box for
				 *
				 * @return	Bounding box of the provided octree element
				 */
				FORCEINLINE static FBoxCenterAndExtent GetBoundingBox(const FSoftSkinVertex& Element)
				{
					return FBoxCenterAndExtent(FVector(Element.Position), FVector::ZeroVector);
				}

				/**
				 * Determine if two octree elements are equal
				 *
				 * @param	A	First octree element to check
				 * @param	B	Second octree element to check
				 *
				 * @return	true if both octree elements are equal, false if they are not
				 */
				FORCEINLINE static bool AreElementsEqual(const FSoftSkinVertex& A, const FSoftSkinVertex& B)
				{
					return (A.Position == B.Position && A.UVs[0] == B.UVs[0]);
				}

				/** Ignored for this implementation */
				FORCEINLINE static void SetElementId(const FSoftSkinVertex& Element, FOctreeElementId2 Id)
				{
				}
			};
			typedef TOctree2<FSoftSkinVertex, FSkeletalMeshVertPosOctreeSemantics> TSKCVertPosOctree;

			void RemapSkeletalMeshVertexColorToImportData(const USkeletalMesh* SkeletalMesh, const int32 LODIndex, FSkeletalMeshImportData* SkelMeshImportData)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RemapSkeletalMeshVertexColorToImportData)
				//Make sure we have all the source data we need to do the remap
				if (!SkeletalMesh->GetImportedModel() || !SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex) || !SkeletalMesh->GetHasVertexColors())
				{
					return;
				}

				// Find the extents formed by the cached vertex positions in order to optimize the octree used later
				FBox Bounds(ForceInitToZero);
				SkelMeshImportData->bHasVertexColors = true;

				int32 WedgeNumber = SkelMeshImportData->Wedges.Num();
				for (int32 WedgeIndex = 0; WedgeIndex < WedgeNumber; ++WedgeIndex)
				{
					SkeletalMeshImportData::FVertex& Wedge = SkelMeshImportData->Wedges[WedgeIndex];
					const FVector Position = FVector(SkelMeshImportData->Points[Wedge.VertexIndex]);
					Bounds += Position;
				}

				TArray<FSoftSkinVertex> Vertices;
				SkeletalMesh->GetImportedModel()->LODModels[LODIndex].GetVertices(Vertices);
				for (int32 SkinVertexIndex = 0; SkinVertexIndex < Vertices.Num(); ++SkinVertexIndex)
				{
					const FSoftSkinVertex& SkinVertex = Vertices[SkinVertexIndex];
					Bounds += FVector(SkinVertex.Position);
				}

				TSKCVertPosOctree VertPosOctree(Bounds.GetCenter(), Bounds.GetExtent().GetMax());

				// Add each old vertex to the octree
				for (int32 SkinVertexIndex = 0; SkinVertexIndex < Vertices.Num(); ++SkinVertexIndex)
				{
					const FSoftSkinVertex& SkinVertex = Vertices[SkinVertexIndex];
					VertPosOctree.AddElement(SkinVertex);
				}

				TMap<int32, FVector3f> WedgeIndexToNormal;
				WedgeIndexToNormal.Reserve(WedgeNumber);
				for (int32 FaceIndex = 0; FaceIndex < SkelMeshImportData->Faces.Num(); ++FaceIndex)
				{
					const SkeletalMeshImportData::FTriangle& Triangle = SkelMeshImportData->Faces[FaceIndex];
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						WedgeIndexToNormal.Add(Triangle.WedgeIndex[Corner], Triangle.TangentZ[Corner]);
					}
				}

				// Iterate over each new vertex position, attempting to find the old vertex it is closest to, applying
				// the color of the old vertex to the new position if possible.
				for (int32 WedgeIndex = 0; WedgeIndex < WedgeNumber; ++WedgeIndex)
				{
					SkeletalMeshImportData::FVertex& Wedge = SkelMeshImportData->Wedges[WedgeIndex];
					const FVector Position = FVector(SkelMeshImportData->Points[Wedge.VertexIndex]);
					const FVector2f UV = Wedge.UVs[0];
					const FVector3f& Normal = WedgeIndexToNormal.FindChecked(WedgeIndex);

					TArray<FSoftSkinVertex> PointsToConsider;
					VertPosOctree.FindNearbyElements(Position, [&PointsToConsider](const FSoftSkinVertex& Vertex)
						{
							PointsToConsider.Add(Vertex);
						});

					if (PointsToConsider.Num() > 0)
					{
						//Get the closest position
						float MaxNormalDot = -MAX_FLT;
						float MinUVDistance = MAX_FLT;
						int32 MatchIndex = INDEX_NONE;
						for (int32 ConsiderationIndex = 0; ConsiderationIndex < PointsToConsider.Num(); ++ConsiderationIndex)
						{
							const FSoftSkinVertex& SkinVertex = PointsToConsider[ConsiderationIndex];
							const FVector2f& SkinVertexUV = SkinVertex.UVs[0];
							const float UVDistanceSqr = FVector2f::DistSquared(UV, SkinVertexUV);
							if (UVDistanceSqr < MinUVDistance)
							{
								MinUVDistance = FMath::Min(MinUVDistance, UVDistanceSqr);
								MatchIndex = ConsiderationIndex;
								MaxNormalDot = Normal | SkinVertex.TangentZ;
							}
							else if (FMath::IsNearlyEqual(UVDistanceSqr, MinUVDistance, KINDA_SMALL_NUMBER))
							{
								//This case is useful when we have hard edge that shared vertice, somtime not all the shared wedge have the same paint color
								//Think about a cube where each face have different vertex color.
								float NormalDot = Normal | SkinVertex.TangentZ;
								if (NormalDot > MaxNormalDot)
								{
									MaxNormalDot = NormalDot;
									MatchIndex = ConsiderationIndex;
								}
							}
						}
						if (PointsToConsider.IsValidIndex(MatchIndex))
						{
							Wedge.Color = PointsToConsider[MatchIndex].Color;
						}
					}
				}
			}

			struct FContentInfo
			{
				bool bApplyGeometry = false;
				bool bApplySkinning = false;
				bool bApplyPartialContent = false;
				bool bApplyGeometryOnly = false;
				bool bApplySkinningOnly = false;
			};

			FContentInfo GetContentInfo(const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode, const bool bIsReImport)
			{
				FContentInfo ContentInfo;
				EInterchangeSkeletalMeshContentType ImportContent = EInterchangeSkeletalMeshContentType::All;
				SkeletalMeshFactoryNode->GetCustomImportContentType(ImportContent);
				ContentInfo.bApplyGeometry = !bIsReImport || (ImportContent == EInterchangeSkeletalMeshContentType::All || ImportContent == EInterchangeSkeletalMeshContentType::Geometry);
				ContentInfo.bApplySkinning = !bIsReImport || (ImportContent == EInterchangeSkeletalMeshContentType::All || ImportContent == EInterchangeSkeletalMeshContentType::SkinningWeights);
				ContentInfo.bApplyPartialContent = bIsReImport && ImportContent != EInterchangeSkeletalMeshContentType::All;
				ContentInfo.bApplyGeometryOnly = ContentInfo.bApplyPartialContent && ContentInfo.bApplyGeometry;
				ContentInfo.bApplySkinningOnly = ContentInfo.bApplyPartialContent && ContentInfo.bApplySkinning;
				return ContentInfo;
			}

			bool BuildMeshReferences(const UInterchangeFactoryBase::FImportAssetObjectParams& Arguments
				, const FString& RootJointNodeId
				, UInterchangeSkeletalMeshFactory::FImportAssetObjectLODData& ImportAssetObjectLODDataRef
				, const FTransform& GlobalOffsetTransform
				, const UInterchangeSkeletalMeshLodDataNode* LodDataNode
				, const bool bBakeMeshes)
			{
				if (!LodDataNode)
				{
					return false;
				}
				const UInterchangeSceneNode* RootJointNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(RootJointNodeId));
				if (!RootJointNode)
				{
					return false;
				}
				if (!ImportAssetObjectLODDataRef.bUseTimeZeroAsBindPose)
				{
					bool bHasBoneWithoutBindPose = false;
					UE::Interchange::Private::FSkeletonHelper::RecursiveBoneHasBindPose(Arguments.NodeContainer, RootJointNodeId, bHasBoneWithoutBindPose);
					if (bHasBoneWithoutBindPose)
					{
						if (!GIsAutomationTesting && Arguments.Translator)
						{
							FString Filename = Arguments.SourceData ? Arguments.SourceData->GetFilename() : TEXT("No specified file");
							UInterchangeResultWarning_Generic* Message = Arguments.Translator->AddMessage<UInterchangeResultWarning_Generic>();
							Message->Text = NSLOCTEXT("InterchangeSkeletalMeshFactory", "CreatePayloadTasks_ForceRebindOfSkinWithTimeZeroPose", "Imported skeletal mesh has some invalid bind poses. Skeletal mesh skinning has been rebind using the time zero pose.");
							Message->SourceAssetName = Filename;
							Message->AssetFriendlyName = Arguments.AssetName;
							Message->AssetType = USkeletalMesh::StaticClass();
						}
						ImportAssetObjectLODDataRef.bUseTimeZeroAsBindPose = true;
					}
				}

				ImportAssetObjectLODDataRef.bDiffPose = false;
				TArray <Private::FJointInfo> JointInfos;
				TArray<FString> BoneNotBindNames;
				Private::FSkeletonHelper::RecursiveAddBones(Arguments.NodeContainer
					, RootJointNodeId
					, JointInfos
					, INDEX_NONE
					, ImportAssetObjectLODDataRef.RefBonesBinary
					, ImportAssetObjectLODDataRef.bUseTimeZeroAsBindPose
					, ImportAssetObjectLODDataRef.bDiffPose
					, BoneNotBindNames);

				FTransform RootJointNodeGlobalTransform;
				ensure(RootJointNode->GetCustomGlobalTransform(Arguments.NodeContainer, GlobalOffsetTransform, RootJointNodeGlobalTransform));
				FTransform RootJointNodeLocalTransform;
				ensure(RootJointNode->GetCustomLocalTransform(RootJointNodeLocalTransform));
				FTransform BakeToRootJointTransfromModifier = RootJointNodeGlobalTransform.Inverse() * RootJointNodeLocalTransform; //It is used for !bBakeMeshes, and so the GlobalTransform will be inversed out when multiplied into the CustomBindPoseGlobalTransform
				FTransform BakeFromRootJointTransfromModifier = RootJointNodeLocalTransform.Inverse() * RootJointNodeGlobalTransform * GlobalOffsetTransform.Inverse(); //GlobalOffseTransform will be added by the BindPoseGlobalTransform when bBakeMeshes && !bRootAncestorOfMeshDependency is used

				{
					// Temporary 5.5.2: fbx legacy has a special way to bake the skeletal mesh that do not fit the interchange standard.
					// This fix the issue with blender armature bone skip if the armature has transform not identity.
					if(const UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::GetUniqueInstance(Arguments.NodeContainer))
					{
						bool bUseSceneNodeGlobalTransform = false;
						SourceNode->GetBooleanAttribute(TEXT("SkeletalMesh-Bake-UseSceneNodeGlobalTransform"), bUseSceneNodeGlobalTransform);
						if (bUseSceneNodeGlobalTransform)
						{
							BakeFromRootJointTransfromModifier.SetIdentity();
						}
					}
				}

				//Scope to query the mesh node
				{
					TArray<FString> MeshUids;
					LodDataNode->GetMeshUids(MeshUids);
					ImportAssetObjectLODDataRef.MeshNodeContexts.Reserve(MeshUids.Num());
					for (const FString& MeshUid : MeshUids)
					{
						FMeshNodeContext MeshReference;
						MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshUid));
						if (!MeshReference.MeshNode)
						{
							//The reference is a scene node and we need to bake the geometry
							MeshReference.SceneNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(MeshUid));
							if (!ensure(MeshReference.SceneNode != nullptr))
							{
								continue;
							}
							FString MeshDependencyUid;
							MeshReference.SceneNode->GetCustomAssetInstanceUid(MeshDependencyUid);
							MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshDependencyUid));
							bool bRootAncestorOfSceneNode = Arguments.NodeContainer->GetIsAncestor(MeshReference.SceneNode->GetUniqueID(), RootJointNode->GetParentUid());
							//Cache the scene node global matrix, we will use this matrix to bake the vertices, add the node geometric mesh offset to this matrix to bake it properly
							FTransform SceneNodeTransform;
							if (!ImportAssetObjectLODDataRef.bUseTimeZeroAsBindPose || !MeshReference.SceneNode->GetCustomTimeZeroGlobalTransform(Arguments.NodeContainer, GlobalOffsetTransform, SceneNodeTransform))
							{
								ensure(MeshReference.SceneNode->GetCustomBindPoseGlobalTransform(Arguments.NodeContainer, GlobalOffsetTransform, SceneNodeTransform));
								if (bRootAncestorOfSceneNode)
								{
									if (!bBakeMeshes)
									{
										SceneNodeTransform *= BakeToRootJointTransfromModifier;
									}
								}
								else
								{
									if (bBakeMeshes)
									{
										SceneNodeTransform = BakeFromRootJointTransfromModifier * SceneNodeTransform;
									}
									else
									{
										SceneNodeTransform *= GlobalOffsetTransform.Inverse();
									}
								}
							}

							FTransform SceneNodeGeometricTransform;
							if (MeshReference.SceneNode->GetCustomGeometricTransform(SceneNodeGeometricTransform))
							{
								SceneNodeTransform = SceneNodeGeometricTransform * SceneNodeTransform;
							}
							MeshReference.SceneGlobalTransform = SceneNodeTransform;
						}
						else
						{
							MeshReference.SceneGlobalTransform = GlobalOffsetTransform;
						}

						if (!ensure(MeshReference.MeshNode != nullptr))
						{
							continue;
						}

						TOptional<FInterchangeMeshPayLoadKey> OptionalPayLoadKey = MeshReference.MeshNode->GetPayLoadKey();
						if (OptionalPayLoadKey.IsSet())
						{
							MeshReference.TranslatorPayloadKey = OptionalPayLoadKey.GetValue();
						}
						else
						{
							continue;
						}
						ImportAssetObjectLODDataRef.MeshNodeContexts.Add(MeshReference);
					}
				}
				return true;
			}
		} //Namespace Private
	} //namespace Interchange
} //namespace UE

#endif //#if WITH_EDITOR

UClass* UInterchangeSkeletalMeshFactory::GetFactoryClass() const
{
	return USkeletalMesh::StaticClass();
}

void UInterchangeSkeletalMeshFactory::CreatePayloadTasks(const FImportAssetObjectParams& Arguments, bool bAsync, TArray<TSharedPtr<UE::Interchange::FInterchangeTaskBase>>& PayloadTasks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::CreateAsset)

#if WITH_EDITOR && WITH_EDITORONLY_DATA
	using namespace UE::Interchange;

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return;
	}

	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return;
	}

	const IInterchangeMeshPayloadInterface* MeshTranslatorPayloadInterface = Cast<IInterchangeMeshPayloadInterface>(Arguments.Translator);
	if (!MeshTranslatorPayloadInterface)
	{
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "CreatePayloadTasks_TranslatorInterfaceMissing", "Cannot import skeletalMesh {0}, the translator {1} does not implement the IInterchangeSkeletalMeshPayloadInterface.")
			, FText::FromString(Arguments.AssetName)
			, FText::FromString(Arguments.Translator->GetName()));
		return;
	}

	FTransform GlobalOffsetTransform = FTransform::Identity;
	bool bBakeMeshes = false;
	if (UInterchangeCommonPipelineDataFactoryNode* CommonPipelineDataFactoryNode = UInterchangeCommonPipelineDataFactoryNode::GetUniqueInstance(Arguments.NodeContainer))
	{
		CommonPipelineDataFactoryNode->GetCustomGlobalOffsetTransform(GlobalOffsetTransform);
		CommonPipelineDataFactoryNode->GetBakeMeshes(bBakeMeshes);
	}

	bool bImportMorphTarget = true;
	SkeletalMeshFactoryNode->GetCustomImportMorphTarget(bImportMorphTarget);

	int32 LodCount = SkeletalMeshFactoryNode->GetLodDataCount();
	TArray<FString> LodDataUniqueIds;
	SkeletalMeshFactoryNode->GetLodDataUniqueIds(LodDataUniqueIds);
	ensure(LodDataUniqueIds.Num() == LodCount);
	PayloadsPerLodIndex.Reserve(LodCount);
	int32 CurrentLodIndex = 0;
	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		FString LodUniqueId = LodDataUniqueIds[LodIndex];
		const UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(Arguments.NodeContainer->GetNode(LodUniqueId));
		if (!LodDataNode)
		{
			continue;
		}

		FString SkeletonNodeUid;
		if (!LodDataNode->GetCustomSkeletonUid(SkeletonNodeUid))
		{
			continue;
		}
		const UInterchangeSkeletonFactoryNode* SkeletonNode = Cast<UInterchangeSkeletonFactoryNode>(Arguments.NodeContainer->GetNode(SkeletonNodeUid));
		if (!SkeletonNode)
		{
			continue;
		}

		FString RootJointNodeId;
		if (!SkeletonNode->GetCustomRootJointUid(RootJointNodeId))
		{
			continue;
		}

		const UInterchangeSceneNode* RootJointNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(RootJointNodeId));
		if (!RootJointNode)
		{
			continue;
		}

		FLodPayloads& LodPayloads = PayloadsPerLodIndex.FindOrAdd(LodIndex);

		FImportAssetObjectLODData& ImportAssetObjectLODData = ImportAssetObjectData.LodDatas.AddDefaulted_GetRef();
		ImportAssetObjectLODData.LodIndex = CurrentLodIndex;

		SkeletonNode->GetCustomUseTimeZeroForBindPose(ImportAssetObjectLODData.bUseTimeZeroAsBindPose);
		
		Private::BuildMeshReferences(
			Arguments
			, RootJointNodeId
			, ImportAssetObjectLODData
			, GlobalOffsetTransform
			, LodDataNode
			, bBakeMeshes
		);

		struct FInternalInstanceData
		{
			bool ScaleGreaterThenOne = false;
			int32 Count = 0;
			bool ShouldFetchWithTransform() const
			{
				return Count == 1 || ScaleGreaterThenOne;
			}
		};
		TMap<FString, FInternalInstanceData> MeshInstancesDatas;
		for (const FMeshNodeContext& MeshNodeContext : ImportAssetObjectLODData.MeshNodeContexts)
		{
			FInternalInstanceData& InstanceData = MeshInstancesDatas.FindOrAdd(MeshNodeContext.TranslatorPayloadKey.UniqueId);
			InstanceData.Count++;
			InstanceData.ScaleGreaterThenOne |= MeshNodeContext.SceneGlobalTransform->GetScale3D().GetAbs().GetMax() > 1.0;
		}
		
		//Reserve the correct amount since we point in the array for the lambda so array must not be resized at any moments after we create the tasks
		LodPayloads.MeshPayloadPerKey.Reserve(ImportAssetObjectLODData.MeshNodeContexts.Num());
		int32 MorphTargetCount = 0;
		for (const FMeshNodeContext& MeshNodeContext : ImportAssetObjectLODData.MeshNodeContexts)
		{
			//Count the morph target dependencies so we can reserve the right amount
			if (bImportMorphTarget)
			{
				MorphTargetCount += MeshNodeContext.MeshNode->GetMorphTargetDependeciesCount();
			}
		}
		LodPayloads.MorphPayloadPerKey.Reserve(MorphTargetCount);

		for (const FMeshNodeContext& MeshNodeContext : ImportAssetObjectLODData.MeshNodeContexts)
		{
			const FInternalInstanceData& InstanceData = MeshInstancesDatas.FindChecked(MeshNodeContext.TranslatorPayloadKey.UniqueId);
			FTransform ApplyTransformWhenFetchPayload = InstanceData.ShouldFetchWithTransform() ? MeshNodeContext.SceneGlobalTransform.Get(FTransform::Identity) : FTransform::Identity;
			//Create the payload task
			TOptional<FMeshPayloadData>& MeshPayload = LodPayloads.MeshPayloadPerKey.FindOrAdd(MeshNodeContext.GetTranslatorAndTransformPayloadKey());
			TSharedPtr<FInterchangeTaskLambda, ESPMode::ThreadSafe> TaskGetMeshPayload = MakeShared<FInterchangeTaskLambda, ESPMode::ThreadSafe>(bAsync ? EInterchangeTaskThread::AsyncThread : EInterchangeTaskThread::GameThread
				, [&MeshPayload, ApplyTransformWhenFetchPayload, MeshTranslatorPayloadInterface, PayLoadKey = MeshNodeContext.TranslatorPayloadKey]()
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::GetMeshPayloadDataTask);
					MeshPayload = MeshTranslatorPayloadInterface->GetMeshPayloadData(PayLoadKey, ApplyTransformWhenFetchPayload);
				});
			PayloadTasks.Add(TaskGetMeshPayload);

			//Count the morph target dependencies so we can reserve the right amount
			if (bImportMorphTarget)
			{
				TArray<FString> MorphTargetUids;
				MeshNodeContext.MeshNode->GetMorphTargetDependencies(MorphTargetUids);
				for (const FString& MorphTargetUid : MorphTargetUids)
				{
					if (const UInterchangeMeshNode* MorphTargetMeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MorphTargetUid)))
					{
						TOptional<FInterchangeMeshPayLoadKey> OptionalPayLoadKey = MorphTargetMeshNode->GetPayLoadKey();
						if (!OptionalPayLoadKey.IsSet())
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Empty LOD morph target mesh reference payload when importing SkeletalMesh asset %s."), *Arguments.AssetName);
							continue;
						}
						FInterchangeMeshPayLoadKey& MorphPayLoadKey = OptionalPayLoadKey.GetValue();
						FInterchangeMeshPayLoadKey GlobalMorphPayLoadKey = MeshNodeContext.GetMorphTargetAndTransformPayloadKey(MorphPayLoadKey);
						TOptional<FMeshPayloadData>& MorphPayload = LodPayloads.MorphPayloadPerKey.FindOrAdd(GlobalMorphPayLoadKey);
						TSharedPtr<FInterchangeTaskLambda, ESPMode::ThreadSafe> TaskGetMorphPayload = MakeShared<FInterchangeTaskLambda, ESPMode::ThreadSafe>(bAsync ? EInterchangeTaskThread::AsyncThread : EInterchangeTaskThread::GameThread
							, [&MorphPayload, ApplyTransformWhenFetchPayload, MeshTranslatorPayloadInterface, MorphPayLoadKey]()
							{
								TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::GetMeshMorphTargetPayloadDataTask);
								MorphPayload = MeshTranslatorPayloadInterface->GetMeshPayloadData(MorphPayLoadKey, ApplyTransformWhenFetchPayload);
							});
						PayloadTasks.Add(TaskGetMorphPayload);
					}
				}
			}
		}
		CurrentLodIndex++;
	}
#endif
}

UInterchangeFactoryBase::FImportAssetResult UInterchangeSkeletalMeshFactory::BeginImportAsset_GameThread(const FImportAssetObjectParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::BeginImportAssetObject_GameThread)

	FImportAssetResult ImportAssetResult;

#if WITH_EDITOR && WITH_EDITORONLY_DATA
	USkeletalMesh* SkeletalMesh = nullptr;
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return ImportAssetResult;
	}

	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return ImportAssetResult;
	}

	UObject* ExistingAsset = Arguments.ReimportObject;
	if (!ExistingAsset)
	{
		FSoftObjectPath ReferenceObject;
		if (SkeletalMeshFactoryNode->GetCustomReferenceObject(ReferenceObject))
		{
			ExistingAsset = ReferenceObject.TryLoad();
		}
	}

	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		SkeletalMesh = NewObject<USkeletalMesh>(Arguments.Parent, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else
	{
		//This is a reimport or an override, simply cast it to USkeletalMesh, the class has been verified by the caller (UE::Interchange::FTaskImportObject_GameThread::DoTask)
		SkeletalMesh = Cast<USkeletalMesh>(ExistingAsset);
	}
	
	//This should not happen
	if (!ensure(SkeletalMesh))
	{
		if (!Arguments.ReimportObject)
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create or find a SkeletalMesh asset named %s."), *Arguments.AssetName);
		}
		return ImportAssetResult;
	}
	
	SkeletalMesh->PreEditChange(nullptr);

	//Lock the skeletalmesh properties if the skeletal mesh already exist (re-import)
	if (ExistingAsset)
	{
		SkeletalMeshLockPropertiesEvent = SkeletalMesh->LockPropertiesUntil();
	}

	ImportAssetResult.ImportedObject = SkeletalMesh;

	//Make sure we can modify the skeletalmesh properties
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(SkeletalMesh);

	//This is consider has a re-import if we have a reimport object or if the object exist and have some valid LOD
	const bool bIsReImport = (Arguments.ReimportObject != nullptr) || (SkeletalMesh->GetLODNum() > 0);

	//Dirty the DDC Key for any imported Skeletal Mesh
	SkeletalMesh->InvalidateDeriveDataCacheGUID();
	USkeleton* SkeletonReference = nullptr;

	if (bIsReImport)
	{
		//Save all existing source data that are imported only by the editor UI

		int32 ExistingLodCount = SkeletalMesh->GetLODNum();

		//Skin weight profiles, The skin weight alternate data will be extracted when iterating the lods
		ImportAssetObjectData.ExistingSkinWeightProfileInfos = SkeletalMesh->GetSkinWeightProfiles();

		//Unbind clothing and save the data to rebind it later in the post import task
		SkeletalMesh->GetSkinWeightProfiles().Reset();
		for (int32 LodIndex = 0; LodIndex < ExistingLodCount; ++LodIndex)
		{
			FSkeletalMeshLODModel& BuildLODModel = SkeletalMesh->GetImportedModel()->LODModels[LodIndex];
			BuildLODModel.SkinWeightProfiles.Reset();

			//Store the LOD alternate skinning profile data
			FSkeletalMeshImportData& SkeletalMeshImportData = ImportAssetObjectData.ExistingAlternateImportDataPerLOD.AddDefaulted_GetRef();
			if (SkeletalMesh->HasMeshDescription(LodIndex))
			{
				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				SkeletalMesh->LoadLODImportedData(LodIndex, SkeletalMeshImportData);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}

			//Cloth
			TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ExistingClothingBindingsLod;
			FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ExistingClothingBindingsLod, LodIndex);
			ImportAssetObjectData.ExistingClothingBindings.Append(ExistingClothingBindingsLod);
		}
	}

	int32 LodCount = SkeletalMeshFactoryNode->GetLodDataCount();
	TArray<FString> LodDataUniqueIds;
	SkeletalMeshFactoryNode->GetLodDataUniqueIds(LodDataUniqueIds);
	ensure(LodDataUniqueIds.Num() == LodCount);
	int32 CurrentLodIndex = 0;

	UE::Interchange::Private::FContentInfo ContentInfo = UE::Interchange::Private::GetContentInfo(SkeletalMeshFactoryNode, bIsReImport);
	ImportAssetObjectData.bIsReImport = bIsReImport;
	ImportAssetObjectData.bApplyGeometryOnly = ContentInfo.bApplyGeometryOnly;

	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		FText WarningMessage_InvalidSkeleton = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "BeginImportAsset_GameThread_InvalidSkeletonLOD", "Invalid Skeleton LOD {0} when importing SkeletalMesh asset {1}.")
			, FText::AsNumber(LodIndex)
			, FText::FromString(Arguments.AssetName));
		FText WarningMessage_InvalidRootJoint = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "BeginImportAsset_GameThread_InvalidSkeletonRootJoint", "Invalid Skeleton LOD {0}'s Root Joint when importing SkeletalMesh asset {1}.")
			, FText::AsNumber(LodIndex)
			, FText::FromString(Arguments.AssetName));

		FString LodUniqueId = LodDataUniqueIds[LodIndex];
		const UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(Arguments.NodeContainer->GetNode(LodUniqueId));
		if (!LodDataNode)
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "BeginImportAsset_GameThread_InvalidLOD", "Invalid LOD {0} when importing SkeletalMesh asset {1}.")
				, FText::AsNumber(LodIndex)
				, FText::FromString(Arguments.AssetName));
			continue;
		}

		FString SkeletonNodeUid;
		if (!LodDataNode->GetCustomSkeletonUid(SkeletonNodeUid))
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidSkeleton;
			continue;
		}
		const UInterchangeSkeletonFactoryNode* SkeletonNode = Cast<UInterchangeSkeletonFactoryNode>(Arguments.NodeContainer->GetNode(SkeletonNodeUid));
		if (!SkeletonNode)
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidSkeleton;
			continue;
		}
		FSoftObjectPath SkeletonNodeReferenceObject;
		SkeletonNode->GetCustomReferenceObject(SkeletonNodeReferenceObject);

		FSoftObjectPath SpecifiedSkeleton;
		SkeletalMeshFactoryNode->GetCustomSkeletonSoftObjectPath(SpecifiedSkeleton);
		bool bSpecifiedSkeleton = SpecifiedSkeleton.IsValid();
		if (SkeletonReference == nullptr)
		{
			UObject* SkeletonObject = nullptr;

			if (SpecifiedSkeleton.IsValid())
			{
				SkeletonObject = SpecifiedSkeleton.TryLoad();
			}
			else if (SkeletonNodeReferenceObject.IsValid())
			{
				SkeletonObject = SkeletonNodeReferenceObject.TryLoad();
			}

			if (SkeletonObject)
			{
				SkeletonReference = Cast<USkeleton>(SkeletonObject);

			}

			if (!Arguments.ReimportObject)
			{
				//In case its a SkeletalMesh AssetReimport without the Skeleton, then we won't have SkeletonReference which is expected
				if (!ensure(SkeletonReference))
				{
					UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
					Message->Text = WarningMessage_InvalidSkeleton;
					break;
				}
			}
			
			ImportAssetObjectData.SkeletonReference = SkeletonReference;
		}

		FString RootJointNodeId;
		if (!SkeletonNode->GetCustomRootJointUid(RootJointNodeId))
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidRootJoint;
			continue;
		}

		const UInterchangeSceneNode* RootJointNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(RootJointNodeId));
		if (!RootJointNode)
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidRootJoint;
			continue;
		}
		if (!ImportAssetObjectData.LodDatas.IsValidIndex(CurrentLodIndex))
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "BeginImportAsset_GameThread_BadLodIndexPrecomputed", "Invalid precompute LOD {0} data when importing SkeletalMesh asset {1}.")
				, FText::AsNumber(LodIndex)
				, FText::FromString(Arguments.AssetName));
			continue;
		}
		FImportAssetObjectLODData& ImportAssetObjectLODData = ImportAssetObjectData.LodDatas[CurrentLodIndex];
		
		//Do not alter the skeletal mesh reference skeleton when importing geometry only
		FReferenceSkeleton RefSkeleton;
		UE::Interchange::Private::FSkeletonHelper::ProcessImportMeshSkeleton( Results
			, SkeletonReference
			, ContentInfo.bApplyGeometryOnly ? RefSkeleton : SkeletalMesh->GetRefSkeleton()
			, Arguments.NodeContainer
			, RootJointNodeId
			, ImportAssetObjectLODData.RefBonesBinary
			, ImportAssetObjectLODData.bUseTimeZeroAsBindPose
			, ImportAssetObjectLODData.bDiffPose);

		if (bSpecifiedSkeleton && !SkeletonReference->IsCompatibleMesh(SkeletalMesh))
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "BeginImportAsset_GameThread_IncompatibleSkeleton", "The skeleton {0} is incompatible with the imported LOD {1} skeletalmesh asset {2}.")
				, FText::FromString(SkeletonReference->GetName())
				, FText::AsNumber(LodIndex)
				, FText::FromString(Arguments.AssetName));
		}

		CurrentLodIndex++;
	}
#endif

	return ImportAssetResult;
}

UInterchangeFactoryBase::FImportAssetResult UInterchangeSkeletalMeshFactory::ImportAsset_Async(const FImportAssetObjectParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::CreateAsset)

	FImportAssetResult ImportAssetResult;

#if WITH_EDITOR && WITH_EDITORONLY_DATA
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return ImportAssetResult;
	}

	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return ImportAssetResult;
	}

	const IInterchangeMeshPayloadInterface* MeshTranslatorPayloadInterface = Cast<IInterchangeMeshPayloadInterface>(Arguments.Translator);
	if (!MeshTranslatorPayloadInterface)
	{
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "ImportAsset_Async_TranslatorInterfaceMissing", "Cannot import skeletalMesh {0}, the translator {1} does not implement the IInterchangeSkeletalMeshPayloadInterface.")
			, FText::FromString(Arguments.AssetName)
			, FText::FromString(Arguments.Translator->GetName()));
		return ImportAssetResult;
	}

	FText ErrorMessage_SkeletalMeshDontExist = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "ImportAsset_Async_AssetDontExist", "Could not import the SkeletalMesh asset {0}, because the asset do not exist.")
		, FText::FromString(Arguments.AssetName));

	UObject* SkeletalMeshObject = UE::Interchange::FFactoryCommon::AsyncFindObject(SkeletalMeshFactoryNode, GetFactoryClass(), Arguments.Parent, Arguments.AssetName);

	if (!SkeletalMeshObject)
	{
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = ErrorMessage_SkeletalMeshDontExist;
		return ImportAssetResult;
	}

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SkeletalMeshObject);
	if (!ensure(SkeletalMesh))
	{
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = ErrorMessage_SkeletalMeshDontExist;
		return ImportAssetResult;
	}

	//Make sure we can modify the skeletalmesh properties
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(SkeletalMesh);

	TMap<FString, TArray<FLODUtilities::FMorphTargetLodBackupData>> BackupImportedMorphTargetData;
	FLODUtilities::BackupCustomImportedMorphTargetData(SkeletalMesh, BackupImportedMorphTargetData);


	FTransform GlobalOffsetTransform = FTransform::Identity;
	bool bBakeMeshes = false;
	if (UInterchangeCommonPipelineDataFactoryNode* CommonPipelineDataFactoryNode = UInterchangeCommonPipelineDataFactoryNode::GetUniqueInstance(Arguments.NodeContainer))
	{
		CommonPipelineDataFactoryNode->GetCustomGlobalOffsetTransform(GlobalOffsetTransform);
		CommonPipelineDataFactoryNode->GetBakeMeshes(bBakeMeshes);
	}

	USkeleton* SkeletonReference = ImportAssetObjectData.SkeletonReference;
		
	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	if (!ImportAssetObjectData.bIsReImport)
	{
		if (!ensure(ImportedResource->LODModels.Num() == 0))
		{
			ImportedResource->LODModels.Empty();
		}
	}
	else
	{
		//When we re-import, we force the current skeletalmesh skeleton, to be specified and to be the reference
		FSoftObjectPath SpecifiedSkeleton = SkeletalMesh->GetSkeleton();
		SkeletalMeshFactoryNode->SetCustomSkeletonSoftObjectPath(SpecifiedSkeleton);
	}
			
	int32 LodCount = SkeletalMeshFactoryNode->GetLodDataCount();
	TArray<FString> LodDataUniqueIds;
	SkeletalMeshFactoryNode->GetLodDataUniqueIds(LodDataUniqueIds);
	ensure(LodDataUniqueIds.Num() == LodCount);
	int32 CurrentLodIndex = 0;

	UE::Interchange::Private::FContentInfo ContentInfo = UE::Interchange::Private::GetContentInfo(SkeletalMeshFactoryNode, ImportAssetObjectData.bIsReImport);
	
	if (ContentInfo.bApplySkinningOnly)
	{
		//Ignore vertex color when we import only the skinning
		constexpr bool bForceIgnoreVertexColor = true;
		SkeletalMeshFactoryNode->SetCustomVertexColorIgnore(bForceIgnoreVertexColor);
		constexpr bool bFalseSetting = false;
		SkeletalMeshFactoryNode->SetCustomVertexColorReplace(bFalseSetting);
	}


	//Call the mesh helper to create the missing material and to use the unmatched existing slot with the unmatch import slot
	{
		using namespace UE::Interchange::Private::MeshHelper;
		TMap<FString, FString> SlotMaterialDependencies;
		SkeletalMeshFactoryNode->GetSlotMaterialDependencies(SlotMaterialDependencies);
		SkeletalMeshFactorySetupAssetMaterialArray(SkeletalMesh->GetMaterials(), SlotMaterialDependencies, Arguments.NodeContainer, ImportAssetObjectData.bIsReImport);
	}

	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		FText WarningMessage_InvalidSkeleton = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "ImportAsset_Async_InvalidSkeletonLOD", "Invalid Skeleton LOD {0} when importing SkeletalMesh asset {1}")
			, FText::AsNumber(LodIndex)
			, FText::FromString(Arguments.AssetName));
		FText WarningMessage_InvalidRootJoint = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "ImportAsset_Async_InvalidSkeletonRootJoint", "Invalid Skeleton LOD {0} Root Joint when importing SkeletalMesh asset {1}")
			, FText::AsNumber(LodIndex)
			, FText::FromString(Arguments.AssetName));


		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::CreateAsset_LOD)

		FString LodUniqueId = LodDataUniqueIds[LodIndex];
		const UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(Arguments.NodeContainer->GetNode(LodUniqueId));
		if (!LodDataNode)
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "ImportAsset_Async_InvalidLOD", "Invalid LOD {0} when importing SkeletalMesh asset {1}.")
				, FText::AsNumber(LodIndex)
				, FText::FromString(Arguments.AssetName));
			continue;
		}

		FString SkeletonNodeUid;
		if (!LodDataNode->GetCustomSkeletonUid(SkeletonNodeUid))
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidSkeleton;
			continue;
		}
		const UInterchangeSkeletonFactoryNode* SkeletonNode = Cast<UInterchangeSkeletonFactoryNode>(Arguments.NodeContainer->GetNode(SkeletonNodeUid));
		if (!SkeletonNode)
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidSkeleton;
			continue;
		}

		FSoftObjectPath SpecifiedSkeleton;
		SkeletalMeshFactoryNode->GetCustomSkeletonSoftObjectPath(SpecifiedSkeleton);
		bool bSpecifiedSkeleton = SpecifiedSkeleton.IsValid();
		if (SkeletonReference == nullptr)
		{
			break;
		}

		FString RootJointNodeId;
		if (!SkeletonNode->GetCustomRootJointUid(RootJointNodeId))
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidRootJoint;
			continue;
		}
		
		const UInterchangeSceneNode* RootJointNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(RootJointNodeId));
		if (!RootJointNode)
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = WarningMessage_InvalidRootJoint;
			continue;
		}

		//We should have a valid lod payload data
		FLodPayloads& LodPayloads = PayloadsPerLodIndex.FindChecked(LodIndex);

		FImportAssetObjectLODData& ImportAssetObjectLODData = ImportAssetObjectData.LodDatas[CurrentLodIndex];
		ensure(ImportAssetObjectLODData.LodIndex == CurrentLodIndex);

		//Add the lod mesh data to the skeletalmesh
		FSkeletalMeshImportData SkeletalMeshImportData;
		const bool bSkinControlPointToTimeZero = ImportAssetObjectLODData.bUseTimeZeroAsBindPose && ImportAssetObjectLODData.bDiffPose;
		//Get all meshes and morph targets payload and fill the SkeletalMeshImportData structure
		UE::Interchange::Private::RetrieveAllSkeletalMeshPayloadsAndFillImportData(SkeletalMeshFactoryNode
																					, SkeletalMeshImportData
																					, ImportAssetObjectLODData.MeshNodeContexts
																					, LodPayloads
																					, ImportAssetObjectLODData.RefBonesBinary
																					, Arguments
																					, bSkinControlPointToTimeZero
																					, Arguments.NodeContainer
																					, RootJointNodeId);
		//////////////////////////////////////////////////////////////////////////
		//Manage vertex color, we want to use the translated source data
		//Replace -> do nothing
		//Ignore -> remove vertex color from import data (when we re-import, ignore have to put back the current mesh vertex color)
		//Override -> replace the vertex color by the override color
		{
			bool bReplaceVertexColor = false;
			SkeletalMeshFactoryNode->GetCustomVertexColorReplace(bReplaceVertexColor);
			if (!bReplaceVertexColor)
			{
				bool bIgnoreVertexColor = false;
				SkeletalMeshFactoryNode->GetCustomVertexColorIgnore(bIgnoreVertexColor);
				if (bIgnoreVertexColor)
				{
					if (ImportAssetObjectData.bIsReImport)
					{
						//Get the vertex color we have in the current asset, 
						UE::Interchange::Private::RemapSkeletalMeshVertexColorToImportData(SkeletalMesh, LodIndex, &SkeletalMeshImportData);
					}
					else
					{
						//Flush the vertex color
						SkeletalMeshImportData.bHasVertexColors = false;
						for (SkeletalMeshImportData::FVertex& Wedge : SkeletalMeshImportData.Wedges)
						{
							Wedge.Color = FColor::White;
						}
					}
				}
				else
				{
					FColor OverrideVertexColor;
					if (SkeletalMeshFactoryNode->GetCustomVertexColorOverride(OverrideVertexColor))
					{
						SkeletalMeshImportData.bHasVertexColors = true;
						for(SkeletalMeshImportData::FVertex& Wedge : SkeletalMeshImportData.Wedges)
						{
							Wedge.Color = OverrideVertexColor;
						}
					}
				}
			}

			if (ContentInfo.bApplyGeometry)
			{
				// Store whether or not this mesh has vertex colors
				SkeletalMesh->SetHasVertexColors(SkeletalMeshImportData.bHasVertexColors);
				SkeletalMesh->SetVertexColorGuid(SkeletalMesh->GetHasVertexColors() ? FGuid::NewGuid() : FGuid());
			}
		}

		if (ImportAssetObjectData.bIsReImport)
		{
			while (ImportedResource->LODModels.Num() <= CurrentLodIndex)
			{
				ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
			}
		}
		else
		{
			ensure(ImportedResource->LODModels.Add(new FSkeletalMeshLODModel()) == CurrentLodIndex);
		}

		auto AddLodInfo = [&SkeletalMesh]()
		{
			FSkeletalMeshLODInfo& NewLODInfo = SkeletalMesh->AddLODInfo();
			NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
			NewLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
			NewLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
			NewLODInfo.LODHysteresis = 0.02f;
			NewLODInfo.bImportWithBaseMesh = true;
		};

		if (ImportAssetObjectData.bIsReImport)
		{
			while (SkeletalMesh->GetLODNum() <= CurrentLodIndex)
			{
				AddLodInfo();
			}
		}
		else
		{
			AddLodInfo();
		}
		
		FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[CurrentLodIndex];

		UE::Interchange::Private::ProcessImportMeshInfluences(SkeletalMeshImportData.Wedges.Num(), SkeletalMeshImportData.Influences);

		if (ContentInfo.bApplyGeometryOnly)
		{
			FSkeletalMeshImportData::ReplaceSkeletalMeshRigImportData(SkeletalMesh, &SkeletalMeshImportData, CurrentLodIndex);
		}
		else if(ContentInfo.bApplySkinningOnly)
		{
			FSkeletalMeshImportData::ReplaceSkeletalMeshGeometryImportData(SkeletalMesh, &SkeletalMeshImportData, CurrentLodIndex);
		}

		//Store the existing material import data before updating it so we can remap properly the material  and section data
		TArray<FName> ExistingOriginalPerSectionMaterialImportName;
		if (ImportAssetObjectData.bIsReImport)
		{
			if (CurrentLodIndex != 0)
			{
				if (SkeletalMesh->HasMeshDescription(CurrentLodIndex))
				{
					FSkeletalMeshImportData LODImportData;
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					SkeletalMesh->LoadLODImportedData(CurrentLodIndex, LODImportData);
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
					for (int32 SectionIndex = 0; SectionIndex < LODImportData.Materials.Num(); ++SectionIndex)
					{
						ExistingOriginalPerSectionMaterialImportName.Add(FName(*LODImportData.Materials[SectionIndex].MaterialImportName));
					}
				}
			}
			else
			{
				TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
				//LOD 0 import data is reorder to the material array before when building the LOD 0
				for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
				{
					ExistingOriginalPerSectionMaterialImportName.Add(Materials[MaterialIndex].ImportedMaterialSlotName);
				}
			}
		}

		//Store the original fbx import data the SkeletalMeshImportData should not be modified after this
		{
			FMeshDescription MeshDescription;
			if (SkeletalMeshImportData.GetMeshDescription(nullptr, &SkeletalMesh->GetLODInfo(CurrentLodIndex)->BuildSettings, MeshDescription))
			{
				//Restore the morph target into the imported mesh description
				FLODUtilities::RestoreCustomImportedMorphTargetData(SkeletalMesh, CurrentLodIndex, MeshDescription, BackupImportedMorphTargetData);
				SkeletalMesh->CreateMeshDescription(CurrentLodIndex, MoveTemp(MeshDescription));
				SkeletalMesh->CommitMeshDescription(CurrentLodIndex);
			}
		}

		//Update the bounding box if we are importing the LOD 0
		if(CurrentLodIndex == 0)
		{
			FBox3f BoundingBox(SkeletalMeshImportData.Points.GetData(), SkeletalMeshImportData.Points.Num());
			const FVector3f BoundingBoxSize = BoundingBox.GetSize();
			if (SkeletalMeshImportData.Points.Num() > 2 && BoundingBoxSize.X < UE_THRESH_POINTS_ARE_SAME && BoundingBoxSize.Y < UE_THRESH_POINTS_ARE_SAME && BoundingBoxSize.Z < UE_THRESH_POINTS_ARE_SAME)
			{
				UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
				Message->Text = FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "ImportAsset_Async_ErrorMeshTooSmall", "The mesh {0} bounding box is smaller than the supported threshold[{1}]. All Vertices will be merge into one vertex.")
					, FText::FromString(Arguments.AssetName)
					, FText::AsNumber(UE_THRESH_POINTS_ARE_SAME));
			}
			FBoxSphereBounds BoxSphereBound((FBox)BoundingBox);
			SkeletalMesh->SetImportedBounds(FBoxSphereBounds((FBox)BoundingBox));
		}
		//Copy the data into the game thread structure so we can finish the import in the game thread callback
		ImportAssetObjectLODData.ExistingOriginalPerSectionMaterialImportName = ExistingOriginalPerSectionMaterialImportName;
		ImportAssetObjectLODData.ImportedMaterials = SkeletalMeshImportData.Materials;
		
		CurrentLodIndex++;
	}

	//Getting the file Hash will cache it into the source data
	Arguments.SourceData->GetFileContentHash();

	ImportAssetResult.ImportedObject = SkeletalMeshObject;
#endif

	return ImportAssetResult;
}

UInterchangeFactoryBase::FImportAssetResult UInterchangeSkeletalMeshFactory::EndImportAsset_GameThread(const FImportAssetObjectParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::EndImportAssetObject_GameThread)
	
	check(IsInGameThread());
	FImportAssetResult ImportAssetResult;

#if WITH_EDITOR && WITH_EDITORONLY_DATA
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return ImportAssetResult;
	}

	UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return ImportAssetResult;
	}

	const UClass* SkeletalMeshClass = SkeletalMeshFactoryNode->GetObjectClass();
	check(SkeletalMeshClass && SkeletalMeshClass->IsChildOf(GetFactoryClass()));

	//Get the skeletal mesh asset from the factory node or a find if the node was not set properly
	USkeletalMesh* SkeletalMesh = nullptr;
	FSoftObjectPath ReferenceObject;
	if (SkeletalMeshFactoryNode->GetCustomReferenceObject(ReferenceObject))
	{
		SkeletalMesh = Cast<USkeletalMesh>(ReferenceObject.TryLoad());
	}
	if (!SkeletalMesh)
	{
		SkeletalMesh = Cast<USkeletalMesh>(StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName));
	}
	
	if (!ensure(SkeletalMesh))
	{
		if (Arguments.ReimportObject == nullptr)
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Could not create SkeletalMesh asset %s."), *Arguments.AssetName);
		}
		else
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("Could not find reimported SkeletalMesh asset %s."), *Arguments.AssetName);
		}
		return ImportAssetResult;
	}

	//Make sure we can modify the skeletalmesh properties
	FSkinnedAssetAsyncBuildScope AsyncBuildScope(SkeletalMesh);

	//Finish the import in the game thread so we can pop dialog if needed
	if (ImportAssetObjectData.IsValid())
	{
		for (FImportAssetObjectLODData& ImportAssetObjectLODData : ImportAssetObjectData.LodDatas)
		{
			if (FSkeletalMeshLODInfo* LodInfo = SkeletalMesh->GetLODInfo(ImportAssetObjectLODData.LodIndex))
			{
				if (SkeletalMesh->GetMaterials().IsEmpty() && !ImportAssetObjectLODData.ImportedMaterials.IsEmpty() )
				{
					for (SkeletalMeshImportData::FMaterial ImportMaterial : ImportAssetObjectLODData.ImportedMaterials)
					{
						UMaterialInterface* MaterialInterface = ImportMaterial.Material.Get() ? ImportMaterial.Material.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
						SkeletalMesh->GetMaterials().Add(FSkeletalMaterial(MaterialInterface, FName(ImportMaterial.MaterialImportName), FName(ImportMaterial.MaterialImportName)));
					}
				}

				if (SkeletalMesh->GetMaterials().Num() > 0)
				{
					FLODUtilities::FSkeletalMeshMatchImportedMaterialsParameters Parameters;
					Parameters.bIsReImport = ImportAssetObjectData.bIsReImport;
					Parameters.LodIndex = ImportAssetObjectLODData.LodIndex;
					Parameters.SkeletalMesh = SkeletalMesh;
					Parameters.ImportedMaterials = &ImportAssetObjectLODData.ImportedMaterials;
					Parameters.ExistingOriginalPerSectionMaterialImportName = &ImportAssetObjectLODData.ExistingOriginalPerSectionMaterialImportName;
					FLODUtilities::MatchImportedMaterials(Parameters);
					//Flush the old LOD sections after we rematch the materials
					if (Parameters.bIsReImport)
					{
						if (FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel())
						{
							if (ImportedModel->LODModels.IsValidIndex(ImportAssetObjectLODData.LodIndex))
							{
								SkeletalMesh->GetImportedModel()->LODModels[ImportAssetObjectLODData.LodIndex].Sections.Empty();
							}
						}
					}
				}
			}
		}

		//Now that materials are matched we can re-order them and remove the unused.
		if (ImportAssetObjectData.bIsReImport)
		{
			FLODUtilities::ReorderMaterialSlotToBaseLod(SkeletalMesh);
			FLODUtilities::RemoveUnusedMaterialSlot(SkeletalMesh);
		}
	}

	if (USkeleton* SkeletonReference = ImportAssetObjectData.SkeletonReference)
	{
		if (SkeletalMesh->GetSkeleton() != SkeletonReference)
		{
			SkeletalMesh->SetSkeleton(SkeletonReference);
		}

		constexpr bool bShowProgress = false;
		if ((!ImportAssetObjectData.bApplyGeometryOnly || !ImportAssetObjectData.bIsReImport) && !SkeletonReference->MergeAllBonesToBoneTree(SkeletalMesh, bShowProgress))
		{
			TUniqueFunction<bool()> RecreateSkeleton = [this, WeakSkeletalMesh = TWeakObjectPtr<USkeletalMesh>(SkeletalMesh), WeakSkeleton = TWeakObjectPtr<USkeleton>(SkeletonReference)]()
			{

				check(IsInGameThread());

				USkeleton* SkeletonPtr = WeakSkeleton.Get();
				USkeletalMesh* SkeletalMeshPtr = WeakSkeletalMesh.Get();

				if (!SkeletonPtr || !SkeletalMeshPtr)
				{
					return false;
				}

				if (GIsRunningUnattendedScript)
				{
					UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
					Message->Text = NSLOCTEXT("InterchangeSkeletalMeshFactory", "ImportWithScriptIncompatibleSkeleton", "Interchange Import UInterchangeSkeletalMeshFactory::EndImportAssetObject_GameThread, cannot merge bone tree with the existing skeleton.");
					return false;
				}

				EAppReturnType::Type MergeBonesChoice = FMessageDialog::Open(EAppMsgType::YesNo
					, EAppReturnType::No
					, NSLOCTEXT("InterchangeSkeletalMeshFactory", "SkeletonFailed_BoneMerge", "Failed to merge bones.\n\n This can happen if significant hierarchical changes have been made,\nsuch as inserting a bone between nodes.\n\nWould you like to regenerate the Skeleton from this mesh? This may invalidate or require recompression of animation data.\n"));
				if (MergeBonesChoice == EAppReturnType::Yes)
				{
					//Allow this thread scope to read and write skeletalmesh locked properties
					FSkinnedAssetAsyncBuildScope AsyncBuildScope(SkeletalMeshPtr);

					if (SkeletonPtr->RecreateBoneTree(SkeletalMeshPtr))
					{
						TArray<const USkeletalMesh*> OtherSkeletalMeshUsingSkeleton;
						FString SkeletalMeshList;
						FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
						TArray<FAssetData> SkeletalMeshAssetData;

						FARFilter ARFilter;
						ARFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
						ARFilter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(SkeletonPtr).GetExportTextName());

						IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
						if (AssetRegistry.GetAssets(ARFilter, SkeletalMeshAssetData))
						{
							// look through all skeletalmeshes that uses this skeleton
							for (int32 AssetId = 0; AssetId < SkeletalMeshAssetData.Num(); ++AssetId)
							{
								FAssetData& CurAssetData = SkeletalMeshAssetData[AssetId];
								const USkeletalMesh* ExtraSkeletalMesh = Cast<USkeletalMesh>(CurAssetData.GetAsset());
								if (SkeletalMeshPtr != ExtraSkeletalMesh && IsValid(ExtraSkeletalMesh))
								{
									OtherSkeletalMeshUsingSkeleton.Add(ExtraSkeletalMesh);
									SkeletalMeshList += TEXT("\n") + ExtraSkeletalMesh->GetPathName();
								}
							}
						}
						if (OtherSkeletalMeshUsingSkeleton.Num() > 0)
						{
							FText MessageText = FText::Format(
								NSLOCTEXT("InterchangeSkeletalMeshFactory", "Skeleton_ReAddAllMeshes", "Would you like to merge all SkeletalMeshes using this skeleton to ensure all bones are merged? This will require to load those SkeletalMeshes.{0}")
								, FText::FromString(SkeletalMeshList));
							if (FMessageDialog::Open(EAppMsgType::YesNo, MessageText) == EAppReturnType::Yes)
							{
								// look through all skeletalmeshes that uses this skeleton
								for (const USkeletalMesh* ExtraSkeletalMesh : OtherSkeletalMeshUsingSkeleton)
								{
									// merge still can fail
									if (!SkeletonPtr->MergeAllBonesToBoneTree(ExtraSkeletalMesh, bShowProgress))
									{
										FMessageDialog::Open(EAppMsgType::Ok,
											FText::Format(NSLOCTEXT("InterchangeSkeletalMeshFactory", "SkeletonRegenError_RemergingBones", "Failed to merge SkeletalMesh '{0}'."), FText::FromString(ExtraSkeletalMesh->GetName())));
									}
								}
							}
						}
					}
				}
				return true;
			};

			if (IsInGameThread())
			{
				RecreateSkeleton();
			}
			else
			{
				//Wait until the skeleton is recreate on the game thread
				Async(EAsyncExecution::TaskGraphMainThread, MoveTemp(RecreateSkeleton)).Wait();
			}
		}

		//Cleanup the reference skeleton sockets that doesn't fit anymmore a bone name.
		TArray<TObjectPtr<USkeletalMeshSocket>>& Sockets = SkeletalMesh->GetMeshOnlySocketList();

		for (int32 SocketIndex = Sockets.Num()-1; SocketIndex >= 0; SocketIndex--)
		{
			// Find bone index the socket is attached to.
			USkeletalMeshSocket* Socket = Sockets[SocketIndex];
			int32 SocketBoneIndex = SkeletalMesh->GetRefSkeleton().FindBoneIndex(Socket->BoneName);
			// If this LOD does not contain the socket bone, abort import.
			if (SocketBoneIndex == INDEX_NONE)
			{
				Sockets.RemoveAt(SocketIndex, EAllowShrinking::No);
			}
		}
		Sockets.Shrink();
	}
	else
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Interchange Import UInterchangeSkeletalMeshFactory::EndImportAssetObject_GameThread, USkeleton* SkeletonReference is nullptr."));
	}

	if (ImportAssetObjectData.bIsReImport)
	{
		//We must reset the matrixs since CalculateInvRefMatrices only do the calculation if matrix count differ from the bone count.
		SkeletalMesh->GetRefBasesInvMatrix().Reset();
	}

	SkeletalMesh->CalculateInvRefMatrices();

	if (!ImportAssetObjectData.bIsReImport)
	{
		/** Apply all SkeletalMeshFactoryNode custom attributes to the skeletal mesh asset */
		SkeletalMeshFactoryNode->ApplyAllCustomAttributeToObject(SkeletalMesh);

		bool bCreatePhysicsAsset = false;
		SkeletalMeshFactoryNode->GetCustomCreatePhysicsAsset(bCreatePhysicsAsset);

		if (!bCreatePhysicsAsset)
		{
			FSoftObjectPath SpecifiedPhysicAsset;
			SkeletalMeshFactoryNode->GetCustomPhysicAssetSoftObjectPath(SpecifiedPhysicAsset);
			if (SpecifiedPhysicAsset.IsValid())
			{
				UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(SpecifiedPhysicAsset.TryLoad());
				SkeletalMesh->SetPhysicsAsset(PhysicsAsset);
			}
		}
	}
	else
	{
		//Apply the re import strategy 
		UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(SkeletalMesh->GetAssetImportData());
		UInterchangeFactoryBaseNode* PreviousNode = nullptr;
		if (InterchangeAssetImportData)
		{
			PreviousNode = InterchangeAssetImportData->GetStoredFactoryNode(InterchangeAssetImportData->NodeUniqueID);
		}
		UInterchangeFactoryBaseNode* CurrentNode = NewObject<UInterchangeSkeletalMeshFactoryNode>(GetTransientPackage());
		UInterchangeBaseNode::CopyStorage(SkeletalMeshFactoryNode, CurrentNode);
		CurrentNode->FillAllCustomAttributeFromObject(SkeletalMesh);
		UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(SkeletalMesh, PreviousNode, CurrentNode, SkeletalMeshFactoryNode);
	}

	//For UAnimSequences we also have to check the existance of USkeletalMeshes not just USkeletons.
	// (USkeletalMesh creation can fail while USkeletons can succeed still)
	SkeletalMeshFactoryNode->SetCustomReferenceObject(FSoftObjectPath(SkeletalMesh));

	ImportAssetResult.ImportedObject = SkeletalMesh;
#endif

	return ImportAssetResult;
}

void UInterchangeSkeletalMeshFactory::Cancel()
{
	if (SkeletalMeshLockPropertiesEvent)
	{
		SkeletalMeshLockPropertiesEvent->Trigger();
		SkeletalMeshLockPropertiesEvent = nullptr;
	}
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeSkeletalMeshFactory::SetupObject_GameThread(const FSetupObjectParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::SetupObject_GameThread)
	check(IsInGameThread());
	Super::SetupObject_GameThread(Arguments);

	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Arguments.ImportedObject);
		//Release the promise so we unlock the skeletalmesh properties
		if (SkeletalMeshLockPropertiesEvent)
		{
			SkeletalMeshLockPropertiesEvent->Trigger();
			SkeletalMeshLockPropertiesEvent = nullptr;
		}

		UAssetImportData* ImportDataPtr = SkeletalMesh->GetAssetImportData();
		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(SkeletalMesh
																										  , ImportDataPtr
																										  , Arguments.SourceData
																										  , Arguments.NodeUniqueID
																										  , Arguments.NodeContainer
																										  , Arguments.OriginalPipelines
																										  , Arguments.Translator);

		ImportDataPtr = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters, [&Arguments, SkeletalMesh](UInterchangeAssetImportData* AssetImportData)
			{
				auto GetSourceIndexFromContentType = [](const EInterchangeSkeletalMeshContentType& ImportContentType)->int32
				{
					return (ImportContentType == EInterchangeSkeletalMeshContentType::Geometry)
						? 1
						: (ImportContentType == EInterchangeSkeletalMeshContentType::SkinningWeights)
						? 2
						: 0;
				};

				auto GetSourceLabelFromSourceIndex = [](const int32 SourceIndex)->FString
				{
					return (SourceIndex == 1)
						? NSSkeletalMeshSourceFileLabels::GeometryText().ToString()
						: (SourceIndex == 2)
						? NSSkeletalMeshSourceFileLabels::SkinningText().ToString()
						: NSSkeletalMeshSourceFileLabels::GeoAndSkinningText().ToString();
				};

				if (const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<const UInterchangeSkeletalMeshFactoryNode>(Arguments.NodeContainer->GetFactoryNode(Arguments.NodeUniqueID)))
				{
					EInterchangeSkeletalMeshContentType ImportContentType = EInterchangeSkeletalMeshContentType::All;
					SkeletalMeshFactoryNode->GetCustomImportContentType(ImportContentType);
					FMD5Hash SourceFileHash = Arguments.SourceData->GetFileContentHash().Get(FMD5Hash());
					const FString& NewSourceFilename = Arguments.SourceData->GetFilename();
					const int32 NewSourceIndex = GetSourceIndexFromContentType(ImportContentType);
					//NewSourceIndex should be 0, 1 or 2 (All, Geo, Skinning)
					check(NewSourceIndex >= 0 && NewSourceIndex < 3);
					const TArray<FString> OldFilenames = AssetImportData->ScriptExtractFilenames();
					const FString DefaultFilename = OldFilenames.Num() > 0 ? OldFilenames[GetSourceIndexFromContentType(EInterchangeSkeletalMeshContentType::All)] : NewSourceFilename;
					for (int32 SourceIndex = 0; SourceIndex < 3; ++SourceIndex)
					{
						FString SourceLabel = GetSourceLabelFromSourceIndex(SourceIndex);
						if (SourceIndex == NewSourceIndex)
						{
							if (SourceIndex == GetSourceIndexFromContentType(EInterchangeSkeletalMeshContentType::All))
							{
								AssetImportData->Update(NewSourceFilename, SourceFileHash);
								break;
							}
							else
							{
								AssetImportData->ScriptedAddFilename(NewSourceFilename, SourceIndex, SourceLabel);
							}
						}
						else
						{
							//Extract filename create a default path if the FSourceFile::RelativeFilename is empty
							//We want to fill the entry with the base source file (SourceIndex 0, All) in this case.
							const bool bValidOldFilename = AssetImportData->SourceData.SourceFiles.IsValidIndex(SourceIndex)
								&& !AssetImportData->SourceData.SourceFiles[SourceIndex].RelativeFilename.IsEmpty()
								&& OldFilenames.IsValidIndex(SourceIndex);
							const FString& OldFilename = bValidOldFilename ? OldFilenames[SourceIndex] : DefaultFilename;
							AssetImportData->ScriptedAddFilename(OldFilename, SourceIndex, SourceLabel);
						}
					}
				}
			});

		
		SkeletalMesh->SetAssetImportData(ImportDataPtr);
#if WITH_EDITOR
		//Re-apply the alternate skinning data
		if (ImportAssetObjectData.ExistingSkinWeightProfileInfos.Num() > 0)
		{
			//Reimport alternate skin of the reimport lod only by looking at the factory node lod data count.
			int32 LodCount = SkeletalMesh->GetLODNum();
			if (const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<const UInterchangeSkeletalMeshFactoryNode>(Arguments.NodeContainer->GetFactoryNode(Arguments.NodeUniqueID)))
			{
				int32 ImportedLodCount = SkeletalMeshFactoryNode->GetLodDataCount();
				if (ImportedLodCount >  0 && ImportedLodCount <= SkeletalMesh->GetLODNum())
				{
					LodCount = ImportedLodCount;
				}
			}

			TSharedPtr<FInterchangeSkeletalMeshAlternateSkinWeightPostImportTask> SkeletalMeshPostImportTask = nullptr;
			TArray<FSkinWeightProfileInfo>& SkinProfiles = SkeletalMesh->GetSkinWeightProfiles();
			SkinProfiles = ImportAssetObjectData.ExistingSkinWeightProfileInfos;
			for (const FSkinWeightProfileInfo& ProfileInfo : SkinProfiles)
			{
				for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
				{
					if (!SkeletalMesh->IsValidLODIndex(LodIndex)
						|| !ImportAssetObjectData.ExistingAlternateImportDataPerLOD.IsValidIndex(LodIndex)
						|| !SkeletalMesh->HasMeshDescription(LodIndex))
					{
						continue;
					}
					const FSkeletalMeshLODInfo* LodInfo = SkeletalMesh->GetLODInfo(LodIndex);
					if (!LodInfo)
					{
						continue;
					}

					const FSkeletalMeshImportData& ExistingImportDataSrc = ImportAssetObjectData.ExistingAlternateImportDataPerLOD[LodIndex];

					const FString ProfileNameStr = ProfileInfo.Name.ToString();

					FSkeletalMeshImportData ImportDataDest;
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					SkeletalMesh->LoadLODImportedData(LodIndex, ImportDataDest);
					PRAGMA_ENABLE_DEPRECATION_WARNINGS

					int32 PointNumberDest = ImportDataDest.Points.Num();
					int32 VertexNumberDest = ImportDataDest.Points.Num();

					if (ExistingImportDataSrc.Points.Num() == PointNumberDest)
					{
						//Replace the data into the destination bulk data and save it, so the build dont complain until we reimport the new alternate skinning
						int32 ProfileIndex = 0;
						if (ImportDataDest.AlternateInfluenceProfileNames.Find(ProfileNameStr, ProfileIndex))
						{
							ImportDataDest.AlternateInfluenceProfileNames.RemoveAt(ProfileIndex);
							ImportDataDest.AlternateInfluences.RemoveAt(ProfileIndex);
						}
						int32 SrcProfileIndex = 0;
						if (ExistingImportDataSrc.AlternateInfluenceProfileNames.Find(ProfileNameStr, SrcProfileIndex))
						{
							ImportDataDest.AlternateInfluenceProfileNames.Add(ProfileNameStr);
							ImportDataDest.AlternateInfluences.Add(ExistingImportDataSrc.AlternateInfluences[SrcProfileIndex]);
						}

						//Resave the bulk data with the new or refreshed data
						PRAGMA_DISABLE_DEPRECATION_WARNINGS
						SkeletalMesh->SaveLODImportedData(LodIndex, ImportDataDest);
						PRAGMA_ENABLE_DEPRECATION_WARNINGS
					}

					//We must enqueue a post import task that will re-import all skin weight profile.
					if (!SkeletalMeshPostImportTask.IsValid())
					{
						SkeletalMeshPostImportTask = MakeShared<FInterchangeSkeletalMeshAlternateSkinWeightPostImportTask>(SkeletalMesh);
						SkeletalMeshPostImportTask->ReimportAlternateSkinWeightDelegate.BindLambda([](USkeletalMesh* SkeletalMesh, int32 LodIndex)
							{
								return FSkinWeightsUtilities::ReimportAlternateSkinWeight(SkeletalMesh, LodIndex);
							});
					}
					SkeletalMeshPostImportTask->AddLodToReimportAlternate(LodIndex);
				}
			}
			
			//Enqueue the task if needed
			if (SkeletalMeshPostImportTask.IsValid())
			{
				UInterchangeManager::GetInterchangeManager().EnqueuePostImportTask(SkeletalMeshPostImportTask);
			}
		}
#endif //WITH_EDITOR
	}
#endif //WITH_EDITORONLY_DATA
}

void UInterchangeSkeletalMeshFactory::BuildObject_GameThread(const FSetupObjectParams& Arguments, bool& OutPostEditchangeCalled)
{
	check(IsInGameThread());
	OutPostEditchangeCalled = false;
#if WITH_EDITOR
	if (Arguments.ImportedObject)
	{
		if (USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Arguments.ImportedObject))
		{
			//Start an async build of the staticmesh
			SkeletalMesh->Build();
		}
	}
#endif
}

void UInterchangeSkeletalMeshFactory::FinalizeObject_GameThread(const FSetupObjectParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::FinalizeObject_GameThread)
	Super::FinalizeObject_GameThread(Arguments);

#if WITH_EDITOR
	//This code works only on the game thread and is not asynchronous
	check(IsInGameThread());

	if (!ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Arguments.ImportedObject);

	//Rebinding cloth will rebuild the skeletalmesh
	//TODO: To avoid a second build we need to restore the clothing data before in SetupObject_GameThread.
	//to do this we need to put the cloth binding data in the import data use by the build (i.e. Meshdescription)
	if (ImportAssetObjectData.ExistingClothingBindings.Num() > 0)
	{
		//Make sure we rebuild the skeletal mesh after re-importing all skin weight
		FScopedSkeletalMeshPostEditChange ScopePostEditChange(SkeletalMesh);
		//Wait until the asset is finish building then lock the skeletal mesh properties to prevent the UI to update during the alternate skinning reimport
		FEvent* LockEvent = SkeletalMesh->LockPropertiesUntil();
		FSkinnedAssetAsyncBuildScope AsyncBuildScope(SkeletalMesh);
		for (ClothingAssetUtils::FClothingAssetMeshBinding& ExistingClothMeshBinding : ImportAssetObjectData.ExistingClothingBindings)
		{
			if (UClothingAssetCommon* ClothAssetCommon = ExistingClothMeshBinding.Asset)
			{
				ClothAssetCommon->RefreshBoneMapping(SkeletalMesh);
			}
		}
		FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
		for (int32 LodIndex = 0; LodIndex < ImportedResource->LODModels.Num(); ++LodIndex)
		{
			// Re-apply our clothing assets
			FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ImportAssetObjectData.ExistingClothingBindings, LodIndex);
		}
		//Release the skeletal mesh async properties
		LockEvent->Trigger();
	}
#endif //WITH_EDITOR
}

bool UInterchangeSkeletalMeshFactory::GetSourceFilenames(const UObject* Object, TArray<FString>& OutSourceFilenames) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::GetSourceFilenames)
#if WITH_EDITORONLY_DATA
	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
	{
		return UE::Interchange::FFactoryCommon::GetSourceFilenames(SkeletalMesh->GetAssetImportData(), OutSourceFilenames);
	}
#endif

	return false;
}

bool UInterchangeSkeletalMeshFactory::SetSourceFilename(const UObject* Object, const FString& SourceFilename, int32 SourceIndex) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::SetSourceFilename)
#if WITH_EDITORONLY_DATA
	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
	{
		const FString SourceLabel = USkeletalMesh::GetSourceFileLabelFromIndex(SourceIndex).ToString();
		return UE::Interchange::FFactoryCommon::SetSourceFilename(SkeletalMesh->GetAssetImportData(), SourceFilename, SourceIndex, SourceLabel);
	}
#endif

	return false;
}

void UInterchangeSkeletalMeshFactory::BackupSourceData(const UObject* Object) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::BackupSourceData)
#if WITH_EDITORONLY_DATA
	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
	{
		UE::Interchange::FFactoryCommon::BackupSourceData(SkeletalMesh->GetAssetImportData());
	}
#endif
}

void UInterchangeSkeletalMeshFactory::ReinstateSourceData(const UObject* Object) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::ReinstateSourceData)
#if WITH_EDITORONLY_DATA
		if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
		{
			UE::Interchange::FFactoryCommon::ReinstateSourceData(SkeletalMesh->GetAssetImportData());
		}
#endif
}
void UInterchangeSkeletalMeshFactory::ClearBackupSourceData(const UObject* Object) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::ClearBackupSourceData)
#if WITH_EDITORONLY_DATA
		if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
		{
			UE::Interchange::FFactoryCommon::ClearBackupSourceData(SkeletalMesh->GetAssetImportData());
		}
#endif
}

bool UInterchangeSkeletalMeshFactory::SetReimportSourceIndex(const UObject* Object, int32 SourceIndex) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeSkeletalMeshFactory::SetReimportSourceIndex)
#if WITH_EDITORONLY_DATA
	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
	{
		return UE::Interchange::FFactoryCommon::SetReimportSourceIndex(SkeletalMesh, SkeletalMesh->GetAssetImportData(), SourceIndex);
	}
#endif

	return false;
}

bool UInterchangeSkeletalMeshFactory::FImportAssetObjectData::IsValid() const
{
	if (LodDatas.Num() < 1)
	{
		return false;
	}
	for (const FImportAssetObjectLODData& ImportAssetObjectLODData : LodDatas)
	{
		if (ImportAssetObjectLODData.LodIndex == INDEX_NONE)
		{
			return false;
		}
#if WITH_EDITOR
		if (!bIsReImport && ImportAssetObjectLODData.ImportedMaterials.Num() < 1)
		{
			return false;
		}
#endif
	}

	return true;
}