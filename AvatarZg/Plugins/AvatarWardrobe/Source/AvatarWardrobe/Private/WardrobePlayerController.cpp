#include "WardrobePlayerController.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "MetaHumanWardrobeComponent.h"
#include "TimerManager.h"
#include "WardrobeCatalogData.h"
#include "WardrobeItemData.h"
#include "WardrobeMenuWidget.h"
#include "WardrobeSaveGame.h"

namespace
{
    const EWardrobeCategory WardrobeCategories[] =
    {
        EWardrobeCategory::Tops,
        EWardrobeCategory::Bottoms,
        EWardrobeCategory::Shoes,
        EWardrobeCategory::Vests,
        EWardrobeCategory::Accessories
    };
}

void AWardrobePlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Delay until the next frame so the MetaHuman Blueprint has already bound
    // its OnItemEquipped event before saved items are restored.
    GetWorldTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateUObject(
            this,
            &AWardrobePlayerController::TryInitializeWardrobePersistence));
}

void AWardrobePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bAutoSaveWardrobe && bPersistenceInitialized && !bIsRestoringWardrobe)
    {
        SaveWardrobe();
    }

    GetWorldTimerManager().ClearTimer(PersistenceRetryTimerHandle);
    UnbindPersistenceDelegates();

    Super::EndPlay(EndPlayReason);
}

void AWardrobePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (InputComponent)
    {
        InputComponent->BindKey(
            EKeys::I,
            IE_Pressed,
            this,
            &AWardrobePlayerController::ToggleWardrobe);
    }
}

void AWardrobePlayerController::TryInitializeWardrobePersistence()
{
    if (bPersistenceInitialized)
    {
        return;
    }

    if (!IsValid(WardrobeCatalog))
    {
        UE_LOG(LogTemp, Error, TEXT("Wardrobe persistence cannot initialize: WardrobeCatalog is not assigned on %s."), *GetName());
        return;
    }

    UMetaHumanWardrobeComponent* WardrobeComponent = FindWardrobeComponent();
    if (!IsValid(WardrobeComponent))
    {
        GetWorldTimerManager().SetTimer(
            PersistenceRetryTimerHandle,
            this,
            &AWardrobePlayerController::TryInitializeWardrobePersistence,
            PersistenceRetryDelay,
            false);
        return;
    }

    BindPersistenceDelegates(WardrobeComponent);
    bPersistenceInitialized = true;

    if (bAutoLoadWardrobe)
    {
        LoadWardrobe();
    }
}

void AWardrobePlayerController::BindPersistenceDelegates(
    UMetaHumanWardrobeComponent* WardrobeComponent)
{
    if (!IsValid(WardrobeComponent))
    {
        return;
    }

    WardrobeComponent->OnWardrobeStateChanged.RemoveDynamic(
        this,
        &AWardrobePlayerController::HandleWardrobeStateChanged);

    WardrobeComponent->OnWardrobeStateChanged.AddDynamic(
        this,
        &AWardrobePlayerController::HandleWardrobeStateChanged);
}

void AWardrobePlayerController::UnbindPersistenceDelegates()
{
    if (UMetaHumanWardrobeComponent* WardrobeComponent = CachedWardrobeComponent.Get())
    {
        WardrobeComponent->OnWardrobeStateChanged.RemoveDynamic(
            this,
            &AWardrobePlayerController::HandleWardrobeStateChanged);
    }
}

void AWardrobePlayerController::HandleWardrobeStateChanged()
{
    if (bAutoSaveWardrobe && bPersistenceInitialized && !bIsRestoringWardrobe)
    {
        SaveWardrobe();
    }
}

