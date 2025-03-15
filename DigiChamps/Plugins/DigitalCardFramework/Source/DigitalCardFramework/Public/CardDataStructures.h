// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CardDataStructures.generated.h"

UENUM(BlueprintType)
enum class ECardTraits : uint8
{
	NONE = 0,
	AIM UMETA(DisplayName = "A.I.M"),
	ACCUSERCORPS UMETA(DisplayName = "Accuser Corps"),
	ACOLYTE UMETA(DisplayName = "Acolyte"),
	ACTIVE UMETA(DisplayName = "Active"),
	ADAPTOID UMETA(DisplayName = "Adaptoid"),
	AERIAL UMETA(DisplayName = "Aerial"),
	ANDROID UMETA(DisplayName = "Android"),
	ANTS UMETA(DisplayName = "Ants"),
	ARMOR UMETA(DisplayName = "Armor"),
	ARROW UMETA(DisplayName = "Arrow"),
	ARTIFACT UMETA(DisplayName = "Artifact"),
	ASGARD UMETA(DisplayName = "Asgard"),
	ASSASSIN UMETA(DisplayName = "Assassin"),
	ATTACK UMETA(DisplayName = "Attack"),
	ATTORNEY UMETA(DisplayName = "Attorney"),
	AVENGER UMETA(DisplayName = "Avenger"),
	BADOON UMETA(DisplayName = "Badoon"),
	BATROCSBRIGADE UMETA(DisplayName = "Batroc's Brigade"),
	BIOMORPH UMETA(DisplayName = "Biomorph"),
	BIRD UMETA(DisplayName = "BIRD"),
	BLACKORDER UMETA(DisplayName = "Black Order"),
	BLACKPANTHER UMETA(DisplayName = "Black Panther"),
	BOARDMEMBER UMETA(DisplayName = "Board Member"),
	BOOINGCROWD UMETA(DisplayName = "Booing Crowd"),
	BOUNTY UMETA(DisplayName = "Bounty"),
	BOUNTYHUNTER UMETA(DisplayName = "Bounty Hunter"),
	BROTHERHOODOFMUTANTS UMETA(DisplayName = "Brotherhood of Mutants"),
	BRUTE UMETA(DisplayName = "Brute"),
	BUSINESSMAN UMETA(DisplayName = "Businessman"),
	CAPTIVE UMETA(DisplayName = "Captive"),
	CARTOON UMETA(DisplayName = "Cartoon"),
	CELESTIAL UMETA(DisplayName = "Celestial"),
	CHAMPION UMETA(DisplayName = "Champion"),
	CHEERINGCROWD UMETA(DisplayName = "Cheering Crowd"),
	CHITAURI UMETA(DisplayName = "Chitauri"),
	CIVILIAN UMETA(DisplayName = "Civilian"),
	CLANAKKABA UMETA(DisplayName = "Clan Akkaba"),
	CLONE UMETA(DisplayName = "Clone"),
	CONDITION UMETA(DisplayName = "Condition"),
	CONFIDENT UMETA(DisplayName = "Confident"),
	CORNERED UMETA(DisplayName = "Cornered"),
	COSMICENTITY UMETA(DisplayName = "Cosmic Entity"),
	CREATURE UMETA(DisplayName = "CREATURE"),
	CRIMINAL UMETA(DisplayName = "Criminal"),
	CROSSFIRESCREW UMETA(DisplayName = "Crossfire's Crew"),
	CYBERPATH UMETA(DisplayName = "Cyberpath"),
	CYBORG UMETA(DisplayName = "Cyborg"),
	DAMAGED UMETA(DisplayName = "Damaged"),
	DARKRIDERS UMETA(DisplayName = "Dark Riders"),
	DEADPOOLCORPS UMETA(DisplayName = "Deadpool Corps"),
	DEFENDER UMETA(DisplayName = "Defender"),
	DEFENSE UMETA(DisplayName = "Defense"),
	DRONE UMETA(DisplayName = "Drone"),
	ELITE UMETA(DisplayName = "Elite"),
	GAMMA UMETA(DisplayName = "Gamma"),
	HEROFORHIRE UMETA(DisplayName = "Hero for Hire"),
	HYDRA UMETA(DisplayName = "Hydra"),
	ITEM UMETA(DisplayName = "Item"),
	KREE UMETA(DisplayName = "Kree"),
	LOCATION UMETA(DisplayName = "Location"),
	MASTERSOFEVIL UMETA(DisplayName = "Masters of Evil"),
	MERCENARY UMETA(DisplayName = "Mercenary"),
	PERSONA UMETA(DisplayName = "Persona"),
	SHIELD UMETA(DisplayName = "S.H.I.E.L.D."),
	SKILL UMETA(DisplayName = "Skill"),
	SOLDIER UMETA(DisplayName = "Soldier"),
	SPY UMETA(DisplayName = "Spy"),
	SUPERPOWER UMETA(DisplayName = "Superpower"),
	TACTIC UMETA(DisplayName = "Tactic"),
	TECH UMETA(DisplayName = "Tech"),
	THWART UMETA(DisplayName = "Thwart"),
	WAKANDA UMETA(DisplayName = "Wakanda"),
	WEAPON UMETA(DisplayName = "Weapon"),

