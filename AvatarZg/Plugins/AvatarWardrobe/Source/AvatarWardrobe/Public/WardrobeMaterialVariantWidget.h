#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WardrobeTypes.h"
#include "WardrobeMaterialVariantWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FWardrobeVariantClickedSignature,
    int32, VariantIndex);

UCLASS(Abstract, Blueprintable)
class AVATARWARDROBE_API UWardrobeMaterialVariantWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
    FWardrobeVariantClickedSignature OnVariantClicked;

    void SetVariantData(int32 InVariantIndex, const FWardrobeMaterialVariant& InVariant);

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void SetSelected(bool bInSelected);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> VariantButton;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> VariantNameText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> ColorPreview;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UBorder> SelectionBorder;

private:
    int32 VariantIndex = INDEX_NONE;

    UFUNCTION()
    void HandleClicked();
};
