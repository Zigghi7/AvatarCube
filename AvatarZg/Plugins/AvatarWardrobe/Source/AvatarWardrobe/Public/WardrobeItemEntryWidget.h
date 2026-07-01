#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WardrobeItemEntryWidget.generated.h"

class UBorder;
class UButton;
class UImage;
class UTextBlock;
class UWardrobeItemData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FWardrobeItemClickedSignature,
    UWardrobeItemData*, Item);

UCLASS(Abstract, Blueprintable)
class AVATARWARDROBE_API UWardrobeItemEntryWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
    FWardrobeItemClickedSignature OnItemClicked;

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void SetItemData(UWardrobeItemData* InItemData);

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void SetSelected(bool bInSelected);

    UFUNCTION(BlueprintPure, Category = "Wardrobe")
    UWardrobeItemData* GetItemData() const { return ItemData; }

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> ItemButton;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> ThumbnailImage;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> NameText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UBorder> SelectionBorder;

private:
    UPROPERTY(Transient)
    TObjectPtr<UWardrobeItemData> ItemData;

    UFUNCTION()
    void HandleClicked();
};
