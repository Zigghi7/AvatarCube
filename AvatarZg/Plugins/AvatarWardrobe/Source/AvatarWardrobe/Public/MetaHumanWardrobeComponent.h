#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WardrobeTypes.h"
#include "MetaHumanWardrobeComponent.generated.h"

struct FStreamableHandle;
class UMaterialInterface;
class USceneComponent;
class USkeletalMeshComponent;
class UWardrobeItemData;

DECLARE_LOG_CATEGORY_EXTERN(LogAvatarWardrobe, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FWardrobeItemEquippedSignature,
    EWardrobeCategory, Category,
    UWardrobeItemData*, Item);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FWardrobeCategoryUnequippedSignature,
    EWardrobeCategory, Category);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWardrobeStateChangedSignature);

UCLASS(ClassGroup = (Avatar), meta = (BlueprintSpawnableComponent))
class AVATARWARDROBE_API UMetaHumanWardrobeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMetaHumanWardrobeComponent();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe|MetaHuman")
    FName LeaderMeshComponentName = TEXT("Body");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wardrobe|MetaHuman")
    FName LeaderMeshComponentTag = TEXT("WardrobeLeader");

    UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
    FWardrobeItemEquippedSignature OnItemEquipped;

    UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
    FWardrobeCategoryUnequippedSignature OnCategoryUnequipped;

    // Broadcast whenever the selected item, category occupancy, or material variant changes.
    UPROPERTY(BlueprintAssignable, Category = "Wardrobe")
    FWardrobeStateChangedSignature OnWardrobeStateChanged;

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void EquipItem(UWardrobeItemData* Item);

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void UnequipCategory(EWardrobeCategory Category);

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    void ApplyMaterialVariant(EWardrobeCategory Category, int32 VariantIndex);

    UFUNCTION(BlueprintPure, Category = "Wardrobe")
    UWardrobeItemData* GetEquippedItem(EWardrobeCategory Category) const;

    UFUNCTION(BlueprintPure, Category = "Wardrobe")
    int32 GetSelectedVariantIndex(EWardrobeCategory Category) const;

    UFUNCTION(BlueprintPure, Category = "Wardrobe")
    USkeletalMeshComponent* GetGarmentComponent(EWardrobeCategory Category) const;

    UFUNCTION(BlueprintCallable, Category = "Wardrobe")
    bool ResolveLeaderMesh();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> LeaderMeshComponent;

    UPROPERTY(Transient)
    TMap<EWardrobeCategory, TObjectPtr<USkeletalMeshComponent>> GarmentComponents;

    UPROPERTY(Transient)
    TMap<EWardrobeCategory, TObjectPtr<UWardrobeItemData>> EquippedItems;

    UPROPERTY(Transient)
    TMap<EWardrobeCategory, int32> SelectedVariantIndices;

    TMap<EWardrobeCategory, TSharedPtr<FStreamableHandle>> MeshLoadHandles;
    TMap<EWardrobeCategory, TSharedPtr<FStreamableHandle>> MaterialLoadHandles;
    TMap<EWardrobeCategory, int32> MeshRequestSerials;
    TMap<EWardrobeCategory, int32> MaterialRequestSerials;

    TMap<TWeakObjectPtr<USceneComponent>, bool> OriginalVisibilityByComponent;

    USkeletalMeshComponent* GetOrCreateGarmentComponent(EWardrobeCategory Category);
    USceneComponent* FindOwnedSceneComponentByName(FName ComponentName) const;

    void HandleMeshLoaded(
        EWardrobeCategory Category,
        TWeakObjectPtr<UWardrobeItemData> ExpectedItem,
        int32 RequestSerial);

    void ConfigurePoseForItem(
        USkeletalMeshComponent* GarmentComponent,
        UWardrobeItemData* Item);

    void ApplyLoadedMaterialVariant(
        EWardrobeCategory Category,
        UWardrobeItemData* Item,
        int32 VariantIndex);

    void HandleMaterialLoaded(
        EWardrobeCategory Category,
        TWeakObjectPtr<UWardrobeItemData> ExpectedItem,
        int32 VariantIndex,
        int32 RequestSerial);

    void RefreshHiddenComponents();
    void RestoreManagedVisibility();
    void CancelAllLoads();

    static FName MakeGarmentComponentName(EWardrobeCategory Category);
};
