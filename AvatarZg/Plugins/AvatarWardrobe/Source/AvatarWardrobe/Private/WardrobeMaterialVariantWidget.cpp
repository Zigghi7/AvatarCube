#include "WardrobeMaterialVariantWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UWardrobeMaterialVariantWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (IsValid(VariantButton))
    {
        VariantButton->OnClicked.RemoveDynamic(this, &UWardrobeMaterialVariantWidget::HandleClicked);
        VariantButton->OnClicked.AddDynamic(this, &UWardrobeMaterialVariantWidget::HandleClicked);
    }
}

void UWardrobeMaterialVariantWidget::NativeDestruct()
{
    if (IsValid(VariantButton))
    {
        VariantButton->OnClicked.RemoveDynamic(this, &UWardrobeMaterialVariantWidget::HandleClicked);
    }

    Super::NativeDestruct();
}

void UWardrobeMaterialVariantWidget::SetVariantData(
    const int32 InVariantIndex,
    const FWardrobeMaterialVariant& InVariant)
{
    VariantIndex = InVariantIndex;

    if (IsValid(VariantNameText))
    {
        VariantNameText->SetText(InVariant.DisplayName);
    }

    if (IsValid(ColorPreview))
    {
        if (!InVariant.PreviewTexture.IsNull())
        {
            // UImage streams the soft texture and uses it as the brush resource.
            // Keep the widget tint white so the preview preserves its original colors.
            ColorPreview->SetColorAndOpacity(FLinearColor::White);
            ColorPreview->SetBrushFromSoftTexture(InVariant.PreviewTexture, false);
        }
        else
        {
            // Backward-compatible fallback for variants that still use a flat tint.
            ColorPreview->SetColorAndOpacity(
                InVariant.bApplyTint ? InVariant.TintColor : FLinearColor::White);
        }
    }
}

void UWardrobeMaterialVariantWidget::SetSelected(const bool bInSelected)
{
    if (IsValid(SelectionBorder))
    {
        SelectionBorder->SetBrushColor(
            bInSelected
                ? FLinearColor(0.0f, 0.75f, 1.0f, 1.0f)
                : FLinearColor(0.12f, 0.12f, 0.12f, 0.75f));
    }
}

void UWardrobeMaterialVariantWidget::HandleClicked()
{
    if (VariantIndex != INDEX_NONE)
    {
        OnVariantClicked.Broadcast(VariantIndex);
    }
}
