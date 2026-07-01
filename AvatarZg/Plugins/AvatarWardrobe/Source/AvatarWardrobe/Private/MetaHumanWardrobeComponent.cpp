#include "MetaHumanWardrobeComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "WardrobeItemData.h"

DEFINE_LOG_CATEGORY(LogAvatarWardrobe);

UMetaHumanWardrobeComponent::UMetaHumanWardrobeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UMetaHumanWardrobeComponent::BeginPlay()
{
    Super::BeginPlay();
    ResolveLeaderMesh();
}

void UMetaHumanWardrobeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelAllLoads();
    RestoreManagedVisibility();
    Super::EndPlay(EndPlayReason);
}

bool UMetaHumanWardrobeComponent::ResolveLeaderMesh()
{
    LeaderMeshComponent = nullptr;

    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        UE_LOG(LogAvatarWardrobe, Error, TEXT("ResolveLeaderMesh failed: component has no valid owner."));
        return false;
    }

    TArray<USkeletalMeshComponent*> SkeletalComponents;
    Owner->GetComponents<USkeletalMeshComponent>(SkeletalComponents);

    if (!LeaderMeshComponentTag.IsNone())
    {
        for (USkeletalMeshComponent* Component : SkeletalComponents)
        {
            if (IsValid(Component) && Component->ComponentHasTag(LeaderMeshComponentTag))
            {
                LeaderMeshComponent = Component;
                break;
            }
        }
    }

    if (!IsValid(LeaderMeshComponent) && !LeaderMeshComponentName.IsNone())
    {
        for (USkeletalMeshComponent* Component : SkeletalComponents)
        {
            if (IsValid(Component) && Component->GetFName() == LeaderMeshComponentName)
            {
                LeaderMeshComponent = Component;
                break;
            }
        }
    }

    if (!IsValid(LeaderMeshComponent))
    {
        FString AvailableComponents;
        for (USkeletalMeshComponent* Component : SkeletalComponents)
        {
            if (IsValid(Component))
            {
                AvailableComponents += Component->GetName() + TEXT(" ");
            }
        }

        UE_LOG(
            LogAvatarWardrobe,
            Error,
            TEXT("No leader skeletal mesh found on %s. Expected name '%s' or tag '%s'. Available: %s"),
            *Owner->GetName(),
            *LeaderMeshComponentName.ToString(),
            *LeaderMeshComponentTag.ToString(),
            *AvailableComponents);
        return false;
    }

    UE_LOG(
        LogAvatarWardrobe,
        Log,
        TEXT("Wardrobe leader mesh resolved: %s on %s"),
        *LeaderMeshComponent->GetName(),
        *Owner->GetName());

    return true;
}

void UMetaHumanWardrobeComponent::EquipItem(UWardrobeItemData* Item)
{
    if (!IsValid(Item))
    {
        UE_LOG(LogAvatarWardrobe, Warning, TEXT("EquipItem ignored: Item is null."));
        return;
    }

    if (!IsValid(LeaderMeshComponent) && !ResolveLeaderMesh())
    {
        return;
    }

    const FSoftObjectPath MeshPath = Item->SkeletalMesh.ToSoftObjectPath();
    if (!MeshPath.IsValid())
    {
        UE_LOG(LogAvatarWardrobe, Warning, TEXT("Item '%s' has no valid Skeletal Mesh."), *Item->GetName());
        return;
    }

    const EWardrobeCategory Category = Item->Category;

    if (TSharedPtr<FStreamableHandle>* ExistingHandle = MeshLoadHandles.Find(Category))
    {
        if (ExistingHandle->IsValid())
        {
            (*ExistingHandle)->CancelHandle();
        }
    }

    EquippedItems.Add(Category, Item);
    SelectedVariantIndices.Add(Category, FMath::Max(0, Item->DefaultVariantIndex));
    OnWardrobeStateChanged.Broadcast();

    int32& RequestSerial = MeshRequestSerials.FindOrAdd(Category);
    ++RequestSerial;

    const int32 CapturedSerial = RequestSerial;
    const TWeakObjectPtr<UWardrobeItemData> WeakItem(Item);

    if (Item->SkeletalMesh.IsValid())
    {
        HandleMeshLoaded(Category, WeakItem, CapturedSerial);
        return;
    }

    FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
        MeshPath,
        FStreamableDelegate::CreateUObject(
            this,
            &UMetaHumanWardrobeComponent::HandleMeshLoaded,
            Category,
            WeakItem,
            CapturedSerial));

    MeshLoadHandles.Add(Category, Handle);
}

