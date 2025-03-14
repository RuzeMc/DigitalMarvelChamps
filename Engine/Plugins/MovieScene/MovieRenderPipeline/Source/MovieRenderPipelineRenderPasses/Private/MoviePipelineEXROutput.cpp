// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineEXROutput.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "Math/Float16.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineCameraSetting.h"
#include "ImageWriteQueue.h"
#include "MoviePipeline.h"
#include "MoviePipelineImageQuantization.h"
#include "MoviePipelinePrimaryConfig.h"
#include "IOpenExrRTTIModule.h"
#include "Modules/ModuleManager.h"
#include "MoviePipelineUtils.h"
#include "ColorManagement/ColorSpace.h"
#include "HDRHelper.h"

THIRD_PARTY_INCLUDES_START
#include "OpenEXR/ImfChannelList.h"
#include "OpenEXR/ImfStandardAttributes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MoviePipelineEXROutput)

THIRD_PARTY_INCLUDES_END

#if WITH_UNREALEXR

// NOTE: see also ExrImageWrapper

class FExrMemStreamOut : public Imf::OStream
{
public:

	FExrMemStreamOut()
		: Imf::OStream("")
		, Pos(0)
	{
	}

	// InN must be 32bit to match the abstract interface.
	virtual void write(const char c[/*n*/], int32 InN)
	{
		int64 SrcN = (int64)InN;
		int64 DestPost = Pos + SrcN;
		if (DestPost > Data.Num())
		{
			Data.AddUninitialized(DestPost - Data.Num());
		}

		for (int64 i = 0; i < SrcN; ++i)
		{
			Data[Pos + i] = c[i];
		}
		Pos += SrcN;
	}


	//---------------------------------------------------------
	// Get the current writing position, in bytes from the
	// beginning of the file.  If the next call to write() will
	// start writing at the beginning of the file, tellp()
	// returns 0.
	//---------------------------------------------------------

	uint64_t tellp() override
	{
		return Pos;
	}


	//-------------------------------------------
	// Set the current writing position.
	// After calling seekp(i), tellp() returns i.
	//-------------------------------------------

	void seekp(uint64_t pos) override
	{
		Pos = pos;
	}


	int64 Pos;
	TArray64<uint8> Data;
};

bool FEXRImageWriteTask::RunTask()
{
	bool bSuccess = WriteToDisk();

	if (OnCompleted)
	{
		AsyncTask(ENamedThreads::GameThread, [bSuccess, LocalOnCompleted = MoveTemp(OnCompleted)] { LocalOnCompleted(bSuccess); });
	}

	return bSuccess;
}

void FEXRImageWriteTask::OnAbandoned()
{
	if (OnCompleted)
	{
		AsyncTask(ENamedThreads::GameThread, [LocalOnCompleted = MoveTemp(OnCompleted)] { LocalOnCompleted(false); });
	}
}

