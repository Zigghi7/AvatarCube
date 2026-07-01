#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WardrobeTypes.h"
#include "WardrobeItemData.generated.h"

class UAnimInstance;
class USkeletalMesh;
class UTexture2D;

UCLASS(BlueprintType)
class AVATARWARDROBE_API UWardrobeItemData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
    FName ItemId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
    EWardrobeCategory Category = EWardrobeCategory::Tops;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
    FName Subcategory = TEXT("All");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe")
    TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
    EWardrobePoseMode PoseMode = EWardrobePoseMode::LeaderPose;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (EditCondition = "PoseMode == EWardrobePoseMode::CopyPose"))
    TSubclassOf<UAnimInstance> CopyPoseAnimClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visibility")
    TArray<FName> ComponentsToHide;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
    TArray<FWardrobeMaterialVariant> MaterialVariants;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials", meta = (ClampMin = "0"))
    int32 DefaultVariantIndex = 0;
};
