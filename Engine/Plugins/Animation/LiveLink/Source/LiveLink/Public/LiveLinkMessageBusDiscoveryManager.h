﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"

#include "LiveLinkMessageBusFinder.h"

class IMessageContext;

class FLiveLinkMessageBusSource;

/** A class to asynchronously discover message bus sources. */
class LIVELINK_API FLiveLinkMessageBusDiscoveryManager : FRunnable
{
public:
	FLiveLinkMessageBusDiscoveryManager();
	~FLiveLinkMessageBusDiscoveryManager();

	//~ Begin FRunnable interface

	virtual uint32 Run() override;

	virtual void Stop() override;

	//~ End FRunnable interface

	void AddDiscoveryMessageRequest();
	void RemoveDiscoveryMessageRequest();
	TArray<FProviderPollResultPtr> GetDiscoveryResults() const;

	bool IsRunning() const;

private:
	void HandlePongMessage(const FLiveLinkPongMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

private:
	// Counter of item that request discovery message
	TAtomic<int32> PingRequestCounter;

	// Last ping Request id
	FGuid LastPingRequest;

	// Time of the last ping request
	double LastPingRequestTime;

	// Ping request timeout
	FTimespan PingRequestFrequency;

	// Result from the last ping request
	TArray<FProviderPollResultPtr> LastProviderPoolResults;

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	// Thread safe bool for stopping the thread
	FThreadSafeBool bRunning;

	// Thread the heartbeats are sent on
	FRunnableThread* Thread;

	// Event used to poll the discovery results.
	class FEvent* PollEvent = nullptr;

	// Critical section for accessing the Source Set
	mutable FCriticalSection SourcesCriticalSection;
};
