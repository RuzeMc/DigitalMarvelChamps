// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/Array.h"
#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Misc/AssertionMacros.h"
#include "RHI.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"

#include <atomic>

class FRHICommandListImmediate;

//-------------------------------------------------------------------------------------------------
// FXRSwapChain
//-------------------------------------------------------------------------------------------------

class XRBASE_API FXRSwapChain : public TSharedFromThis<FXRSwapChain, ESPMode::ThreadSafe>
{
public:
	FXRSwapChain(TArray<FTextureRHIRef>&& InRHITextureSwapChain, const FTextureRHIRef& AliasedTexture);
	virtual ~FXRSwapChain() {}

	const FTextureRHIRef& GetTextureRef() const { return RHITexture; }
	FRHITexture* GetTexture() const { return RHITexture.GetReference(); }
	FRHITexture* GetTexture2D() const { return RHITexture->GetTexture2D(); }
	FRHITexture* GetTexture2DArray() const { return RHITexture->GetTexture2DArray(); }
	FRHITexture* GetTextureCube() const { return RHITexture->GetTextureCube(); }
	uint32 GetSwapChainLength() const { return (uint32)RHITextureSwapChain.Num(); }
	TArray<FTextureRHIRef> GetSwapChain() const { return RHITextureSwapChain; }

	uint32 GetSwapChainIndex_RHIThread() { return SwapChainIndex_RHIThread; }

	virtual void IncrementSwapChainIndex_RHIThread();

	virtual void WaitCurrentImage_RHIThread(int64 TimeoutNanoseconds = 0) {} // Default to no timeout (immediate).
	virtual void ReleaseCurrentImage_RHIThread(IRHICommandContext* RHICmdContext) {}

	void SetDebugLabel(FStringView NewLabel) { DebugLabel = NewLabel; }

protected:
	virtual void ReleaseResources_RHIThread();

	FTextureRHIRef RHITexture;
	TArray<FTextureRHIRef> RHITextureSwapChain;
	std::atomic_uint32_t SwapChainIndex_RHIThread;

	FString DebugLabel;
};

typedef TSharedPtr<FXRSwapChain, ESPMode::ThreadSafe> FXRSwapChainPtr;

template<typename T = FXRSwapChain, typename... Types>
FXRSwapChainPtr CreateXRSwapChain(TArray<FTextureRHIRef>&& InRHITextureSwapChain, const FTextureRHIRef& InRHIAliasedTexture, Types... InExtraParameters)
{
	check(InRHITextureSwapChain.Num() >= 1);
	return MakeShareable(new T(MoveTemp(InRHITextureSwapChain), InRHIAliasedTexture, InExtraParameters...));
}



