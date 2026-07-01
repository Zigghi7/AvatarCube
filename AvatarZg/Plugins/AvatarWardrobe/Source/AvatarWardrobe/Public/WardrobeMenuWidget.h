#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "WardrobeTypes.h"
#include "WardrobeMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UWrapBox;
class UMetaHumanWardrobeComponent;
class UWardrobeCatalogData;
class UWardrobeItemData;
class UWardrobeItemEntryWidget;
class UWardrobeMaterialVariantWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWardrobeCloseRequestedSignature);

UCLASS(Abstract, Blueprintable)
class AVATARWARDROBE_API UWardrobeMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
    FWardrobeCloseRequestedSignature OnCloseRequested;

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void InitializeWardrobe(
        UWardrobeCatalogData* InCatalog,
        UMetaHumanWardrobeComponent* InWardrobeComponent);

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void SetCategory(EWardrobeCategory NewCategory);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe|Classes")
    TSubclassOf<UWardrobeItemEntryWidget> ItemEntryWidgetClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe|Classes")
    TSubclassOf<UWardrobeMaterialVariantWidget> MaterialVariantWidgetClass;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Button_Tops;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Button_Bottoms;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Button_Shoes;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Button_Vests;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Button_Accessories;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Button_Unequip;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Button_Close;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UComboBoxString> Combo_Subcategory;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UWrapBox> ItemsWrapBox;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UVerticalBox> MaterialVariantsBox;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Text_CategoryTitle;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Text_SelectedItem;

private:
    UPROPERTY(Transient)
    TObjectPtr<UWardrobeCatalogData> Catalog;

    UPROPERTY(Transient)
    TObjectPtr<UMetaHumanWardrobeComponent> WardrobeComponent;

    UPROPERTY(Transient)
    TObjectPtr<UWardrobeItemData> SelectedItem;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UWardrobeItemEntryWidget>> SpawnedItemEntries;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UWardrobeMaterialVariantWidget>> SpawnedVariantEntries;

    EWardrobeCategory CurrentCategory = EWardrobeCategory::Tops;
    FName CurrentSubcategory = NAME_None;
    bool bRefreshingSubcategoryOptions = false;

    UFUNCTION()
    void HandleTopsClicked();

    UFUNCTION()
    void HandleBottomsClicked();

    UFUNCTION()
    void HandleShoesClicked();

    UFUNCTION()
    void HandleVestsClicked();

    UFUNCTION()
    void HandleAccessoriesClicked();

    UFUNCTION()
    void HandleUnequipClicked();

    UFUNCTION()
    void HandleCloseClicked();

    UFUNCTION()
    void HandleSubcategoryChanged(FString SelectedOption, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void HandleItemClicked(UWardrobeItemData* Item);

    UFUNCTION()
    void HandleVariantClicked(int32 VariantIndex);

    void PopulateSubcategories();
    void RebuildItemGrid();
    void RebuildMaterialVariants(UWardrobeItemData* Item);
    void RefreshEntrySelection();
    void BindControls();
    void UnbindControls();

    static FText GetCategoryDisplayText(EWardrobeCategory Category);
};