bool AWardrobePlayerController::SaveWardrobe()
{
    UMetaHumanWardrobeComponent* WardrobeComponent = FindWardrobeComponent();
    if (!IsValid(WardrobeComponent))
    {
        UE_LOG(LogTemp, Warning, TEXT("SaveWardrobe failed: wardrobe component not found."));
        return false;
    }

    UWardrobeSaveGame* SaveObject = Cast<UWardrobeSaveGame>(
        UGameplayStatics::CreateSaveGameObject(UWardrobeSaveGame::StaticClass()));

    if (!IsValid(SaveObject))
    {
        UE_LOG(LogTemp, Error, TEXT("SaveWardrobe failed: could not create save object."));
        return false;
    }

    SaveObject->EquippedItems.Reset();

    for (const EWardrobeCategory Category : WardrobeCategories)
    {
        UWardrobeItemData* Item = WardrobeComponent->GetEquippedItem(Category);
        if (!IsValid(Item))
        {
            continue;
        }

        FWardrobeSavedItem SavedItem;
        SavedItem.Category = Category;
        SavedItem.ItemId = Item->ItemId;
        SavedItem.ItemAssetPath = FSoftObjectPath(Item->GetPathName());
        SavedItem.MaterialVariantIndex = WardrobeComponent->GetSelectedVariantIndex(Category);
        SaveObject->EquippedItems.Add(SavedItem);
    }

    const bool bSaved = UGameplayStatics::SaveGameToSlot(
        SaveObject,
        WardrobeSaveSlotName,
        WardrobeSaveUserIndex);

    if (bSaved)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("Wardrobe save completed. Slot='%s', Items=%d"),
            *WardrobeSaveSlotName,
            SaveObject->EquippedItems.Num());
    }
    else
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Wardrobe save failed. Slot='%s'."),
            *WardrobeSaveSlotName);
    }

    return bSaved;
}

bool AWardrobePlayerController::LoadWardrobe()
{
    if (!UGameplayStatics::DoesSaveGameExist(WardrobeSaveSlotName, WardrobeSaveUserIndex))
    {
        UE_LOG(LogTemp, Log, TEXT("No wardrobe save exists yet in slot '%s'."), *WardrobeSaveSlotName);
        return false;
    }

    UMetaHumanWardrobeComponent* WardrobeComponent = FindWardrobeComponent();
    if (!IsValid(WardrobeComponent) || !IsValid(WardrobeCatalog))
    {
        UE_LOG(LogTemp, Warning, TEXT("LoadWardrobe failed: component or catalog is missing."));
        return false;
    }

    UWardrobeSaveGame* SaveObject = Cast<UWardrobeSaveGame>(
        UGameplayStatics::LoadGameFromSlot(
            WardrobeSaveSlotName,
            WardrobeSaveUserIndex));

    if (!IsValid(SaveObject))
    {
        UE_LOG(LogTemp, Error, TEXT("LoadWardrobe failed: invalid save object."));
        return false;
    }

    bIsRestoringWardrobe = true;

    for (const FWardrobeSavedItem& SavedItem : SaveObject->EquippedItems)
    {
        UWardrobeItemData* MatchingItem = nullptr;

        // First choice: stable logical ItemId.
        if (!SavedItem.ItemId.IsNone())
        {
            for (UWardrobeItemData* Candidate : WardrobeCatalog->Items)
            {
                if (IsValid(Candidate) && Candidate->ItemId == SavedItem.ItemId)
                {
                    MatchingItem = Candidate;
                    break;
                }
            }
        }

        // Fallback: original Data Asset path.
        if (!IsValid(MatchingItem) && SavedItem.ItemAssetPath.IsValid())
        {
            for (UWardrobeItemData* Candidate : WardrobeCatalog->Items)
            {
                if (IsValid(Candidate) && FSoftObjectPath(Candidate->GetPathName()) == SavedItem.ItemAssetPath)
                {
                    MatchingItem = Candidate;
                    break;
                }
            }
        }

        if (!IsValid(MatchingItem))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("Saved wardrobe item not found. ItemId='%s', Path='%s'."),
                *SavedItem.ItemId.ToString(),
                *SavedItem.ItemAssetPath.ToString());
            continue;
        }

        if (MatchingItem->Category != SavedItem.Category)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("Saved category mismatch for '%s'. Using the category configured on the item asset."),
                *MatchingItem->GetName());
        }

        WardrobeComponent->EquipItem(MatchingItem);

        if (MatchingItem->MaterialVariants.IsValidIndex(SavedItem.MaterialVariantIndex))
        {
            WardrobeComponent->ApplyMaterialVariant(
                MatchingItem->Category,
                SavedItem.MaterialVariantIndex);
        }
    }

    bIsRestoringWardrobe = false;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("Wardrobe load requested. Slot='%s', Items=%d"),
        *WardrobeSaveSlotName,
        SaveObject->EquippedItems.Num());

    return true;
}

