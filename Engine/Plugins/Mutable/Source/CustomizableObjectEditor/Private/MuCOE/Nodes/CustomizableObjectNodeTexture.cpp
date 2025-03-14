// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeTexture.h"

#include "MuCOE/Nodes/CustomizableObjectNodePassThroughTexture.h"
#include "MuCO/CustomizableObjectCustomVersion.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "ISinglePropertyView.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Modules/ModuleManager.h"

class UCustomizableObjectNodeRemapPins;
struct FGeometry;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


TSharedPtr<SGraphNode> UCustomizableObjectNodeTextureBase::CreateVisualWidget()
{
	return SNew(SGraphNodeTexture, this);
}


void UCustomizableObjectNodeTexture::AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins)
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	FString PinName = TEXT("Texture");
	UEdGraphPin* PinImagePin = CustomCreatePin(EGPD_Output, Schema->PC_Image, FName(*PinName));
	PinImagePin->bDefaultValueIsIgnored = true;
}


void UCustomizableObjectNodeTexture::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);
	
	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::FixPinsNamesImageToTexture2)
	{
		if (UEdGraphPin* TexturePin = FindPin(TEXT("Image"))) {
			TexturePin->PinName = TEXT("Texture");
			UCustomizableObjectNode::ReconstructNode();
		}
	}
}


FText UCustomizableObjectNodeTexture::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (Texture)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("TextureName"), FText::FromString(Texture->GetName()));

		return FText::Format(LOCTEXT("Texture_Title", "{TextureName}\nTexture"), Args);
	}
	else
	{
		return LOCTEXT("Texture", "Texture");
	}
}


FLinearColor UCustomizableObjectNodeTexture::GetNodeTitleColor() const
{
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();
	return Schema->GetPinTypeColor(Schema->PC_Image);
}


TObjectPtr<UTexture> UCustomizableObjectNodeTexture::GetTexture()
{
	return Texture;
}


FText UCustomizableObjectNodeTexture::GetTooltipText() const
{
	return LOCTEXT("Texture_Tooltip", "Defines a texture.");
}


void SGraphNodeTexture::Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode)
{
	NodeTexture = Cast<UCustomizableObjectNodeTextureBase>(InGraphNode);

	FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FSinglePropertyParams SingleDetails;
	SingleDetails.NamePlacement = EPropertyNamePlacement::Hidden;
	SingleDetails.bHideAssetThumbnail = true;

	if (Cast<UCustomizableObjectNodeTexture>(InGraphNode))
	{
		TextureSelector = PropPlugin.CreateSingleProperty(NodeTexture, "Texture", SingleDetails);
	}
	else if (Cast<UCustomizableObjectNodePassThroughTexture>(InGraphNode))
	{
		TextureSelector = PropPlugin.CreateSingleProperty(NodeTexture, "PassThroughTexture", SingleDetails);
	}
	else
	{
		// Node type not supported.
		ensure(false);
	}

	TextureBrush.SetResourceObject(NodeTexture->GetTexture());
	TextureBrush.ImageSize.X = 128.0f;
	TextureBrush.ImageSize.Y = 128.0f;
	TextureBrush.DrawAs = ESlateBrushDrawType::Image;

	SCustomizableObjectNode::Construct({}, InGraphNode);
}


void SGraphNodeTexture::UpdateGraphNode()
{
	SGraphNode::UpdateGraphNode();
}


void SGraphNodeTexture::SetDefaultTitleAreaWidget(TSharedRef<SOverlay> DefaultTitleAreaWidget)
{
	DefaultTitleAreaWidget->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(FMargin(5))
		[
			SNew(SCheckBox)
			.OnCheckStateChanged(this, &SGraphNodeTexture::OnExpressionPreviewChanged)
			.IsChecked(IsExpressionPreviewChecked())
			.Cursor(EMouseCursor::Default)
			.Style(FAppStyle::Get(), "Graph.Node.AdvancedView")
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(GetExpressionPreviewArrow())
				]
			]
		];
}


void SGraphNodeTexture::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
	LeftNodeBox->AddSlot()
	.AutoHeight()
	[
		SNew(SVerticalBox)
		.Visibility(ExpressionPreviewVisibility())
		
		+ SVerticalBox::Slot()
		.Padding(5.0f,5.0f,0.0f,2.5f)
		.AutoHeight()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(SImage)
			.Image(&TextureBrush)
		]
	];

	MainBox->AddSlot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		.Visibility(ExpressionPreviewVisibility())

		+ SHorizontalBox::Slot()
		.Padding(1.0f, 5.0f, 5.0f, 5.0f)
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		[
			TextureSelector.ToSharedRef()
		]
	];
}


void SGraphNodeTexture::OnExpressionPreviewChanged(const ECheckBoxState NewCheckedState)
{
	NodeTexture->bCollapsed = (NewCheckedState != ECheckBoxState::Checked);
	UpdateGraphNode();
}


ECheckBoxState SGraphNodeTexture::IsExpressionPreviewChecked() const
{
	return NodeTexture->bCollapsed ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
}


const FSlateBrush* SGraphNodeTexture::GetExpressionPreviewArrow() const
{
	return FCustomizableObjectEditorStyle::Get().GetBrush(NodeTexture->bCollapsed ? TEXT("Nodes.ArrowDown") : TEXT("Nodes.ArrowUp"));
}


EVisibility SGraphNodeTexture::ExpressionPreviewVisibility() const
{
	return NodeTexture->bCollapsed ? EVisibility::Collapsed : EVisibility::Visible;
}


void SGraphNodeTexture::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (NodeTexture)
	{
		if (NodeTexture->GetTexture() != TextureBrush.GetResourceObject())
		{
			TextureBrush.SetResourceObject(NodeTexture->GetTexture());
		}
	}
}


#undef LOCTEXT_NAMESPACE
