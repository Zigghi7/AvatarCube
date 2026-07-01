#include "WardrobeMenuWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "MetaHumanWardrobeComponent.h"
#include "WardrobeCatalogData.h"
#include "WardrobeItemData.h"
#include "WardrobeItemEntryWidget.h"
#include "WardrobeMaterialVariantWidget.h"

void UWardrobeMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    BindControls();
    SetCategory(CurrentCategory);
}

void UWardrobeMenuWidget::NativeDestruct()
{
    UnbindControls();
    Super::NativeDestruct();
}

void UWardrobeMenuWidget::InitializeWardrobe(
    UWardrobeCatalogData* InCatalog,
    UMetaHumanWardrobeComponent* InWardrobeComponent)
{
    Catalog = InCatalog;
    WardrobeComponent = InWardrobeComponent;

    if (IsValid(ItemsWrapBox))
    {
        SetCategory(CurrentCategory);
    }
}

void UWardrobeMenuWidget::SetCategory(const EWardrobeCategory NewCategory)
{
    CurrentCategory = NewCategory;
    CurrentSubcategory = NAME_None;

    if (IsValid(Text_CategoryTitle))
    {
        Text_CategoryTitle->SetText(GetCategoryDisplayText(CurrentCategory));
    }

    PopulateSubcategories();
    RebuildItemGrid();

    SelectedItem = IsValid(WardrobeComponent)
        ? WardrobeComponent->GetEquippedItem(CurrentCategory)
        : nullptr;

    RebuildMaterialVariants(SelectedItem);
}

void UWardrobeMenuWidget::BindControls()
{
    if (IsValid(Button_Tops))
    {
        Button_Tops->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleTopsClicked);
        Button_Tops->OnClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleTopsClicked);
    }

    if (IsValid(Button_Bottoms))
    {
        Button_Bottoms->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleBottomsClicked);
        Button_Bottoms->OnClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleBottomsClicked);
    }

    if (IsValid(Button_Shoes))
    {
        Button_Shoes->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleShoesClicked);
        Button_Shoes->OnClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleShoesClicked);
    }

    if (IsValid(Button_Vests))
    {
        Button_Vests->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleVestsClicked);
        Button_Vests->OnClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleVestsClicked);
    }

    if (IsValid(Button_Accessories))
    {
        Button_Accessories->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleAccessoriesClicked);
        Button_Accessories->OnClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleAccessoriesClicked);
    }

    if (IsValid(Button_Unequip))
    {
        Button_Unequip->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleUnequipClicked);
        Button_Unequip->OnClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleUnequipClicked);
    }

    if (IsValid(Button_Close))
    {
        Button_Close->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleCloseClicked);
        Button_Close->OnClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleCloseClicked);
    }

    if (IsValid(Combo_Subcategory))
    {
        Combo_Subcategory->OnSelectionChanged.RemoveDynamic(
            this,
            &UWardrobeMenuWidget::HandleSubcategoryChanged);
        Combo_Subcategory->OnSelectionChanged.AddDynamic(
            this,
            &UWardrobeMenuWidget::HandleSubcategoryChanged);
    }
}

void UWardrobeMenuWidget::UnbindControls()
{
    if (IsValid(Button_Tops))
    {
        Button_Tops->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleTopsClicked);
    }
    if (IsValid(Button_Bottoms))
    {
        Button_Bottoms->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleBottomsClicked);
    }
    if (IsValid(Button_Shoes))
    {
        Button_Shoes->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleShoesClicked);
    }
    if (IsValid(Button_Vests))
    {
        Button_Vests->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleVestsClicked);
    }
    if (IsValid(Button_Accessories))
    {
        Button_Accessories->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleAccessoriesClicked);
    }
    if (IsValid(Button_Unequip))
    {
        Button_Unequip->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleUnequipClicked);
    }
    if (IsValid(Button_Close))
    {
        Button_Close->OnClicked.RemoveDynamic(this, &UWardrobeMenuWidget::HandleCloseClicked);
    }
    if (IsValid(Combo_Subcategory))
    {
        Combo_Subcategory->OnSelectionChanged.RemoveDynamic(
            this,
            &UWardrobeMenuWidget::HandleSubcategoryChanged);
    }
}