bool FEXRImageWriteTask::WriteToDisk()
{
	// Ensure that the payload filename has the correct extension for the format
	const TCHAR* FormatExtension = TEXT(".exr");
	if (FormatExtension && !Filename.EndsWith(FormatExtension))
	{
		Filename = FPaths::GetBaseFilename(Filename, false) + FormatExtension;
	}

	bool bSuccess = EnsureWritableFile();

	if (bSuccess)
	{
		PreProcess();

		static_assert(static_cast<uint8>(EEXRCompressionFormat::Max) == Imf::Compression::NUM_COMPRESSION_METHODS);
		Imf::Compression FileCompression = static_cast<Imf::Compression>(Compression);
		
		// Data Window specifies how much data is in the actual file, ie: 1920x1080
		IMATH_NAMESPACE::Box2i DataWindow = IMATH_NAMESPACE::Box2i(IMATH_NAMESPACE::V2i(0,0), IMATH_NAMESPACE::V2i(Width - 1, Height - 1));
		
		float BorderRatio = FMath::Clamp((OverscanPercentage / (1.0f + OverscanPercentage)) / 2.0f, 0.0f, 0.5f);
		int32 OverscanBorderWidth = FMath::RoundToInt(Width * BorderRatio);
		int32 OverscanBorderHeight = FMath::RoundToInt(Height * BorderRatio);

		// Taking Overscan into account.
		IMATH_NAMESPACE::V2i TopLeft = IMATH_NAMESPACE::V2i(OverscanBorderWidth, OverscanBorderHeight);
		IMATH_NAMESPACE::V2i BottomRight = IMATH_NAMESPACE::V2i(Width - OverscanBorderWidth - 1, Height - OverscanBorderHeight - 1);

		// Display Window specifies the total 'visible' area of the output file. 
		// The Display Window always starts at 0,0, but Data Window can go negative to
		// support having pixels out of bounds (such as camera overscan).
		IMATH_NAMESPACE::Box2i DisplayWindow = IMATH_NAMESPACE::Box2i(TopLeft, BottomRight);
		
		Imf::Header Header(DisplayWindow, DataWindow, 1, Imath::V2f(0, 0), 1, Imf::LineOrder::INCREASING_Y, FileCompression);
		
		// If using lossy compression, specify the compression level in the header per exr spec.
		if (FileCompression == Imf::Compression::DWAA_COMPRESSION ||
			FileCompression == Imf::Compression::DWAB_COMPRESSION)
		{
			FileMetadata.Add("dwaCompressionLevel", CompressionLevel);
		}

		// Insert our key-value pair metadata (if any, can be an arbitrary set of key/value pairs)
		AddFileMetadata(Header);

		if (ColorSpaceChromaticities.Num() > 0)
		{
			if (ensureMsgf(ColorSpaceChromaticities.Num() == 4, TEXT("Four chromaticity coordinates expected.")))
			{
				Imf::Chromaticities Chromaticities = {
					IMATH_NAMESPACE::V2f((float)ColorSpaceChromaticities[0].X, (float)ColorSpaceChromaticities[0].Y),
					IMATH_NAMESPACE::V2f((float)ColorSpaceChromaticities[1].X, (float)ColorSpaceChromaticities[1].Y),
					IMATH_NAMESPACE::V2f((float)ColorSpaceChromaticities[2].X, (float)ColorSpaceChromaticities[2].Y),
					IMATH_NAMESPACE::V2f((float)ColorSpaceChromaticities[3].X, (float)ColorSpaceChromaticities[3].Y),
				};
				Imf::addChromaticities(Header, Chromaticities);
			}
		}

		FExrMemStreamOut OutputFile; 
		
		{
			// The FrameBuffer stores all the channels of the resulting image.
			Imf::FrameBuffer FrameBuffer;

			// If we have to quantize the data (ie: Upscale 8 bit to 16 bit) we need to store them long enough for the file to get written.
			TArray<TUniquePtr<FImagePixelData>> QuantizedData;

			for (TUniquePtr<FImagePixelData>& Layer : Layers)
			{
				uint8 RawBitDepth = Layer->GetBitDepth();
				check(RawBitDepth == 8 || RawBitDepth == 16 || RawBitDepth == 32);

				if (!ensureAlwaysMsgf(Layer->GetSize().X == Width && Layer->GetSize().Y == Height, TEXT("Skipping layer due to mismatched width/height from rest of EXR file!")))
				{
					continue;
				}

				void const* RawDataPtr = nullptr;
				int64 RawDataSize;
				bSuccess = Layer->GetRawData(RawDataPtr, RawDataSize);
				if (!bSuccess)
				{
					UE_LOG(LogMovieRenderPipelineIO, Error, TEXT("Failed to retrieve raw data from image data for writing. Bailing."));
					break;
				}

				int64 BytesWritten = 0;
				switch (RawBitDepth)
				{
				case 8:
				{
					TUniquePtr<FImagePixelData> QuantizedPixelData = UE::MoviePipeline::QuantizeImagePixelDataToBitDepth(Layer.Get(), 16);
					QuantizedData.Add(MoveTemp(QuantizedPixelData));

					// Add an entry in the LayerNames table if needed since it matches by Layer pointer but that has changed.
					FString LayerName = LayerNames.FindOrAdd(Layer.Get());
					if (LayerName.Len() > 0)
					{
						LayerNames.Add(QuantizedData.Last().Get(), LayerName);
					}

					BytesWritten = CompressRaw<Imf::HALF>(Header, FrameBuffer, QuantizedData.Last().Get());
				}
					break;
				case 16:
					BytesWritten = CompressRaw<Imf::HALF>(Header, FrameBuffer, Layer.Get());
					break;
				case 32:
					BytesWritten = CompressRaw<Imf::FLOAT>(Header, FrameBuffer, Layer.Get());
					break;
				default:
					checkNoEntry();
				}
				
				// Reserve enough space in the output file for the whole layer so we don't keep reallocating.
				OutputFile.Data.Reserve(BytesWritten);
			}

			// This scope ensures that IMF::Outputfile creates a complete file by closing the file when it goes out of scope.
			// To complete the file, EXR seeks back into the file and writes the scanline offsets when the file is closed, 
			// which moves the tellp location. So file length is stored in advance for later use. The output file needs to be
			// created after the header information is filled.
			Imf::OutputFile ImfFile(OutputFile, Header, FPlatformMisc::NumberOfCoresIncludingHyperthreads());
#if WITH_EDITOR
			try
#endif
			{
				ImfFile.setFrameBuffer(FrameBuffer);
				ImfFile.writePixels(Height);
			}
#if WITH_EDITOR
			catch (const IEX_NAMESPACE::BaseExc& Exception)
			{
				UE_LOG(LogMovieRenderPipelineIO, Error, TEXT("Caught exception: %hs"), Exception.message().c_str());
			}
#endif
		}
		
		// Now that the scope has closed for the Imf::OutputFile, now we can write the data to disk.
		if (bSuccess)
		{
			bSuccess = FFileHelper::SaveArrayToFile(OutputFile.Data, *Filename);
		} 
	}

	if (!bSuccess)
	{
		UE_LOG(LogMovieRenderPipelineIO, Error, TEXT("Failed to write image to '%s'. The pixel format may not be compatible with this image type, or there was an error writing to that filename."), *Filename);
	}

	return bSuccess;
}

