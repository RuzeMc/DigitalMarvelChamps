// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataTable.h"
#include "Engine/Texture2DArray.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByName.h"

#include "CustomizableObjectNodeTable.generated.h"

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObjectLayout;
class UCustomizableObjectNodeRemapPins;
class UCustomizableObjectNodeRemapPinsByName;
class UCustomizableObjectNodeTable;
class UEdGraphPin;
class UObject;
class USkeletalMesh;
class UTexture2D;
class UTexture2DArray;
class UAnimInstance;
struct FGuid;
struct FSkeletalMaterial;
struct FSkelMeshSection;

/** Enum class for the different types of image pins */
UENUM()
enum class ETableTextureType : uint8
{
	PASSTHROUGH_TEXTURE = 0 UMETA(DisplayName = "Passthrough"),
	MUTABLE_TEXTURE = 1 UMETA(DisplayName = "Mutable")
};


/** Enum class for the different types of pin meshes */
enum class ETableMeshPinType : uint8
{
	NONE = 0,
	SKELETAL_MESH = 1,
	STATIC_MESH = 2
};


/** Enum to decide where the data comes from: Struct or Data Table*/
UENUM()
enum class ETableDataGatheringSource : uint8
{
	/** Gathers the information from a data table */
	ETDGM_DataTable = 0 UMETA(DisplayName = "Data Table"),

	/** When compiling the CO, it uses the asset registry to gather and generate a data table. It uses all the data tables found in the specified paths that are references of the selected structure.*/
	ETDGM_AssetRegistry = 1 UMETA(DisplayName = " Struct + Asset Registry")
};


USTRUCT()
struct FTableNodeColumnData
{
	GENERATED_USTRUCT_BODY()

	/** Anim Instance Column name related to this Mesh pin */
	UPROPERTY()
	FString AnimInstanceColumnName = "";

	/** Anim Slot Column name related to this Mesh pin */
	UPROPERTY()
	FString AnimSlotColumnName = "";

	/** Anim Tag Column name related to this Mesh pin */
	UPROPERTY()
	FString AnimTagColumnName = "";
};


/** Base class for all Table Pins. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTableObjectPinData : public UCustomizableObjectNodePinData
{
	GENERATED_BODY()

public:

	/** Id of the property associated to a struct column */
	UPROPERTY()
	FGuid StructColumnId;

	/** Name of the data table column related to the pin */
	UPROPERTY()
	FString ColumnName = "";

};


/** Additional data for Image pins. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTableImagePinData : public UCustomizableObjectNodeTableObjectPinData
{
	GENERATED_BODY()

public:
	// UObject interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;

	UPROPERTY(EditAnywhere, Category = NoCategory,  DisplayName = "Texture Parameter Mode")
	ETableTextureType ImageMode = ETableTextureType::MUTABLE_TEXTURE;

	UPROPERTY()
	TObjectPtr<UCustomizableObjectNodeTable> NodeTable = nullptr;

private:
	// Replaced by the more general bIsNot2DTexture
	UPROPERTY()
	bool bIsArrayTexture_DEPRECATED = false;

public:
	UPROPERTY()
	bool bIsNotTexture2D = false;
};


/** Additional data for Mesh pins. */
UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTableMeshPinData : public UCustomizableObjectNodeTableObjectPinData
{
	GENERATED_BODY()

public:

	/** Anim Instance Column name related to this Mesh pin */
	UPROPERTY()
	FString AnimInstanceColumnName_DEPRECATED = "";

	/** Anim Slot Column name related to this Mesh pin */
	UPROPERTY()
	FString AnimSlotColumnName_DEPRECATED = "";

	/** Anim Tag Column name related to this Mesh pin */
	UPROPERTY()
	FString AnimTagColumnName_DEPRECATED = "";

	UPROPERTY()
	FString MutableColumnName = "";

	/** LOD of the mesh related to this Mesh pin */
	UPROPERTY()
	int32 LOD = 0;

	/** Section Index (Surface Index) of the mesh related to this Mesh pin */
	UPROPERTY()
	int32 Material = 0;

	/** Layouts related to this Mesh pin */
	UPROPERTY()
	TArray< TObjectPtr<UCustomizableObjectLayout> > Layouts;
};


UCLASS()
class UCustomizableObjectNodeTableRemapPins : public UCustomizableObjectNodeRemapPinsByName
{
	GENERATED_BODY()
public:

	// Specific method to decide when two pins are equal
	virtual bool Equal(const UCustomizableObjectNode& Node, const UEdGraphPin& OldPin, const UEdGraphPin& NewPin) const override;