void UMetaHumanWardrobeComponent::HandleMeshLoaded(
    const EWardrobeCategory Category,
    const TWeakObjectPtr<UWardrobeItemData> ExpectedItem,
    const int32 RequestSerial)
{
    const int32* CurrentSerial = MeshRequestSerials.Find(Category);
    if (!CurrentSerial || *CurrentSerial != RequestSerial)
    {
        return;
    }

    UWardrobeItemData* Item = ExpectedItem.Get();
    if (!IsValid(Item) || EquippedItems.FindRef(Category) != Item)
    {
        return;
    }

    USkeletalMesh* LoadedMesh = Item->SkeletalMesh.Get();
    if (!IsValid(LoadedMesh))
    {
        UE_LOG(LogAvatarWardrobe, Error, TEXT("Failed to load Skeletal Mesh for item '%s'."), *Item->GetName());
        return;
    }

    USkeletalMeshComponent* GarmentComponent = GetOrCreateGarmentComponent(Category);
    if (!IsValid(GarmentComponent))
    {
        return;
    }

    GarmentComponent->SetVisibility(false, true);
    GarmentComponent->SetSkeletalMesh(LoadedMesh, true);
    ConfigurePoseForItem(GarmentComponent, Item);
    GarmentComponent->SetVisibility(true, true);

    RefreshHiddenComponents();

    const int32 VariantIndex = GetSelectedVariantIndex(Category);
    if (Item->MaterialVariants.IsValidIndex(VariantIndex))
    {
        ApplyMaterialVariant(Category, VariantIndex);
    }

    MeshLoadHandles.Remove(Category);
    OnItemEquipped.Broadcast(Category, Item);
}

void UMetaHumanWardrobeComponent::ConfigurePoseForItem(
    USkeletalMeshComponent* GarmentComponent,
    UWardrobeItemData* Item)
{
    if (!IsValid(GarmentComponent) || !IsValid(Item) || !IsValid(LeaderMeshComponent))
    {
        return;
    }

    GarmentComponent->AttachToComponent(
        LeaderMeshComponent,
        FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    GarmentComponent->SetRelativeTransform(FTransform::Identity);

    if (Item->PoseMode == EWardrobePoseMode::CopyPose && Item->CopyPoseAnimClass)
    {
        GarmentComponent->SetLeaderPoseComponent(nullptr, true);
        GarmentComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GarmentComponent->SetAnimInstanceClass(Item->CopyPoseAnimClass);
        GarmentComponent->SetComponentTickEnabled(true);
        return;
    }

    if (Item->PoseMode == EWardrobePoseMode::CopyPose && !Item->CopyPoseAnimClass)
    {
        UE_LOG(
            LogAvatarWardrobe,
            Warning,
            TEXT("Item '%s' uses Copy Pose but has no CopyPoseAnimClass. Falling back to Leader Pose."),
            *Item->GetName());
    }

    GarmentComponent->SetAnimInstanceClass(nullptr);
    GarmentComponent->SetLeaderPoseComponent(LeaderMeshComponent, true);
    GarmentComponent->SetComponentTickEnabled(false);
}

void UMetaHumanWardrobeComponent::UnequipCategory(const EWardrobeCategory Category)
{
    if (TSharedPtr<FStreamableHandle>* Handle = MeshLoadHandles.Find(Category))
    {
        if (Handle->IsValid())
        {
            (*Handle)->CancelHandle();
        }
    }

    if (TSharedPtr<FStreamableHandle>* Handle = MaterialLoadHandles.Find(Category))
    {
        if (Handle->IsValid())
        {
            (*Handle)->CancelHandle();
        }
    }

    ++MeshRequestSerials.FindOrAdd(Category);
    ++MaterialRequestSerials.FindOrAdd(Category);

    if (USkeletalMeshComponent* GarmentComponent = GetGarmentComponent(Category))
    {
        GarmentComponent->SetLeaderPoseComponent(nullptr, true);
        GarmentComponent->SetAnimInstanceClass(nullptr);
        GarmentComponent->SetSkeletalMesh(nullptr, true);
        GarmentComponent->SetVisibility(false, true);
    }

    EquippedItems.Remove(Category);
    SelectedVariantIndices.Remove(Category);
    MeshLoadHandles.Remove(Category);
    MaterialLoadHandles.Remove(Category);

    RefreshHiddenComponents();
    OnCategoryUnequipped.Broadcast(Category);
    OnWardrobeStateChanged.Broadcast();
}

void UMetaHumanWardrobeComponent::ApplyMaterialVariant(
    const EWardrobeCategory Category,
    const int32 VariantIndex)
{
    UWardrobeItemData* Item = GetEquippedItem(Category);
    if (!IsValid(Item) || !Item->MaterialVariants.IsValidIndex(VariantIndex))
    {
        return;
    }

    SelectedVariantIndices.Add(Category, VariantIndex);
    OnWardrobeStateChanged.Broadcast();

    if (TSharedPtr<FStreamableHandle>* ExistingHandle = MaterialLoadHandles.Find(Category))
    {
        if (ExistingHandle->IsValid())
        {
            (*ExistingHandle)->CancelHandle();
        }
    }

    int32& RequestSerial = MaterialRequestSerials.FindOrAdd(Category);
    ++RequestSerial;

    const FWardrobeMaterialVariant& Variant = Item->MaterialVariants[VariantIndex];
    const FSoftObjectPath MaterialPath = Variant.MaterialOverride.ToSoftObjectPath();

    if (!MaterialPath.IsValid() || Variant.MaterialOverride.IsValid())
    {
        ApplyLoadedMaterialVariant(Category, Item, VariantIndex);
        return;
    }

    const int32 CapturedSerial = RequestSerial;
    const TWeakObjectPtr<UWardrobeItemData> WeakItem(Item);

    FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
    TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
        MaterialPath,
        FStreamableDelegate::CreateUObject(
            this,
            &UMetaHumanWardrobeComponent::HandleMaterialLoaded,
            Category,
            WeakItem,
            VariantIndex,
            CapturedSerial));

    MaterialLoadHandles.Add(Category, Handle);
}