	//TODO Add to this if we're ever missing a trait for a card

};

UENUM(BlueprintType)
enum class ECardAspect : uint8
{
	BASIC = 0,
	AGRESSION,
	LEADERSHIP,
	PROTECTION,
	JUSTICE,
	POOL,
	HEROASPECT,
	ENCOUNTER,
	CAMPAIGN
};

UENUM(BlueprintType)
enum class ECardResourceType : uint8
{
	WILD = 0,
	MENTAL,
	PHYSICAL,
	ENERGY
};

UENUM(BlueprintType)
enum class ECardType : uint8
{
	HERO = 0,
	ALLY,
	SUPPORT,
	UPGRADE,
	EVENT,
	RESOURCE,
	VILLAIN,
	TREACHERY,
	MINION,
	ATTACHMENT,
	MAINSCHEME,
	SIDESCHEME,
	ENVIRONMENT,
	OBLIGATION,
	PLAYERSIDESCHEME
};


USTRUCT(BlueprintType)
struct DIGITALCARDFRAMEWORK_API FCardDataTableRow :public FTableRowBase
{
	GENERATED_USTRUCT_BODY()

	public:
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (Categories = "Card.Set"))
		FGameplayTag CardSetTag;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		FName CardName = "";
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		UTexture* FrontCardTexture = nullptr;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		UMaterial* FrontCardMaerial = nullptr;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		UTexture* BackCardTexture = nullptr;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		UMaterial* BackCardMaterial = nullptr;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		ECardAspect Aspect;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		ECardType Type;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General", meta = (EditCondition = "!bIsHeroCard"))
		TArray<ECardTraits> Traits;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		bool bIsRestricted = false;
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (EditCondition = "bIsRestricted"))
		bool bIsRestrictedPerDeck = false;
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (EditCondition = "bIsRestrictedPerDeck"))
		int32 NumPerDeck = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (EditCondition = "bIsRestricted"))
		bool bIsRestrictedPerPlayer = false;
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (EditCondition = "bIsRestrictedPerPlayer"))
		int32 NumPerPlayer = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (EditCondition = "bIsRestricted"))
		bool bIsRestrictedPerField = false;
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (EditCondition = "bIsRestrictedPerField"))
		int32 NumPerField = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "General", meta = (EditCondition = "bIsRestricted"))
		bool bIsUniqueCard = false;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General", meta = (EditCondition = "bIsUniqueCard"))
		FName UniqueName = "";
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General")
		bool bUsesCounters = false;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General", meta = (EditCondition = "bUsesCounters"))
		int32 NumOfCounters = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
		int32 Health = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
		int32 Attack = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character", meta = (DisplayName = "Thwart/Scheme"))
		int32 Thwart = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hero")
		int32 Defense = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hero")
		int32 Recover = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hero")
		int32 HandSize = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hero")
		bool bIsMainHeroCard = false;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hero", meta = (EditCondition = "bIsMainHeroCard"))
		TArray<ECardTraits> HeroTraits;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hero", meta = (EditCondition = "bIsMainHeroCard", DisplayName = "Alter-ego Traits"))
		TArray<ECardTraits> AlteregoTraits;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally")
		int32 AttackConsequentialDamage = 0;
		UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ally")
		int32 ThwartConsequentialDamage = 0;
};