	// Method to use in the RemapPins step of the node reconstruction process
	virtual void RemapPins(const UCustomizableObjectNode& Node, const TArray<UEdGraphPin*>& OldPins, const TArray<UEdGraphPin*>& NewPins, TMap<UEdGraphPin*, UEdGraphPin*>& PinsToRemap, TArray<UEdGraphPin*>& PinsToOrphan) override;
};


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeTable : public UCustomizableObjectNode
{
public:
	
	GENERATED_BODY()

	// Name of the property parameter
	UPROPERTY(EditAnywhere, Category = TableProperties)
	FString ParameterName = "Default Name";

	/** If true, adds a "None" parameter option */
	UPROPERTY(EditAnywhere, Category = TableProperties)
	bool bAddNoneOption;

	/** If there is a bool column in the table, checked rows will not be compiled */
	UPROPERTY(EditAnywhere, Category = CompilationRestrictions)
	bool bDisableCheckedRows = true;

	/** Source where table gathers the data */
	UPROPERTY(EditAnywhere, Category = TableProperties)
	ETableDataGatheringSource TableDataGatheringMode = ETableDataGatheringSource::ETDGM_DataTable;

	// Pointer to the Data Table Asset represented in this node
	UPROPERTY(EditAnywhere, Category = TableProperties, meta = (DontUpdateWhileEditing, EditCondition = "TableDataGatheringMode == ETableDataGatheringSource::ETDGM_DataTable", EditConditionHides))
	TObjectPtr<UDataTable> Table = nullptr;

	// Pointer to the Struct Asset represented in this node
	UPROPERTY(EditAnywhere, Category = TableProperties, meta = (DontUpdateWhileEditing, EditCondition = "TableDataGatheringMode == ETableDataGatheringSource::ETDGM_AssetRegistry", EditConditionHides))
	TObjectPtr<UScriptStruct> Structure = nullptr;

	UPROPERTY(EditAnywhere, Category = TableProperties, meta = (EditCondition = "TableDataGatheringMode == ETableDataGatheringSource::ETDGM_AssetRegistry", EditConditionHides))
	TArray<FName> FilterPaths;
	
	/** Name of the column that contains the Version options. */
	UPROPERTY(EditAnywhere, Category = CompilationRestrictions)
	FName VersionColumn;

	/** Name of the row that will be used as default value. */
	UPROPERTY(EditAnywhere, Category = TableProperties)
	FName DefaultRowName;

	UPROPERTY(EditAnywhere, Category = UI, meta = (DisplayName = "Parameter UI Metadata"))
	FMutableParamUIMetadata ParamUIMetadata;

	/** Name of the column that contains the MutableUIMetadata of the row options. */
	UPROPERTY(EditAnywhere, Category = UI)
	FName ParamUIMetadataColumn;
	
	/** Name of the column that contains the asset to use its thumbnails as option thumbnails. */
	UPROPERTY(EditAnywhere, Category = UI)
	FName ThumbnailColumn;
	
	UPROPERTY(EditAnywhere, Category = UI, meta = (Tooltip = "Given a row, add all tags found in GameplayTag columns to its Parameter UI Metadata"))
	bool bGatherTags = true;

	/** Map to relate a Structure Column with its Data */
	UPROPERTY()
	TMap<FGuid, FTableNodeColumnData> ColumnDataMap;

public:

	// UObject interface
	virtual void PostLoad() override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
	// EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual bool GetCanRenameNode() const override { return true; }
	
	// UCustomizableObjectNode interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	bool IsNodeOutDatedAndNeedsRefresh() override;
	FString GetRefreshMessage() const override;
	virtual bool ProvidesCustomPinRelevancyTest() const override;
	virtual bool IsPinRelevant(const UEdGraphPin* Pin) const override;
	UCustomizableObjectNodeTableRemapPins* CreateRemapPinsDefault() const;
	virtual bool HasPinViewer() const override;
	virtual TSharedPtr<IDetailsView> CustomizePinDetails(const UEdGraphPin& Pin) const override;
	
	/*** Allows to perform work when remapping the pin data. */
	virtual void RemapPinsData(const TMap<UEdGraphPin*, UEdGraphPin*>& PinsToRemap) override;
	
	// Own interface	
	// Returns the reference Texture parameter from a Material
	UTexture2D* FindReferenceTextureParameter(const UEdGraphPin* Pin, FString ParameterImageName) const;

	// Methods to get the UVs of the skeletal mesh 
	UObject* GetDefaultMeshForLayout(const UCustomizableObjectLayout* Layout) const;

	// Methods to provide the Layouts to the Layout block editors
	TArray<UCustomizableObjectLayout*> GetLayouts(const UEdGraphPin* Pin) const;

	// Returns the name of the table column related to a pin
	FString GetColumnNameByPin(const UEdGraphPin* Pin) const;

	// Returns the LOD of the mesh associated to the input pin
	void GetPinLODAndSection(const UEdGraphPin* Pin, int32& LODIndex, int32& SectionIndex) const;

	// Get the anim blueprint and anim slot columns related to a mesh
	void GetAnimationColumns(const FGuid& ColumnId, FString& AnimBPColumnName, FString& AnimSlotColumnName, FString& AnimTagColumnName) const;

	template<class T> 
	T* GetColumnDefaultAssetByType(FString ColumnName) const
	{
		T* ObjectToReturn = nullptr;
		const UScriptStruct* TableStruct = nullptr;

		if (TableDataGatheringMode == ETableDataGatheringSource::ETDGM_AssetRegistry)
		{
			TableStruct = Structure;
		}
		else
		{
			if (Table)
			{
				TableStruct = Table->GetRowStruct();
			}
		}

		if (TableStruct)
		{
			// Getting Default Struct Values
			// A Script Struct always has at leaset one property
			TArray<int8> DeafaultDataArray;
			DeafaultDataArray.SetNumZeroed(TableStruct->GetStructureSize());
			TableStruct->InitializeStruct(DeafaultDataArray.GetData());

			FProperty* Property = FindTableProperty(TableStruct, FName(*ColumnName));

			if (Property)
			{
				if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
				{
					UObject* Object = nullptr;
					
					// Getting default UObject
					uint8* CellData = SoftObjectProperty->ContainerPtrToValuePtr<uint8>(DeafaultDataArray.GetData());

					if (CellData)
					{
						Object = SoftObjectProperty->GetPropertyValue(CellData).LoadSynchronous();
					}

					if (Object)
					{
						if (Object->IsA(T::StaticClass()))
						{
							ObjectToReturn = Cast <T>(Object);
						}
					}
				}
			}

			// Cleaning Default Structure Pointer
			TableStruct->DestroyStruct(DeafaultDataArray.GetData());
		}

		return ObjectToReturn;
	}

	template<class T>
	T* GetColumnDefaultAssetByType(const UEdGraphPin* Pin) const
	{
		if (Pin)
		{
			FString ColumnName = GetColumnNameByPin(Pin);

			if (ColumnName != FString())
			{
				return GetColumnDefaultAssetByType<T>(ColumnName);
			}
		}

		return nullptr;
	}

	// Generation Mutable Source Methods
	// We should do this in a template!
	FSoftObjectPtr GetSkeletalMeshAt(const UEdGraphPin* Pin, const UDataTable* DataTable, const FName& RowName) const;
	TSoftClassPtr<UAnimInstance> GetAnimInstanceAt(const UEdGraphPin* Pin, const UDataTable* DataTable, const FName& RowName) const;
	
	// Returns the image mode of the column
	ETableTextureType GetColumnImageMode(const FString& ColumnName) const;

	// Returns the mesh type of the Pin
	ETableMeshPinType GetPinMeshType(const UEdGraphPin* Pin) const;

	// Functions to generate the names of a mutable table's column
	FString GenerateSkeletalMeshMutableColumName(const FString& PinName, int32 LODIndex, int32 MaterialIndex) const;
	FString GenerateStaticMeshMutableColumName(const FString& PinName, int32 MaterialIndex) const;

	/** Returns the struct pointer used to gather data */
	const UScriptStruct* GetTableNodeStruct() const;

	// Methods from UDataTable but modified to support ScriptStructs
	/** Get an array of all the column titles, using the friendly display name from the property */
	TArray<FString> GetColumnTitles(const UScriptStruct* ScriptStruct) const;
	
	/** Returns the column property where PropertyName matches the name of the column property. Returns nullptr if no match is found or the match is not a supported table property */
	FProperty* FindTableProperty(const UScriptStruct* ScriptStruct, const FName& PropertyName) const;

	/** Returns the column id */
	FGuid GetColumnIdByName(const FName& ColumnName) const;

	/** Return the list of UDataTable that will be used to compose the final UDataTable. */
	TArray<FAssetData> GetParentTables() const;

	/** Returns the skeletal material associated to the given skeletal mesh output pin. */
	FSkeletalMaterial* GetDefaultSkeletalMaterialFor(const UEdGraphPin& MeshPin) const;

	/** Returns the index of the skeletal material associated to the given skeletal mesh output pin. */
	int32 GetDefaultSkeletalMaterialIndexFor(const UEdGraphPin& MeshPin) const;

	/** Returns the SkeletalMesh section associated to the given skeletal mesh output pin. */
	const FSkelMeshSection* GetDefaultSkeletalMeshSectionFor(const UEdGraphPin& MeshPin) const;
private:

	/** Number of properties to know when the node needs an update */
	UPROPERTY()
	int32 NumProperties;
	
	FDelegateHandle OnTableChangedDelegateHandle;
	
	// Generates a mesh pin for each LOD and Material Surface of the reference SkeletalMesh
	void GenerateMeshPins(UObject* Mesh, const FString& ColumnName, const FGuid& ColumnId);

	// Checks if a pin already exists and if it has the same type as before the node refresh
	bool CheckPinUpdated(const FString& PinName, const FName& PinType) const;

};
