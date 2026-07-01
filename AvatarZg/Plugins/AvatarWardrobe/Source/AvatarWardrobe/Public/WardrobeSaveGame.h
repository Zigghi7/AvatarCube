#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WardrobeTypes.h"
#include "WardrobeSaveGame.generated.h"

USTRUCT(BlueprintType)
struct AVATARWARDROBE_API FWardrobeSavedItem
{
    GENERATED_BODY()

    UPROPERTY(SaveGame)
    EWardrobeCategory Category = EWardrobeCategory::Tops;

    // Stable logical ID configured on the WardrobeItemData asset.
    UPROPERTY(SaveGame)
    FName ItemId = NAME_None;

    // Fallback if ItemId is missing or changed.
    UPROPERTY(SaveGame)
    FSoftObjectPath ItemAssetPath;

    // The selected material/color variant for this item.
    UPROPERTY(SaveGame)
    int32 MaterialVariantIndex = INDEX_NONE;
};

UCLASS(BlueprintType)
class AVATARWARDROBE_API UWardrobeSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY(SaveGame)
    int32 SaveVersion = 1;

    UPROPERTY(SaveGame)
    TArray<FWardrobeSavedItem> EquippedItems;
};
