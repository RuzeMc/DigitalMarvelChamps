//Base card class that will be used for all card objects in this game
enum ECardAspect
{
    BASIC = 0,
    LEADERSHIP,
    JUSTICE,
    AGGRESSION,
    PROTECTION,
    POOL
}

enum ECardType
{
    HERO = 0,
    EVENT,
    SUPPORT,
    UPGRADE,
    ALLY,
    RESOURCE,
    VILLAIN,
    MAINSCHEME,
    TREACHERY,
    MINION,
    OBLIGATION,
    SIDESCHEME,
    ATTATCHMENT
}

class ABaseCard : AActor
{
    FName CardName = n"";

    UFUNCTION(BlueprintEvent)
    void SetupCard()
    {
        Log(n"LogCardError", f"Warning!!! Card Setup function has not been overidden");
    }

    UPROPERTY()
    UTexture FrontImage;

    UPROPERTY()
    UTexture BackImage;

}