static FString GetChannelName(const FString& InLayerName, const int32 InChannelIndex, const ERGBFormat InFormat)
{
	const int32 MaxChannels = 4;
	static const char* RGBAChannelNames[] = { "R", "G", "B", "A" };
	static const char* BGRAChannelNames[] = { "B", "G", "R", "A" };
	static const char* GrayChannelNames[] = { "G" };
	check(InChannelIndex < MaxChannels);

	const char** ChannelNames = BGRAChannelNames;

	switch (InFormat)
	{
		case ERGBFormat::RGBA:
		case ERGBFormat::RGBAF:
		{
			ChannelNames = RGBAChannelNames;
		}
		break;
		case ERGBFormat::BGRA:
		{
			ChannelNames = BGRAChannelNames;
		}
		break;
		case ERGBFormat::Gray:
		case ERGBFormat::GrayF:
		{
			check(InChannelIndex < UE_ARRAY_COUNT(GrayChannelNames));
			ChannelNames = GrayChannelNames;
		}
		break;
		default:
			checkNoEntry();
	}

	if (InLayerName.Len() > 0)
	{
		return FString::Printf(TEXT("%s.%hs"), *InLayerName, ChannelNames[InChannelIndex]);
	}
	else
	{
		return FString(ChannelNames[InChannelIndex]);
	}
}

static int32 GetComponentWidth(const EImagePixelType InPixelType)
{
	switch (InPixelType)
	{
	case EImagePixelType::Color: return 1;
	case EImagePixelType::Float16: return 2;
	case EImagePixelType::Float32: return 4;
	default:
		checkNoEntry();
	}
	return 1;
}

template <Imf::PixelType OutputFormat>
int64 FEXRImageWriteTask::CompressRaw(Imf::Header& InHeader, Imf::FrameBuffer& InFrameBuffer, FImagePixelData* InLayer)
{
	void const* RawDataPtr = nullptr;
	int64 RawDataSize;

	if (InLayer->GetRawData(RawDataPtr, RawDataSize) == false)
	{
		UE_LOG(LogMovieRenderPipelineIO, Error, TEXT("Failed to retrieve raw data from image data for writing. Bailing."));
		return 0;
	}
		

	// Look up our layer name (if any).
	FString& LayerName = LayerNames.FindOrAdd(InLayer);
	int32 NumChannels = InLayer->GetNumChannels();
	int32 ComponentWidth = GetComponentWidth(InLayer->GetType());

	for (int32 Channel = 0; Channel < NumChannels; Channel++)
	{
		FString ChannelName = GetChannelName(LayerName, Channel, InLayer->GetPixelLayout());

		// Insert the channel into the header with the right datatype.
		InHeader.channels().insert(TCHAR_TO_ANSI(*ChannelName), Imf::Channel(OutputFormat));

		// Now insert the data for this channel. Unreal stores them interleaved.
		InFrameBuffer.insert(TCHAR_TO_ANSI(*ChannelName),	// Name
			Imf::Slice(OutputFormat,						// Type
			(char*)RawDataPtr + (ComponentWidth * Channel), // Data Start (offset by component to match interleave)
				ComponentWidth * NumChannels,				// xStride
				Width * ComponentWidth * NumChannels));		// yStride
	}
	
	return int64(Width) * int64(Height) * NumChannels * int64(OutputFormat == 2 ? 4 : 2);
}

