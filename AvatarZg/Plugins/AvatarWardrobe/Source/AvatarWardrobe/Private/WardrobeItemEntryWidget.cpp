#include "WardrobeItemEntryWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "WardrobeItemData.h"

void UWardrobeItemEntryWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (IsValid(ItemButton))
    {
        ItemButton->OnClicked.RemoveDynamic(this, &UWardrobeItemEntryWidget::HandleClicked);
        ItemButton->OnClicked.AddDynamic(this, &UWardrobeItemEntryWidget::HandleClicked);
    }
}

void UWardrobeItemEntryWidget::NativeDestruct()
{
    if (IsValid(ItemButton))
    {
        ItemButton->OnClicked.RemoveDynamic(this, &UWardrobeItemEntryWidget::HandleClicked);
    }

    Super::NativeDestruct();
}

void UWardrobeItemEntryWidget::SetItemData(UWardrobeItemData* InItemData)
{
    ItemData = InItemData;

    if (!IsValid(ItemData))
    {
        if (IsValid(NameText))
        {
            NameText->SetText(FText::GetEmpty());
        }
        return;
    }

    if (IsValid(NameText))
    {
        NameText->SetText(ItemData->DisplayName);
    }

    if (IsValid(ThumbnailImage))
    {
        if (UTexture2D* Texture = ItemData->Thumbnail.LoadSynchronous())
        {
            ThumbnailImage->SetBrushFromTexture(Texture, true);
            ThumbnailImage->SetVisibility(ESlateVisibility::Visible);
        }
        else
        {
            ThumbnailImage->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

void UWardrobeItemEntryWidget::SetSelected(const bool bInSelected)
{
    if (IsValid(SelectionBorder))
    {
        SelectionBorder->SetBrushColor(
            bInSelected
                ? FLinearColor(0.0f, 0.75f, 1.0f, 1.0f)
                : FLinearColor(0.12f, 0.12f, 0.12f, 0.75f));
    }
}

void UWardrobeItemEntryWidget::HandleClicked()
{
    if (IsValid(ItemData))
    {
        OnItemClicked.Broadcast(ItemData);
    }
}
