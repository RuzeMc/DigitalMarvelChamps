// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Online/AuthCommon.h"
#include "Online/OnlineIdCommon.h"

namespace UE::Online {

class FOnlineServicesNull;

class UE_DEPRECATED(5.5, "FOnlineAccountIdRegistryNull now uses TOnlineBasicAccountIdRegistry, and FOnlineAccountIdString was never exposed external to FOnlineAccountIdRegistryNull") FOnlineAccountIdString
{
public:
	FString Data;
	int32 AccountIndex;
	FAccountId AccountId;
};

class FOnlineAccountIdRegistryNull
	: public IOnlineAccountIdRegistry
{
public:
	ONLINESERVICESNULL_API static FOnlineAccountIdRegistryNull& Get();

	ONLINESERVICESNULL_API FAccountId Find(const FString& AccountId) const;
	ONLINESERVICESNULL_API FAccountId FindOrAddAccountId(const FString& AccountId);

	// Begin IOnlineAccountIdRegistry
	virtual FString ToString(const FAccountId& AccountId) const override;
	virtual FString ToLogString(const FAccountId& AccountId) const override;
	virtual TArray<uint8> ToReplicationData(const FAccountId& AccountId) const override;
	virtual FAccountId FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineAccountIdRegistry

	virtual ~FOnlineAccountIdRegistryNull() = default;

private:
	FOnlineAccountIdRegistryNull();
	TOnlineBasicAccountIdRegistry<FString> Registry;
};


struct FAccountInfoNull final : public FAccountInfo
{
};

// Auth NULL is implemented in a way similar to console platforms where there is not an explicit
// login / logout from online services. On those platforms the user account is picked either before
// the game has started or as a part of selecting an input device.
class ONLINESERVICESNULL_API FAccountInfoRegistryNULL final : public FAccountInfoRegistry
{
public:
	using Super = FAccountInfoRegistry;

	virtual ~FAccountInfoRegistryNULL() = default;

	TSharedPtr<FAccountInfoNull> Find(FPlatformUserId PlatformUserId) const;
	TSharedPtr<FAccountInfoNull> Find(FAccountId AccountIdHandle) const;

	void Register(const TSharedRef<FAccountInfoNull>&UserAuthData);
	void Unregister(FAccountId AccountId);
};

class ONLINESERVICESNULL_API FAuthNull : public FAuthCommon
{
public:
	using Super = FAuthCommon;

	FAuthNull(FOnlineServicesNull& InOwningSubsystem);
	virtual void Initialize() override;
	virtual void PreShutdown() override;

protected:
	virtual const FAccountInfoRegistry& GetAccountInfoRegistry() const override;

	void InitializeUsers();
	void UninitializeUsers();
	void OnInputDeviceConnectionChange(EInputDeviceConnectionState NewConnectionState, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId);

	FAccountInfoRegistryNULL AccountInfoRegistryNULL;
};

/* UE::Online */ }