bool FEXRImageWriteTask::EnsureWritableFile()
{
	FString Directory = FPaths::GetPath(Filename);

	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		IFileManager::Get().MakeDirectory(*Directory);
	}

	// If the file doesn't exist, we're ok to continue
	if (IFileManager::Get().FileSize(*Filename) == -1)
	{
		return true;
	}
	// If we're allowed to overwrite the file, and we deleted it ok, we can continue
	else if (bOverwriteFile && FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Filename))
	{
		return true;
	}
	// We can't write to the file
	else
	{
		UE_LOG(LogMovieRenderPipelineIO, Error, TEXT("Failed to write image to '%s'. Should Overwrite: %d - If we should have overwritten the file, we failed to delete the file. If we shouldn't have overwritten the file the file already exists so we can't replace it."), *Filename, bOverwriteFile);
		return false;
	}
}

void FEXRImageWriteTask::AddFileMetadata(Imf::Header& InHeader)
{
	static const FName RTTIExtensionModuleName("UEOpenExrRTTI");
	IOpenExrRTTIModule* OpenExrModule = FModuleManager::LoadModulePtr<IOpenExrRTTIModule>(RTTIExtensionModuleName);
	if (OpenExrModule)
	{
		OpenExrModule->AddFileMetadata(FileMetadata, InHeader);
	}
}

void FEXRImageWriteTask::PreProcess()
{
	if (PixelPreprocessors.IsEmpty())
	{
		return;
	}

	for (int32 LayerIdx = 0; LayerIdx < Layers.Num(); ++LayerIdx)
	{
		if (const TArray<FPixelPreProcessor>* LayerPixelPreprocessorsPtr = PixelPreprocessors.Find(LayerIdx))
		{
			for (const FPixelPreProcessor& PreProcessor : *LayerPixelPreprocessorsPtr)
			{
				// PreProcessors are assumed to be valid. Fetch the Data pointer each time
				// in case a pre-processor changes our pixel data.
				FImagePixelData* Data = Layers[LayerIdx].Get();
				PreProcessor(Data);
			}
		}
	}
}

#endif // WITH_UNREALEXR

namespace UE
{
	namespace MoviePipeline
	{
		static TPair<UE::Color::EColorSpace, FString> GetDisplayGamutType(EDisplayColorGamut InDisplayGamut)
		{
			switch (InDisplayGamut)
			{
			case EDisplayColorGamut::sRGB_D65: return { UE::Color::EColorSpace::sRGB, TEXT("sRGB") };
			case EDisplayColorGamut::DCIP3_D65: return { UE::Color::EColorSpace::P3DCI, TEXT("P3DCI") };
			case EDisplayColorGamut::Rec2020_D65: return { UE::Color::EColorSpace::Rec2020, TEXT("Rec2020") };
			case EDisplayColorGamut::ACES_D60: return { UE::Color::EColorSpace::ACESAP0, TEXT("ACESAP0") };
			case EDisplayColorGamut::ACEScg_D60: return { UE::Color::EColorSpace::ACESAP1, TEXT("ACESAP1") };

			default:
				checkNoEntry();
				return {};
			}
		};

		void UpdateColorSpaceMetadataImpl(const FEXRColorSpaceMetadata& InColorSpaceMetadata, FEXRImageWriteTask& InOutImageTask)
		{
			if (!InColorSpaceMetadata.SourceName.IsEmpty())
			{
				InOutImageTask.FileMetadata.Add("unreal/colorSpace/source", InColorSpaceMetadata.SourceName);
			}
			if (!InColorSpaceMetadata.DestinationName.IsEmpty())
			{
				InOutImageTask.FileMetadata.Add("unreal/colorSpace/destination", InColorSpaceMetadata.DestinationName);
			}

			InOutImageTask.ColorSpaceChromaticities = InColorSpaceMetadata.Chromaticities;
		}