bool AWardrobePlayerController::DeleteWardrobeSave()
{
    if (!UGameplayStatics::DoesSaveGameExist(WardrobeSaveSlotName, WardrobeSaveUserIndex))
    {
        return true;
    }

    return UGameplayStatics::DeleteGameInSlot(
        WardrobeSaveSlotName,
        WardrobeSaveUserIndex);
}

void AWardrobePlayerController::ToggleWardrobe()
{
    if (bWardrobeIsOpen)
    {
        CloseWardrobe();
    }
    else
    {
        OpenWardrobe();
    }
}

void AWardrobePlayerController::OpenWardrobe()
{
    if (bWardrobeIsOpen)
    {
        return;
    }

    if (!WardrobeWidgetClass)
    {
        UE_LOG(LogTemp, Error, TEXT("WardrobeWidgetClass is not assigned on %s."), *GetName());
        return;
    }

    if (!IsValid(WardrobeCatalog))
    {
        UE_LOG(LogTemp, Error, TEXT("WardrobeCatalog is not assigned on %s."), *GetName());
        return;
    }

    UMetaHumanWardrobeComponent* WardrobeComponent = FindWardrobeComponent();
    if (!IsValid(WardrobeComponent))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("No MetaHumanWardrobeComponent found. Add the component to the MetaHuman and tag the actor '%s'."),
            *WardrobeTargetActorTag.ToString());
        return;
    }

    if (!IsValid(WardrobeWidget))
    {
        WardrobeWidget = CreateWidget<UWardrobeMenuWidget>(this, WardrobeWidgetClass);
        if (!IsValid(WardrobeWidget))
        {
            return;
        }

        WardrobeWidget->OnCloseRequested.AddDynamic(
            this,
            &AWardrobePlayerController::HandleCloseRequested);
    }

    WardrobeWidget->InitializeWardrobe(WardrobeCatalog, WardrobeComponent);
    WardrobeWidget->AddToViewport(100);

    FInputModeGameAndUI InputMode;
    InputMode.SetWidgetToFocus(WardrobeWidget->TakeWidget());
    InputMode.SetHideCursorDuringCapture(false);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(InputMode);

    bShowMouseCursor = true;

    if (bBlockMoveAndLookInputWhileOpen)
    {
        SetIgnoreMoveInput(true);
        SetIgnoreLookInput(true);
    }

    bWardrobeIsOpen = true;
}

void AWardrobePlayerController::CloseWardrobe()
{
    if (!bWardrobeIsOpen)
    {
        return;
    }

    if (IsValid(WardrobeWidget))
    {
        WardrobeWidget->RemoveFromParent();
    }

    SetInputMode(FInputModeGameOnly());
    bShowMouseCursor = false;

    if (bBlockMoveAndLookInputWhileOpen)
    {
        ResetIgnoreMoveInput();
        ResetIgnoreLookInput();
    }

    bWardrobeIsOpen = false;
}

UMetaHumanWardrobeComponent* AWardrobePlayerController::FindWardrobeComponent()
{
    if (CachedWardrobeComponent.IsValid())
    {
        return CachedWardrobeComponent.Get();
    }

    if (!WardrobeTargetActorTag.IsNone())
    {
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor) || !Actor->ActorHasTag(WardrobeTargetActorTag))
            {
                continue;
            }

            if (UMetaHumanWardrobeComponent* Component =
                Actor->FindComponentByClass<UMetaHumanWardrobeComponent>())
            {
                CachedWardrobeComponent = Component;
                return Component;
            }
        }
    }

    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor))
        {
            continue;
        }

        if (UMetaHumanWardrobeComponent* Component =
            Actor->FindComponentByClass<UMetaHumanWardrobeComponent>())
        {
            CachedWardrobeComponent = Component;
            return Component;
        }
    }

    return nullptr;
}

void AWardrobePlayerController::HandleCloseRequested()
{
    CloseWardrobe();
}