void UWardrobeMenuWidget::PopulateSubcategories()
{
    if (!IsValid(Combo_Subcategory))
    {
        return;
    }

    bRefreshingSubcategoryOptions = true;
    Combo_Subcategory->ClearOptions();
    Combo_Subcategory->AddOption(TEXT("Tutti"));

    TSet<FString> UniqueSubcategories;
    if (IsValid(Catalog))
    {
        for (UWardrobeItemData* Item : Catalog->Items)
        {
            if (!IsValid(Item) || Item->Category != CurrentCategory || Item->Subcategory.IsNone())
            {
                continue;
            }

            UniqueSubcategories.Add(Item->Subcategory.ToString());
        }
    }

    TArray<FString> SortedSubcategories = UniqueSubcategories.Array();
    SortedSubcategories.Sort();

    for (const FString& Subcategory : SortedSubcategories)
    {
        Combo_Subcategory->AddOption(Subcategory);
    }

    Combo_Subcategory->SetSelectedOption(TEXT("Tutti"));
    bRefreshingSubcategoryOptions = false;
}

void UWardrobeMenuWidget::RebuildItemGrid()
{
    SpawnedItemEntries.Reset();

    if (!IsValid(ItemsWrapBox))
    {
        return;
    }

    ItemsWrapBox->ClearChildren();

    if (!IsValid(Catalog) || !ItemEntryWidgetClass)
    {
        return;
    }

    for (UWardrobeItemData* Item : Catalog->Items)
    {
        if (!IsValid(Item) || Item->Category != CurrentCategory)
        {
            continue;
        }

        if (!CurrentSubcategory.IsNone() && Item->Subcategory != CurrentSubcategory)
        {
            continue;
        }

        UWardrobeItemEntryWidget* Entry = CreateWidget<UWardrobeItemEntryWidget>(
            GetOwningPlayer(),
            ItemEntryWidgetClass);

        if (!IsValid(Entry))
        {
            continue;
        }

        Entry->SetItemData(Item);
        Entry->OnItemClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleItemClicked);
        ItemsWrapBox->AddChildToWrapBox(Entry);
        SpawnedItemEntries.Add(Entry);
    }

    RefreshEntrySelection();
}

void UWardrobeMenuWidget::RebuildMaterialVariants(UWardrobeItemData* Item)
{
    SpawnedVariantEntries.Reset();

    if (IsValid(MaterialVariantsBox))
    {
        MaterialVariantsBox->ClearChildren();
    }

    if (IsValid(Text_SelectedItem))
    {
        Text_SelectedItem->SetText(IsValid(Item) ? Item->DisplayName : FText::FromString(TEXT("Nessun capo selezionato")));
    }

    if (!IsValid(Item) || !IsValid(MaterialVariantsBox) || !MaterialVariantWidgetClass)
    {
        return;
    }

    const int32 SelectedVariant = IsValid(WardrobeComponent)
        ? WardrobeComponent->GetSelectedVariantIndex(Item->Category)
        : INDEX_NONE;

    for (int32 Index = 0; Index < Item->MaterialVariants.Num(); ++Index)
    {
        UWardrobeMaterialVariantWidget* VariantEntry =
            CreateWidget<UWardrobeMaterialVariantWidget>(GetOwningPlayer(), MaterialVariantWidgetClass);

        if (!IsValid(VariantEntry))
        {
            continue;
        }

        VariantEntry->SetVariantData(Index, Item->MaterialVariants[Index]);
        VariantEntry->SetSelected(Index == SelectedVariant);
        VariantEntry->OnVariantClicked.AddDynamic(this, &UWardrobeMenuWidget::HandleVariantClicked);
        MaterialVariantsBox->AddChildToVerticalBox(VariantEntry);
        SpawnedVariantEntries.Add(VariantEntry);
    }
}