		void UpdateColorSpaceMetadata(const FOpenColorIOColorConversionSettings& InConversionSettings, FEXRImageWriteTask& InOutImageTask)
		{
			FEXRColorSpaceMetadata ColorSpaceMetadata;

			if (InConversionSettings.IsValid())
			{
				// Note: OpenColorIO does not expose chromaticity information so we only provide transform names.
				if (InConversionSettings.IsDisplayView())
				{
					switch (InConversionSettings.DisplayViewDirection)
					{
					case EOpenColorIOViewTransformDirection::Forward:
						ColorSpaceMetadata.SourceName = InConversionSettings.SourceColorSpace.ToString();
						ColorSpaceMetadata.DestinationName = InConversionSettings.DestinationDisplayView.ToString();
						break;
					case EOpenColorIOViewTransformDirection::Inverse:
						ColorSpaceMetadata.SourceName = InConversionSettings.DestinationDisplayView.ToString();
						ColorSpaceMetadata.DestinationName = InConversionSettings.SourceColorSpace.ToString();
						break;

					default:
						checkNoEntry();
					}
				}
				else
				{
					ColorSpaceMetadata.SourceName = InConversionSettings.SourceColorSpace.ToString();
					ColorSpaceMetadata.DestinationName = InConversionSettings.DestinationColorSpace.ToString();
				}
			}

			UpdateColorSpaceMetadataImpl(ColorSpaceMetadata, InOutImageTask);
		}

		void UpdateColorSpaceMetadata(ESceneCaptureSource InSceneCaptureSource, FEXRImageWriteTask& InOutImageTask)
		{
			FEXRColorSpaceMetadata ColorSpaceMetadata;

			switch (InSceneCaptureSource)
			{
			case SCS_FinalColorLDR:
			case SCS_FinalToneCurveHDR:
			{
				// We are in output display space
				TPair<UE::Color::EColorSpace, FString> ColorSpaceType = GetDisplayGamutType(HDRGetDefaultDisplayColorGamut());
				UE::Color::FColorSpace OutputCS = UE::Color::FColorSpace(ColorSpaceType.Key);

				ColorSpaceMetadata.DestinationName = ColorSpaceType.Value;
				ColorSpaceMetadata.Chromaticities = { OutputCS.GetRedChromaticity(), OutputCS.GetGreenChromaticity(), OutputCS.GetBlueChromaticity(), OutputCS.GetWhiteChromaticity() };
				break;
			}
			case SCS_SceneColorHDR:
			case SCS_SceneColorHDRNoAlpha:
			case SCS_FinalColorHDR:
			case SCS_BaseColor:
			{
				// We are in working color space
				const UE::Color::FColorSpace& WCS = UE::Color::FColorSpace::GetWorking();
				ColorSpaceMetadata.Chromaticities = { WCS.GetRedChromaticity(), WCS.GetGreenChromaticity(), WCS.GetBlueChromaticity(), WCS.GetWhiteChromaticity() };
				break;
			}
			default:
				break;
			}

			UpdateColorSpaceMetadataImpl(ColorSpaceMetadata, InOutImageTask);
		}
	}
}

