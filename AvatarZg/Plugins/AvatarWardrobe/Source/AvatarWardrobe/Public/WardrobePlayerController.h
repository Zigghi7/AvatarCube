#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "WardrobePlayerController.generated.h"

class UMetaHumanWardrobeComponent;
class UWardrobeCatalogData;
class UWardrobeMenuWidget;

UCLASS(Blueprintable)
class AVATARWARDROBE_API AWardrobePlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void ToggleWardrobe();

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void OpenWardrobe();

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void CloseWardrobe();

    UFUNCTION(BlueprintCallable, Category = "Wardrobe|Persistence")
    bool SaveWardrobe();

    UFUNCTION(BlueprintCallable, Category = "Wardrobe|Persistence")
    bool LoadWardrobe();

    UFUNCTION(BlueprintCallable, Category = "Wardrobe|Persistence")
    bool DeleteWardrobeSave();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void SetupInputComponent() override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe")
    TSubclassOf<UWardrobeMenuWidget> WardrobeWidgetClass;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe")
    TObjectPtr<UWardrobeCatalogData> WardrobeCatalog;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe")
    FName WardrobeTargetActorTag = TEXT("WardrobeTarget");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe")
    bool bBlockMoveAndLookInputWhileOpen = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe|Persistence")
    bool bAutoLoadWardrobe = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe|Persistence")
    bool bAutoSaveWardrobe = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe|Persistence")
    FString WardrobeSaveSlotName = TEXT("AvatarWardrobe");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe|Persistence", meta = (ClampMin = "0"))
    int32 WardrobeSaveUserIndex = 0;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wardrobe|Persistence", meta = (ClampMin = "0.05"))
    float PersistenceRetryDelay = 0.25f;

private:
    UPROPERTY(Transient)
    TObjectPtr<UWardrobeMenuWidget> WardrobeWidget;

    TWeakObjectPtr<UMetaHumanWardrobeComponent> CachedWardrobeComponent;
    bool bWardrobeIsOpen = false;
    bool bPersistenceInitialized = false;
    bool bIsRestoringWardrobe = false;
    FTimerHandle PersistenceRetryTimerHandle;

    UMetaHumanWardrobeComponent* FindWardrobeComponent();
    void TryInitializeWardrobePersistence();
    void BindPersistenceDelegates(UMetaHumanWardrobeComponent* WardrobeComponent);
    void UnbindPersistenceDelegates();

    UFUNCTION()
    void HandleCloseRequested();

    UFUNCTION()
    void HandleWardrobeStateChanged();
};
