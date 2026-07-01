#pragma once

#include "CoreMinimal.h"
#include "WardrobeTypes.generated.h"

class UMaterialInterface;
class UTexture2D;

UENUM(BlueprintType)
enum class EWardrobeCategory : uint8
{
    Tops        UMETA(DisplayName = "Tops"),
    Bottoms     UMETA(DisplayName = "Bottoms"),
    Shoes       UMETA(DisplayName = "Shoes"),
    Vests       UMETA(DisplayName = "Vests"),
    Accessories UMETA(DisplayName = "Accessories")
};

UENUM(BlueprintType)
enum class EWardrobePoseMode : uint8
{
    LeaderPose UMETA(DisplayName = "Leader Pose - Lightweight"),
    CopyPose   UMETA(DisplayName = "Copy Pose - Cloth/Extra Bones")
};

USTRUCT(BlueprintType)
struct AVATARWARDROBE_API FWardrobeMaterialVariant
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
    FText DisplayName = FText::FromString(TEXT("Default"));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant", meta = (ClampMin = "0"))
    int32 MaterialSlotIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
    TSoftObjectPtr<UMaterialInterface> MaterialOverride;

    /**
     * Optional runtime preview shown in the wardrobe UI.
     * Content Browser material thumbnails are editor-only, so assign a Texture2D
     * containing the desired material preview here.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant|Preview")
    TSoftObjectPtr<UTexture2D> PreviewTexture;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant")
    bool bApplyTint = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant", meta = (EditCondition = "bApplyTint"))
    FName TintParameterName = TEXT("Tint");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variant", meta = (EditCondition = "bApplyTint"))
    FLinearColor TintColor = FLinearColor::White;
};