void UMoviePipelineImageSequenceOutput_EXR::OnReceiveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame)
{
	if (!bMultilayer)
	{
		// Some software doesn't support multi-layer, so in that case we fall back to the single-layer-multiple-file
		// codepath of our parent.
		Super::OnReceiveImageDataImpl(InMergedOutputFrame);
		return;
	}
	check(InMergedOutputFrame);

	// Ensure our OpenExrRTTI module gets loaded. This needs to happen from the main thread, if it's not loaded then metadata silently fails when writing.
	static const FName RTTIExtensionModuleName("UEOpenExrRTTI");
	FModuleManager::Get().LoadModule(RTTIExtensionModuleName);


	UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelinePrimaryConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	// EXR only supports one resolution per file, but in certain scenarios we can get layers with different resolutions. To solve this, we're
	// going to write one exr file per image resolution. First we loop through all layers to figure out how many sizes we're dealing with.
	TArray<FIntPoint> Resolutions;
	for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
	{
		Resolutions.AddUnique(RenderPassData.Value->GetSize());
	}

	// Then submit multiple write tasks. Layers that don't match the current resolution will be skipped until the correct iteration of the loop.
	for (int32 Index = 0; Index < Resolutions.Num(); Index++)
	{
		FString OutputDirectory = OutputSettings->OutputDirectory.Path;

		// We need to resolve the filename format string. We combine the folder and file name into one long string first
		FString FinalFilePath;
		FMoviePipelineFormatArgs FinalFormatArgs;
		FString FinalImageSequenceFileName;
		FString ClipName;
		const TCHAR* Extension = TEXT("exr");
		{
			// If we have more than one resolution we'll store it as "_Add" / "_Add(1)" etc.
			FString FileNameFormatString = OutputSettings->FileNameFormat + "{ExtraTag}";

			// If we're writing more than one render pass out, we need to ensure the file name has the format string in it so we don't
			// overwrite the same file multiple times. Burn In overlays don't count because they get composited on top of an existing file.
			const bool bIncludeRenderPass = false;
			const bool bTestFrameNumber = true;

			UE::MoviePipeline::ValidateOutputFormatString(FileNameFormatString, bIncludeRenderPass, bTestFrameNumber);

			// Create specific data that needs to override 
			TMap<FString, FString> FormatOverrides;
			FormatOverrides.Add(TEXT("render_pass"), TEXT("")); // Render Passes are included inside the exr file by named layers.
			FormatOverrides.Add(TEXT("ext"), Extension);

			// The logic for the ExtraTag is a little complicated. If there's only one layer (ideal situation) then it's empty.
			if (Index == 0)
			{
				FormatOverrides.Add(TEXT("ExtraTag"), TEXT(""));
			}
			else if(Resolutions.Num() == 2)
			{
				// This is our most common case when we have a second file (the only expected one really)
				FormatOverrides.Add(TEXT("ExtraTag"), TEXT("_Add"));
			}
			else
			{
				// Finally a fallback in the event we have three or more unique resolutions.
				FormatOverrides.Add(TEXT("ExtraTag"), FString::Printf(TEXT("_Add(%d)"), Index));
			}

			// This resolves the filename format and gathers metadata from the settings at the same time.
			GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, FormatOverrides, FinalImageSequenceFileName, FinalFormatArgs, &InMergedOutputFrame->FrameOutputState, -InMergedOutputFrame->FrameOutputState.ShotOutputFrameNumber);

			FString FilePathFormatString = OutputDirectory / FileNameFormatString;
			GetPipeline()->ResolveFilenameFormatArguments(FilePathFormatString, FormatOverrides, FinalFilePath, FinalFormatArgs, &InMergedOutputFrame->FrameOutputState);

			if (FPaths::IsRelative(FinalFilePath))
			{
				FinalFilePath = FPaths::ConvertRelativePathToFull(FinalFilePath);
			}

			// Create a deterministic clipname by removing frame numbers, file extension, and any trailing .'s
			UE::MoviePipeline::RemoveFrameNumberFormatStrings(FileNameFormatString, true);
			GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, FormatOverrides, ClipName, FinalFormatArgs, &InMergedOutputFrame->FrameOutputState);
			ClipName.RemoveFromEnd(Extension);
			ClipName.RemoveFromEnd(".");
		}

		TUniquePtr<FEXRImageWriteTask> MultiLayerImageTask = MakeUnique<FEXRImageWriteTask>();
		MultiLayerImageTask->Filename = FinalFilePath;
		MultiLayerImageTask->Compression = Compression;
		// MultiLayerImageTask->CompressionLevel is intentionally skipped because it doesn't seem to make any practical difference
		// so we don't expose it to the user because that will just cause confusion where the setting doesn't seem to do anything.

		// FinalFormatArgs.FileMetadata has been merged by ResolveFilenameFormatArgs with the FrameOutputState,
		// but we need to convert from FString, FString (needed for BP/Python purposes) to a FStringFormatArg as
		// we need to preserve numeric metadata types later in the image writing process (for compression level)
		TMap<FString, FStringFormatArg> NewFileMetdataMap;
		for (const TPair<FString, FString>& Metadata : FinalFormatArgs.FileMetadata)
		{
			NewFileMetdataMap.Add(Metadata.Key, Metadata.Value);
		}
		MultiLayerImageTask->FileMetadata = NewFileMetdataMap;

		// Add color space metadata to the output: xy chromaticity coordinates and/or the color space source/dest names.
		// TODO: Support is also needed for regular exrs via the image wrapper module.
		UMoviePipelineColorSetting* ColorSetting = GetPipeline()->GetPipelinePrimaryConfig()->FindSetting<UMoviePipelineColorSetting>();
		if (ColorSetting && ColorSetting->OCIOConfiguration.bIsEnabled)
		{
			UE::MoviePipeline::UpdateColorSpaceMetadata(ColorSetting->OCIOConfiguration.ColorConfiguration, *MultiLayerImageTask);
		}
		else
		{
			ESceneCaptureSource SceneCaptureSource = (ColorSetting && ColorSetting->bDisableToneCurve) ? ESceneCaptureSource::SCS_FinalColorHDR : ESceneCaptureSource::SCS_FinalToneCurveHDR;
			UE::MoviePipeline::UpdateColorSpaceMetadata(SceneCaptureSource, *MultiLayerImageTask);
		}

		int32 LayerIndex = 0;
		bool bRequiresTransparentOutput = false;
		int32 ShotIndex = 0;
		for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
		{
			if (RenderPassData.Value->GetSize() != Resolutions[Index])
			{
				// If this layer isn't for this resolution, don't add it to the multilayer task, a second task will be created soon.
				continue;
			}

			// No quantization required, just copy the data as we will move it into the image write task.
			TUniquePtr<FImagePixelData> PixelData = RenderPassData.Value->CopyImageData();
			FImagePixelDataPayload* Payload = RenderPassData.Value->GetPayload<FImagePixelDataPayload>();
			ShotIndex = Payload->SampleState.OutputState.ShotIndex;

			// If there is more than one layer, then we will prefix the layer. The first layer is not prefixed (and gets inserted as RGBA)
			// as most programs that handle EXRs expect the main image data to be in an unnamed layer.
			if (LayerIndex == 0)
			{
				// Only check the main image pass for transparent output since that's generally considered the 'preview'.
				bRequiresTransparentOutput = Payload->bRequireTransparentOutput;
				MultiLayerImageTask->OverscanPercentage = Payload->SampleState.OverscanPercentage;
			}
			else
			{
				// If there is more than one layer, then we will prefix the layer. The first layer is not prefixed (and gets inserted as RGBA)
				// as most programs that handle EXRs expect the main image data to be in an unnamed layer. We only postfix with cameraname
				// if there's multiple cameras, as pipelines may be already be built around the generic "one camera" support.
				UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[ShotIndex];
				UMoviePipelineCameraSetting* CameraSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineCameraSetting>(CurrentShot);
				int32 NumCameras = CameraSettings->bRenderAllCameras ? CurrentShot->SidecarCameras.Num() : 1;
				
				FString CombinedName;
				if (NumCameras == 1)
				{
					CombinedName = RenderPassData.Key.Name;
				}
				else
				{
					CombinedName = FString::Printf(TEXT("%s_%s"), *RenderPassData.Key.Name, *RenderPassData.Key.CameraName);
				}
				MultiLayerImageTask->LayerNames.FindOrAdd(PixelData.Get(), CombinedName);
			}

			MultiLayerImageTask->Width = Resolutions[Index].X;
			MultiLayerImageTask->Height = Resolutions[Index].Y;
			MultiLayerImageTask->Layers.Add(MoveTemp(PixelData));
			LayerIndex++;
		}

		MoviePipeline::FMoviePipelineOutputFutureData OutputData;
		OutputData.Shot = GetPipeline()->GetActiveShotList()[ShotIndex];
		OutputData.PassIdentifier = FMoviePipelinePassIdentifier(TEXT("")); // exrs put all the render passes internally so this resolves to a ""
		OutputData.FilePath = FinalFilePath;
		GetPipeline()->AddOutputFuture(ImageWriteQueue->Enqueue(MoveTemp(MultiLayerImageTask)), OutputData);

#if WITH_EDITOR
		GetPipeline()->AddFrameToOutputMetadata(ClipName, FinalImageSequenceFileName, InMergedOutputFrame->FrameOutputState, Extension, bRequiresTransparentOutput);
#endif

	}
}