void UWardrobeMenuWidget::RefreshEntrySelection()
{
    UWardrobeItemData* EquippedItem = IsValid(WardrobeComponent)
        ? WardrobeComponent->GetEquippedItem(CurrentCategory)
        : nullptr;

    for (UWardrobeItemEntryWidget* Entry : SpawnedItemEntries)
    {
        if (IsValid(Entry))
        {
            Entry->SetSelected(Entry->GetItemData() == EquippedItem);
        }
    }
}

void UWardrobeMenuWidget::HandleTopsClicked()
{
    SetCategory(EWardrobeCategory::Tops);
}

void UWardrobeMenuWidget::HandleBottomsClicked()
{
    SetCategory(EWardrobeCategory::Bottoms);
}

void UWardrobeMenuWidget::HandleShoesClicked()
{
    SetCategory(EWardrobeCategory::Shoes);
}

void UWardrobeMenuWidget::HandleVestsClicked()
{
    SetCategory(EWardrobeCategory::Vests);
}

void UWardrobeMenuWidget::HandleAccessoriesClicked()
{
    SetCategory(EWardrobeCategory::Accessories);
}

void UWardrobeMenuWidget::HandleUnequipClicked()
{
    if (IsValid(WardrobeComponent))
    {
        WardrobeComponent->UnequipCategory(CurrentCategory);
    }

    SelectedItem = nullptr;
    RefreshEntrySelection();
    RebuildMaterialVariants(nullptr);
}

void UWardrobeMenuWidget::HandleCloseClicked()
{
    OnCloseRequested.Broadcast();
}

void UWardrobeMenuWidget::HandleSubcategoryChanged(
    const FString SelectedOption,
    ESelectInfo::Type SelectionType)
{
    if (bRefreshingSubcategoryOptions)
    {
        return;
    }

    CurrentSubcategory = SelectedOption.Equals(TEXT("Tutti"), ESearchCase::IgnoreCase)
        ? NAME_None
        : FName(*SelectedOption);

    RebuildItemGrid();
}

void UWardrobeMenuWidget::HandleItemClicked(UWardrobeItemData* Item)
{
    if (!IsValid(Item) || !IsValid(WardrobeComponent))
    {
        return;
    }

    SelectedItem = Item;
    WardrobeComponent->EquipItem(Item);
    RefreshEntrySelection();
    RebuildMaterialVariants(Item);
}

void UWardrobeMenuWidget::HandleVariantClicked(const int32 VariantIndex)
{
    if (!IsValid(SelectedItem) || !IsValid(WardrobeComponent))
    {
        return;
    }

    WardrobeComponent->ApplyMaterialVariant(SelectedItem->Category, VariantIndex);

    for (int32 Index = 0; Index < SpawnedVariantEntries.Num(); ++Index)
    {
        if (IsValid(SpawnedVariantEntries[Index]))
        {
            SpawnedVariantEntries[Index]->SetSelected(Index == VariantIndex);
        }
    }
}

FText UWardrobeMenuWidget::GetCategoryDisplayText(const EWardrobeCategory Category)
{
    switch (Category)
    {
        case EWardrobeCategory::Tops:
            return FText::FromString(TEXT("Tops"));
        case EWardrobeCategory::Bottoms:
            return FText::FromString(TEXT("Bottoms"));
        case EWardrobeCategory::Shoes:
            return FText::FromString(TEXT("Shoes"));
        case EWardrobeCategory::Vests:
            return FText::FromString(TEXT("Vests"));
        case EWardrobeCategory::Accessories:
            return FText::FromString(TEXT("Accessories"));
        default:
            return FText::FromString(TEXT("Wardrobe"));
    }
}