void UMetaHumanWardrobeComponent::HandleMaterialLoaded(
    const EWardrobeCategory Category,
    const TWeakObjectPtr<UWardrobeItemData> ExpectedItem,
    const int32 VariantIndex,
    const int32 RequestSerial)
{
    const int32* CurrentSerial = MaterialRequestSerials.Find(Category);
    if (!CurrentSerial || *CurrentSerial != RequestSerial)
    {
        return;
    }

    UWardrobeItemData* Item = ExpectedItem.Get();
    if (!IsValid(Item) || EquippedItems.FindRef(Category) != Item)
    {
        return;
    }

    if (GetSelectedVariantIndex(Category) != VariantIndex)
    {
        return;
    }

    ApplyLoadedMaterialVariant(Category, Item, VariantIndex);
    MaterialLoadHandles.Remove(Category);
}

void UMetaHumanWardrobeComponent::ApplyLoadedMaterialVariant(
    const EWardrobeCategory Category,
    UWardrobeItemData* Item,
    const int32 VariantIndex)
{
    USkeletalMeshComponent* GarmentComponent = GetGarmentComponent(Category);
    if (!IsValid(GarmentComponent) || !IsValid(Item) || !Item->MaterialVariants.IsValidIndex(VariantIndex))
    {
        return;
    }

    const FWardrobeMaterialVariant& Variant = Item->MaterialVariants[VariantIndex];
    if (Variant.MaterialSlotIndex < 0 || Variant.MaterialSlotIndex >= GarmentComponent->GetNumMaterials())
    {
        UE_LOG(
            LogAvatarWardrobe,
            Warning,
            TEXT("Invalid material slot %d for item '%s'. Mesh has %d slots."),
            Variant.MaterialSlotIndex,
            *Item->GetName(),
            GarmentComponent->GetNumMaterials());
        return;
    }

    UMaterialInterface* BaseMaterialForVariant = Variant.MaterialOverride.Get();

    if (!IsValid(BaseMaterialForVariant))
    {
        if (const USkeletalMesh* SkeletalMeshAsset = GarmentComponent->GetSkeletalMeshAsset())
        {
            const TArray<FSkeletalMaterial>& MeshMaterials = SkeletalMeshAsset->GetMaterials();
            if (MeshMaterials.IsValidIndex(Variant.MaterialSlotIndex))
            {
                BaseMaterialForVariant = MeshMaterials[Variant.MaterialSlotIndex].MaterialInterface;
            }
        }
    }

    if (IsValid(BaseMaterialForVariant))
    {
        GarmentComponent->SetMaterial(Variant.MaterialSlotIndex, BaseMaterialForVariant);
    }

    if (Variant.bApplyTint)
    {
        UMaterialInterface* BaseMaterial = GarmentComponent->GetMaterial(Variant.MaterialSlotIndex);
        if (!IsValid(BaseMaterial))
        {
            return;
        }

        UMaterialInstanceDynamic* DynamicMaterial =
            GarmentComponent->CreateDynamicMaterialInstance(Variant.MaterialSlotIndex, BaseMaterial);

        if (IsValid(DynamicMaterial))
        {
            DynamicMaterial->SetVectorParameterValue(Variant.TintParameterName, Variant.TintColor);
        }
    }
}

UWardrobeItemData* UMetaHumanWardrobeComponent::GetEquippedItem(
    const EWardrobeCategory Category) const
{
    return EquippedItems.FindRef(Category);
}

int32 UMetaHumanWardrobeComponent::GetSelectedVariantIndex(
    const EWardrobeCategory Category) const
{
    if (const int32* FoundIndex = SelectedVariantIndices.Find(Category))
    {
        return *FoundIndex;
    }

    return INDEX_NONE;
}

