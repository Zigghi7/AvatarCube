#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WardrobeCatalogData.generated.h"

class UWardrobeItemData;

UCLASS(BlueprintType)
class AVATARWARDROBE_API UWardrobeCatalogData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
    TArray<TObjectPtr<UWardrobeItemData>> Items;
};
