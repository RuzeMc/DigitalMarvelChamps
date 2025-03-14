// Copyright Epic Games, Inc. All Rights Reserved.

#include "Expressions/Input/TG_Expression_Material.h"
#include "UObject/ObjectSaveContext.h"
#include "Misc/TransactionObjectEvent.h"

#if WITH_EDITOR
void UTG_Expression_Material::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// First catch if Material changes
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UTG_Expression_Material, Material))
	{
		UE_LOG(LogTextureGraph, VeryVerbose, TEXT("Material Expression PostEditChangeProperty."));
		SetMaterialInternal(Material);
		FeedbackPinValue(GET_MEMBER_NAME_CHECKED(UTG_Expression_Material, RenderedAttribute), RenderedAttribute);
	}
	// Second catch if AttributeName changes
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UTG_Expression_Material, RenderedAttribute))
	{
		UE_LOG(LogTextureGraph, VeryVerbose, TEXT("Material Expression PostEditChangeProperty."));
		SetRenderedAttribute(RenderedAttribute);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UTG_Expression_Material::PostEditUndo()
{
	// Make sure the signature is in sync after undo in case we undo a material assignment:
	// So recreate it internally without notifying, normally, the node's pins should match
	DynSignature.Reset();
	GetSignature();

	Super::PostEditUndo();
}

void UTG_Expression_Material::OnReferencedObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext)
{
	// every editor should check if your texture graph is dependent on the object being saved
	UMaterialInterface* MaterialBeingSaved = Cast<UMaterialInterface>(Object);
	// if object being saved is our linked TextureGraph, we re-create
	if (MaterialBeingSaved && Material && (MaterialBeingSaved == Material))
	{

		SetMaterialInternal(MaterialBeingSaved);
	}
}

#endif

UTG_Expression_Material::UTG_Expression_Material()
{
#if WITH_EDITOR
	// listener for UObject saves, so we can synchronise when linked TextureGraphs get updated 
	PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddUObject(this, &UTG_Expression_Material::OnReferencedObjectPreSave);
#endif
}
UTG_Expression_Material::~UTG_Expression_Material()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
#endif
}

void UTG_Expression_Material::Initialize()
{
	// If the referenced material is valid, then we need to recreate a dubplicate
	if (Material)
	{
		MaterialCopy = DuplicateObject(Material, this);
	}

	Super::Initialize();
	SetRenderedAttribute(RenderedAttribute); // reassign the RenderedAttribute to make sure it is the correct one
}

void UTG_Expression_Material::SetMaterialInternal(UMaterialInterface* InMaterial)
{
	if (!InMaterial)
	{
		Material = nullptr;
		MaterialCopy = nullptr;
	}
	else if (InMaterial->IsA<UMaterial>())
	{
		// create a material instance
		Material = InMaterial;
		MaterialCopy = DuplicateObject(Material, this);
	}
	else if (InMaterial->IsA<UMaterialInstance>())
	{
		Material = InMaterial;
		MaterialCopy = DuplicateObject(Material, this);
	}

	Super::SetMaterialInternal(MaterialCopy);

	SetRenderedAttribute(RenderedAttribute);
}


void UTG_Expression_Material::SetMaterial(UMaterialInterface* InMaterial)
{
	// This is the public setter of the material, 
	// This is NOT called if the Material is modified from the detail panel!!!
	// We catch that case in PostEditChangeProperty, which will call SetMaterialInternal 

	// If it is the same Material then avoid anymore work, we shoudl be good to go
	if (InMaterial == Material)
	{
		// Just check that the MaterialInstance is valid, if not reassign below
		if ((!Material) || (Material && MaterialCopy && MaterialInstance))
			return;
	}

	SetMaterialInternal(InMaterial);
}

void UTG_Expression_Material::SetRenderedAttribute(FName InRenderedAttribute)
{
	if (GetAvailableMaterialAttributeNames().Num())
	{
		int32 RenderAttributeIndex = GetAvailableMaterialAttributeNames().Find(InRenderedAttribute);
		if (RenderAttributeIndex == INDEX_NONE)
		{
			RenderedAttribute = GetAvailableMaterialAttributeNames()[0];
		}
		else
		{
			RenderedAttribute = InRenderedAttribute;
		}
	}
	else
	{
		RenderedAttribute = TEXT("None");
	}
	
}

bool UTG_Expression_Material::CanHandleAsset(UObject* Asset)
{
	const UMaterialInterface* Mat = Cast<UMaterialInterface>(Asset);
	
	return Mat !=nullptr;
}

void UTG_Expression_Material::SetAsset(UObject* Asset)
{
	if(UMaterialInterface* MaterialAsset = Cast<UMaterialInterface>(Asset); MaterialAsset != nullptr)
	{
		SetMaterial(MaterialAsset);
#if WITH_EDITOR
		// We need to find its property and trigger property change event manually.
		const auto SourcePin = GetParentNode()->GetInputPin("Material");

		check(SourcePin)
	
		if(SourcePin)
		{
			auto Property = SourcePin->GetExpressionProperty();
			PropertyChangeTriggered(Property, EPropertyChangeType::ValueSet);
		}
#endif
	}
}

void UTG_Expression_Material::SetTitleName(FName NewName)
{
	TitleName = NewName;
}

FName UTG_Expression_Material::GetTitleName() const
{
	return TitleName;
}

TArray<FName> UTG_Expression_Material::GetRenderAttributeOptions() const
{
	return GetAvailableMaterialAttributeNames();
}

EDrawMaterialAttributeTarget UTG_Expression_Material::GetRenderedAttributeId()
{
	if (GetAvailableMaterialAttributeNames().Num())
	{
		int32 RenderAttributeIndex = GetAvailableMaterialAttributeNames().Find(RenderedAttribute);
		if (RenderAttributeIndex == INDEX_NONE)
		{
			return GetAvailableMaterialAttributeIds()[0];
		}
		else
		{
			return GetAvailableMaterialAttributeIds()[RenderAttributeIndex];
		}
	}
	else
	{
		return EDrawMaterialAttributeTarget::Emissive;
	}
}

