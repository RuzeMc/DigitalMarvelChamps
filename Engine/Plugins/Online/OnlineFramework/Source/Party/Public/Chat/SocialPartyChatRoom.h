// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SocialChatRoom.h"
#include "SocialPartyChatRoom.generated.h"

class UPartyMember;
enum class ESocialChannelType : uint8;

class USocialChatManager;
enum class EMemberExitedReason : uint8;

/** A multi-user chat room channel. Used for all chat situations outside of private user-to-user direct messages. */
UCLASS()
class PARTY_API USocialPartyChatRoom : public USocialChatRoom
{
	GENERATED_BODY()

public:
	virtual void Initialize(USocialUser* InSocialUser, const FChatRoomId& InChannelId, ESocialChannelType InSourceChannelType);

protected:
	virtual void HandlePartyMemberLeft(EMemberExitedReason Reason);
	virtual void HandlePartyMemberJoined(UPartyMember& NewMember);
};