USkeletalMeshComponent* UMetaHumanWardrobeComponent::GetGarmentComponent(
    const EWardrobeCategory Category) const
{
    return GarmentComponents.FindRef(Category);
}

USkeletalMeshComponent* UMetaHumanWardrobeComponent::GetOrCreateGarmentComponent(
    const EWardrobeCategory Category)
{
    if (USkeletalMeshComponent* Existing = GetGarmentComponent(Category))
    {
        return Existing;
    }

    if (!IsValid(LeaderMeshComponent) && !ResolveLeaderMesh())
    {
        return nullptr;
    }

    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return nullptr;
    }

    const FName ComponentName = MakeGarmentComponentName(Category);
    USkeletalMeshComponent* NewComponent = NewObject<USkeletalMeshComponent>(Owner, ComponentName);
    if (!IsValid(NewComponent))
    {
        return nullptr;
    }

    Owner->AddInstanceComponent(NewComponent);
    NewComponent->SetupAttachment(LeaderMeshComponent);
    NewComponent->SetRelativeTransform(FTransform::Identity);
    NewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewComponent->SetGenerateOverlapEvents(false);
    NewComponent->SetVisibility(false, true);
    NewComponent->RegisterComponent();

    GarmentComponents.Add(Category, NewComponent);
    return NewComponent;
}

USceneComponent* UMetaHumanWardrobeComponent::FindOwnedSceneComponentByName(
    const FName ComponentName) const
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner) || ComponentName.IsNone())
    {
        return nullptr;
    }

    TArray<USceneComponent*> SceneComponents;
    Owner->GetComponents<USceneComponent>(SceneComponents);

    for (USceneComponent* Component : SceneComponents)
    {
        if (IsValid(Component) && Component->GetFName() == ComponentName)
        {
            return Component;
        }
    }

    return nullptr;
}

void UMetaHumanWardrobeComponent::RefreshHiddenComponents()
{
    RestoreManagedVisibility();

    for (const TPair<EWardrobeCategory, TObjectPtr<UWardrobeItemData>>& Pair : EquippedItems)
    {
        const UWardrobeItemData* Item = Pair.Value;
        if (!IsValid(Item))
        {
            continue;
        }

        for (const FName ComponentName : Item->ComponentsToHide)
        {
            USceneComponent* Component = FindOwnedSceneComponentByName(ComponentName);
            if (!IsValid(Component))
            {
                UE_LOG(
                    LogAvatarWardrobe,
                    Warning,
                    TEXT("Component '%s' requested by item '%s' was not found on actor '%s'."),
                    *ComponentName.ToString(),
                    *Item->GetName(),
                    *GetNameSafe(GetOwner()));
                continue;
            }

            const TWeakObjectPtr<USceneComponent> WeakComponent(Component);
            if (!OriginalVisibilityByComponent.Contains(WeakComponent))
            {
                OriginalVisibilityByComponent.Add(WeakComponent, Component->IsVisible());
            }

            Component->SetVisibility(false, true);
        }
    }
}

void UMetaHumanWardrobeComponent::RestoreManagedVisibility()
{
    for (const TPair<TWeakObjectPtr<USceneComponent>, bool>& Pair : OriginalVisibilityByComponent)
    {
        if (USceneComponent* Component = Pair.Key.Get())
        {
            Component->SetVisibility(Pair.Value, true);
        }
    }
}

void UMetaHumanWardrobeComponent::CancelAllLoads()
{
    for (TPair<EWardrobeCategory, TSharedPtr<FStreamableHandle>>& Pair : MeshLoadHandles)
    {
        if (Pair.Value.IsValid())
        {
            Pair.Value->CancelHandle();
        }
    }

    for (TPair<EWardrobeCategory, TSharedPtr<FStreamableHandle>>& Pair : MaterialLoadHandles)
    {
        if (Pair.Value.IsValid())
        {
            Pair.Value->CancelHandle();
        }
    }

    MeshLoadHandles.Empty();
    MaterialLoadHandles.Empty();
}

FName UMetaHumanWardrobeComponent::MakeGarmentComponentName(
    const EWardrobeCategory Category)
{
    switch (Category)
    {
        case EWardrobeCategory::Tops:
            return TEXT("Wardrobe_Tops");
        case EWardrobeCategory::Bottoms:
            return TEXT("Wardrobe_Bottoms");
        case EWardrobeCategory::Shoes:
            return TEXT("Wardrobe_Shoes");
        case EWardrobeCategory::Vests:
            return TEXT("Wardrobe_Vests");
        case EWardrobeCategory::Accessories:
            return TEXT("Wardrobe_Accessories");
        default:
            return TEXT("Wardrobe_Unknown");
    }
}
