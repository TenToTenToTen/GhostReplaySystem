/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "RecordComponent.h"
#include "BloodStainRecordDataUtils.h"
#include "BloodStainSubsystem.h"
#include "BloodStainSystem.h"
#include "GhostData.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "StructUtils/InstancedStruct.h"
#include "GroomComponent.h"
#include "GroomAsset.h"

DECLARE_CYCLE_STAT(TEXT("RecordComp TickComponent"), STAT_RecordComponent_TickComponent, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp Initialize"), STAT_RecordComponent_Initialize, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp CollectMeshComponents"), STAT_RecordComponent_CollectMeshComponents, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp SaveQueuedFrames"), STAT_RecordComponent_CookQueuedFrames, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp OnComponentAttached"), STAT_RecordComponent_OnComponentAttached, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp OnComponentDetached"), STAT_RecordComponent_OnComponentDetached, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp FillMaterialData"), STAT_RecordComponent_FillMaterialData, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp CreateRecordFromMesh"), STAT_RecordComponent_CreateRecordFromMesh, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp HandleAttachedChanges"), STAT_RecordComponent_HandleAttachedChanges, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp HandleAttachedChangesByBit"), STAT_RecordComponent_HandleAttachedChangesByBit, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp HandleMeshComponentChangesByBit"), STAT_RecordComponent_HandleMeshComponentChangesByBit, STATGROUP_BloodStain);

URecordComponent::URecordComponent()
	: StartTime(0), MaxRecordFrames(0), CurrentFrameIndex(0), TimeSinceLastRecord(0)
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URecordComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_TickComponent);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	TimeSinceLastRecord += DeltaTime;
	if (TimeSinceLastRecord >= RecordOptions.SamplingInterval)
	{
		if (RecordOptions.bTrackAttachmentChanges)
		{
			HandleMeshComponentChangesByBit();
		}
		
		TimeSinceLastRecord -= RecordOptions.SamplingInterval;

		FRecordFrame NewFrame;
		NewFrame.FrameIndex = CurrentFrameIndex++;
		NewFrame.TimeStamp = GetWorld()->GetTimeSeconds() - StartTime;

		// Record All Owned Component Transform (support for StaticMeshComponent, SkeletalMeshComponent)
		for (TObjectPtr<UMeshComponent>& MeshComp : OwnedComponentsForRecord)
		{
			FString ComponentName = CreateUniqueComponentName(MeshComp);

			if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(MeshComp))
			{
				if (SkeletalComp->IsSimulatingPhysics())
				{
					const USkeletalMesh* SkeletalMesh = SkeletalComp->GetSkeletalMeshAsset();
					const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
					const int32 NumBones = SkeletalComp->GetNumBones();

					TArray<FTransform> BoneWorldTransforms;
					BoneWorldTransforms.SetNum(NumBones);
					for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
					{
						BoneWorldTransforms[BoneIndex] = SkeletalComp->GetBoneTransform(BoneIndex); // World space
					}
					TArray<FTransform> BoneLocalTransforms;
					BoneLocalTransforms.SetNum(NumBones);
					const FTransform WorldToComponent = SkeletalComp->GetComponentTransform().Inverse();

					for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
					{
						int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
						if (ParentIndex != INDEX_NONE)
						{
							BoneLocalTransforms[BoneIndex] = BoneWorldTransforms[BoneIndex].GetRelativeTransform(BoneWorldTransforms[ParentIndex]);
						}
						else
						{
							// Root bone: relative to component
							BoneLocalTransforms[BoneIndex] = BoneWorldTransforms[BoneIndex] * WorldToComponent;
						}
					}
					FBoneComponentSpace LocalBoneData(BoneLocalTransforms);
					NewFrame.SkeletalMeshBoneTransforms.Add(ComponentName, LocalBoneData);
				}
				else
				{
				FBoneComponentSpace LocalBaseTransforms(SkeletalComp->GetBoneSpaceTransforms());
				NewFrame.SkeletalMeshBoneTransforms.Add(ComponentName, LocalBaseTransforms);
				}
			}
			NewFrame.ComponentTransforms.Add(ComponentName, MeshComp->GetComponentTransform());
		}

		/* If there is no space left, discard the oldest frame */
		if (FrameQueuePtr->IsFull())
		{
			FRecordFrame Discard;
			FrameQueuePtr->Dequeue(Discard);
		}
		FrameQueuePtr->Enqueue(NewFrame);
	}
}

void URecordComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// In General, this is Stable
	if (EndPlayReason == EEndPlayReason::Type::Destroyed)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UBloodStainSubsystem* BloodStainSubsystem = GameInstance->GetSubsystem<UBloodStainSubsystem>())
				{
					BloodStainSubsystem->StopRecordComponent(this);
				}
			}
		}
	}
}

void URecordComponent::Initialize(const FBloodStainRecordOptions& InOptions, const float& InGroupStartTime)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_Initialize);
	
	RecordOptions = InOptions;
	
	MaxRecordFrames = FMath::CeilToInt(RecordOptions.MaxRecordTime / RecordOptions.SamplingInterval);
	uint32 CapacityPlusOne = FMath::Max<uint32>(MaxRecordFrames + 1, 2);
	FrameQueuePtr = MakeUnique<TCircularQueue<FRecordFrame>>(CapacityPlusOne);

	StartTime = InGroupStartTime;
	
	CollectOwnedMeshComponents();
}

FRecordActorSaveData URecordComponent::CookQueuedFrames(const float& BaseTime)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_CookQueuedFrames);

	FRecordActorSaveData Result = FRecordActorSaveData();
	Result.PrimaryComponentName = PrimaryComponentName;
	BloodStainRecordDataUtils::CookQueuedFrames(RecordOptions.SamplingInterval, BaseTime, FrameQueuePtr.Get(), Result, ComponentActiveIntervals);

	return Result;
}

void URecordComponent::OnComponentAttached(UMeshComponent* NewComponent)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_OnComponentAttached);

	if (!IsValid(NewComponent))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[On Component Attached] Component is Not Valid"));
		return;
	}

	if (!IsComponentSupported(NewComponent))
	{
		return;
	}
	
	const FString ComponentName = CreateUniqueComponentName(NewComponent);
	if (IntervalIndexMap.Contains(ComponentName))
	{
		// If it's already registered, do nothing
		UE_LOG(LogBloodStain, Warning, TEXT("[OnComponentAttached] Component %s is already registered"), *ComponentName);
		return;
	}
	
	OwnedComponentsForRecord.Add(NewComponent);

	FComponentRecord Record;

	if (CreateRecordFromMeshComponent(NewComponent, Record))
	{
		FComponentActiveInterval I = FComponentActiveInterval(Record, CurrentFrameIndex, INT32_MAX);
		int32 NewIdx = ComponentActiveIntervals.Add(I);
		IntervalIndexMap.Add(I.Meta.ComponentName, NewIdx);
	}
	
	UE_LOG(LogBloodStain, Warning, TEXT("[OnComponentAttached] Component %s Attached"), *ComponentName);
}

void URecordComponent::OnComponentDetached(UMeshComponent* DetachedComponent)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_OnComponentDetached);
	if (!DetachedComponent)
	{
		return;
	}

	const FString ComponentName = CreateUniqueComponentName(DetachedComponent);
	
	if (!OwnedComponentsForRecord.Contains(DetachedComponent))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[OnComponentDetached] Component is not Attached %s"), *ComponentName);
		return;
	}

	OwnedComponentsForRecord.Remove(DetachedComponent);
	
	if (const int32* Idx = IntervalIndexMap.Find(ComponentName))
	{
		ComponentActiveIntervals[*Idx].EndFrame = CurrentFrameIndex - 1;
		IntervalIndexMap.Remove(ComponentName);
	}

	UE_LOG(LogBloodStain, Warning, TEXT("[OnComponentDetached] Component %s Detached"), *ComponentName);
}

void URecordComponent::FillMaterialData(const UMeshComponent* InMeshComponent, FComponentRecord& OutRecord)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_FillMaterialData);
	TArray<UMaterialInterface*> Materials;
	InMeshComponent->GetUsedMaterials(Materials);
	for (int32 MatIndex = 0; MatIndex < Materials.Num(); ++MatIndex)
	{
		if (UMaterialInterface* Material = Materials[MatIndex])
		{
			if (UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material))
			{
				UMaterialInterface* ParentMaterial = DynamicMaterial->Parent;
				OutRecord.MaterialPaths.Add(ParentMaterial ? ParentMaterial->GetPathName() : TEXT(""));

				FMaterialParameters MatParams;

				TArray<FMaterialParameterInfo> VectorParamInfos;
				TArray<FGuid> VectorParamGuids;
				DynamicMaterial->GetAllVectorParameterInfo(VectorParamInfos, VectorParamGuids);
				for (const FMaterialParameterInfo& ParamInfo : VectorParamInfos)
				{
					FLinearColor Value;
					if (DynamicMaterial->GetVectorParameterValue(ParamInfo, Value))
					{
						MatParams.VectorParams.Add(ParamInfo.Name, Value);
					}
				}

				TArray<FMaterialParameterInfo> ScalarParamInfos;
				TArray<FGuid> ScalarParamGuids;
				DynamicMaterial->GetAllScalarParameterInfo(ScalarParamInfos, ScalarParamGuids);
				for (const FMaterialParameterInfo& ParamInfo : ScalarParamInfos)
				{
					float Value;
					if (DynamicMaterial->GetScalarParameterValue(ParamInfo, Value))
					{
						MatParams.ScalarParams.Add(ParamInfo.Name, Value);
					}
				}

				if (MatParams.VectorParams.Num() > 0 || MatParams.ScalarParams.Num() > 0)
				{
					OutRecord.MaterialParameters.Add(MatIndex, MatParams);
				}
			}
			else
			{
				//  Use the asset from a disk if it's not MID
				OutRecord.MaterialPaths.Add(Material->GetPathName());
			}
		}
		else
		{
			OutRecord.MaterialPaths.Add(TEXT(""));
		}
	}
}

void URecordComponent::SetRecordActorUserData(const FInstancedStruct& InInstancedStruct)
{
	InstancedStruct = InInstancedStruct;
}

FInstancedStruct URecordComponent::GetRecordActorUserData()
{
	return InstancedStruct;
}

void URecordComponent::CollectOwnedMeshComponents()
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_CollectMeshComponents);
	
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	
    ComponentActiveIntervals.Empty();
    OwnedComponentsForRecord.Empty();
    IntervalIndexMap.Empty();

	TArray<AActor*> ActorsToProcess;
	ActorsToProcess.Add(Owner);
	Owner->GetAttachedActors(ActorsToProcess, false, true);

	for (AActor* CurrentActor: ActorsToProcess)
	{
		TArray<UMeshComponent*> MeshComponents;
		CurrentActor->GetComponents<UMeshComponent>(MeshComponents);

		for (UMeshComponent* MeshComp : MeshComponents)
		{
			if (!IsComponentSupported(MeshComp))
			{
				continue;
			}
			AddComponentToRecordList(MeshComp);
		}
	}

    // TODO - to clear out the exact order of GetComponents()
	if (!OwnedComponentsForRecord.IsEmpty())
	{
		const UMeshComponent* PrimaryComp = OwnedComponentsForRecord[0].Get();
		if (PrimaryComp != nullptr)
		{
			PrimaryComponentName = FName(CreateUniqueComponentName(PrimaryComp));
		}
	}
	
	UE_LOG(LogBloodStain, Log, TEXT("Collected %d mesh components for %s and its attachments."), OwnedComponentsForRecord.Num(), *Owner->GetName());
}

bool URecordComponent::CreateRecordFromMeshComponent(UMeshComponent* InMeshComponent, FComponentRecord& OutRecord)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_CreateRecordFromMesh);
	if (!InMeshComponent || !IsValid(InMeshComponent))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("CreateRecordFromMeshComponent: Invalid or null mesh component provided."));
		return false;
	}

	FString AssetPath;
	if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(InMeshComponent))
	{
		if (UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh())
		{
			AssetPath = StaticMesh->GetPathName();
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(InMeshComponent))
	{
		if (USkeletalMesh* SkeletalMesh = SkeletalMeshComp->GetSkeletalMeshAsset())
		{
			AssetPath = SkeletalMesh->GetPathName();
		}
	}
	else if (UGroomComponent* GroomComp = Cast<UGroomComponent>(InMeshComponent))
	{
		if (GroomComp->GroomAsset)
		{
			if (USkeletalMeshComponent* ParentSkeletal = Cast<USkeletalMeshComponent>(GroomComp->GetAttachParent()))
			{
				AssetPath = GroomComp->GroomAsset->GetPathName();
			}
		}
	}

	

	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogBloodStain, Warning, TEXT("CreateRecordFromMeshComponent: Component %s has no valid mesh asset."), *InMeshComponent->GetName());
		return false;
	}

	if (TSharedPtr<FComponentRecord> CachedRecord = MetaDataCache.FindRef(CreateUniqueComponentName(InMeshComponent)))
	{
		OutRecord = *CachedRecord;
	}
	else
	{
		TSharedPtr<FComponentRecord> NewRecord = MakeShared<FComponentRecord>();
		NewRecord->ComponentName = CreateUniqueComponentName(InMeshComponent);
		NewRecord->ComponentClassPath = InMeshComponent->GetClass()->GetPathName();
		NewRecord->AssetPath = AssetPath;
		FillMaterialData(InMeshComponent, *NewRecord);

		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InMeshComponent))
		{
			if (SkeletalMeshComponent->LeaderPoseComponent.Get() != nullptr)
			{
				const FString ComponentName = CreateUniqueComponentName(SkeletalMeshComponent->LeaderPoseComponent.Get());
				NewRecord->LeaderPoseComponentName = ComponentName;
			}
		}
		
		MetaDataCache.Add(OutRecord.ComponentName, NewRecord);
		OutRecord = *NewRecord;
	}
	return true;
}

void URecordComponent::HandleAttachedActorChangesByBit()
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_HandleAttachedChangesByBit); 
	TArray<AActor*> CurActors;
	if (AActor* Owner = GetOwner())
	{
		CurActors.Add(Owner);
		Owner->GetAttachedActors(CurActors, false, true);
	}

	auto EnsureMapping = [&](AActor* Actor) {
		if (!AttachedActorIndexMap.Contains(Actor))
		{
			const int32 NewIndex = AttachedIndexToActor.Add(Actor);
			AttachedActorIndexMap.Add(Actor, NewIndex);
			PrevAttachedBits.Add(false);
			CurAttachedBits.Add(false);
		}
	};

	for (AActor* Actor : CurActors)
	{
		EnsureMapping(Actor);
	}

	CurAttachedBits.Init(false, AttachedIndexToActor.Num());
	for (AActor* Actor : CurActors)
	{
		CurAttachedBits[AttachedActorIndexMap[Actor]] = true;
	}

	const TBitArray<> Diff = TBitArray<>::BitwiseXOR(CurAttachedBits, PrevAttachedBits, EBitwiseOperatorFlags::MaxSize);
	const TBitArray<> Added = TBitArray<>::BitwiseAND(Diff, CurAttachedBits, EBitwiseOperatorFlags::MaxSize);
	const TBitArray<> Removed = TBitArray<>::BitwiseAND(Diff, PrevAttachedBits, EBitwiseOperatorFlags::MaxSize);


	for (int32 Bit = Added.FindFrom(true, 0); Bit != INDEX_NONE; Bit = Added.FindFrom(true, Bit + 1))
	{
		const AActor* NewActor = AttachedIndexToActor[Bit];
		TArray<UMeshComponent*> MeshComps;
		NewActor->GetComponents<UMeshComponent>(MeshComps);
		for (UMeshComponent* MeshComp : MeshComps)
		{
			OnComponentAttached(MeshComp);
		}
	}

	for (int32 Bit = Removed.FindFrom(true, 0); Bit != INDEX_NONE; Bit = Removed.FindFrom(true, Bit + 1))
	{
		const AActor* GoneActor = AttachedIndexToActor[Bit];
		TArray<UMeshComponent*> MeshComps;
		GoneActor->GetComponents<UMeshComponent>(MeshComps);
		for (UMeshComponent* MeshComp : MeshComps)
		{
			OnComponentDetached(MeshComp);
		}
	}
	
	PrevAttachedBits = CurAttachedBits;
}

void URecordComponent::HandleMeshComponentChangesByBit()
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_HandleAttachedChangesByBit);

	TArray<UMeshComponent*> CurMeshComponents;
    if (AActor* Owner = GetOwner())
    {
        TArray<AActor*> ActorsToProcess;
        ActorsToProcess.Add(Owner);
        Owner->GetAttachedActors(ActorsToProcess, false, true);

        for (AActor* Actor : ActorsToProcess)
        {
            TArray<UMeshComponent*> MeshComps;
            Actor->GetComponents<UMeshComponent>(MeshComps);
            for (UMeshComponent* MeshComp : MeshComps)
            {
                if (MeshComp->IsVisible())
                {
                    if (IsComponentSupported(MeshComp))
                    {
                        CurMeshComponents.Add(MeshComp);
                    }
                }
            }
        }
    }

    auto EnsureMapping = [&](UMeshComponent* Component) {
        if (!AttachedComponentIndexMap.Contains(Component))
        {
            const int32 NewIndex = IndexToAttachedComponent.Add(Component);
            AttachedComponentIndexMap.Add(Component, NewIndex);
            PrevComponentBits.Add(false);
            CurComponentBits.Add(false);
        }
    };

    for (UMeshComponent* Component : CurMeshComponents)
    {
        EnsureMapping(Component);
    }
    
    CurComponentBits.Init(false, IndexToAttachedComponent.Num());
    for (UMeshComponent* Component : CurMeshComponents)
    {
        if (const int32* IndexPtr = AttachedComponentIndexMap.Find(Component))
        {
            CurComponentBits[*IndexPtr] = true;
        }
    }

    const TBitArray<> Diff = TBitArray<>::BitwiseXOR(CurComponentBits, PrevComponentBits, EBitwiseOperatorFlags::MaxSize);
    const TBitArray<> Added = TBitArray<>::BitwiseAND(Diff, CurComponentBits, EBitwiseOperatorFlags::MaxSize);
    const TBitArray<> Removed = TBitArray<>::BitwiseAND(Diff, PrevComponentBits, EBitwiseOperatorFlags::MaxSize);

    for (TBitArray<>::FConstIterator It(Added); It; ++It)
    {
        if (It.GetValue())
        {
            if (IndexToAttachedComponent.IsValidIndex(It.GetIndex()))
            {
                if (UMeshComponent* NewComponent = IndexToAttachedComponent[It.GetIndex()])
                {
                    OnComponentAttached(NewComponent);
                }
            }
        }
    }

    for (TBitArray<>::FConstIterator It(Removed); It; ++It)
    {
        if (It.GetValue())
        {
            if (IndexToAttachedComponent.IsValidIndex(It.GetIndex()))
            {
                if (UMeshComponent* GoneComponent = IndexToAttachedComponent[It.GetIndex()])
                {
                    OnComponentDetached(GoneComponent);
                }
            }
        }
    }
    PrevComponentBits = CurComponentBits;
}

bool URecordComponent::AddComponentToRecordList(UMeshComponent* MeshComp)
{
	if (!MeshComp->IsVisible())
	{
		return false;
	}
	
	FComponentRecord Record;
	if (CreateRecordFromMeshComponent(MeshComp, Record))
	{
		FComponentActiveInterval Interval = FComponentActiveInterval(Record, 0, INT32_MAX);
		const int32 NewIdx = ComponentActiveIntervals.Add(Interval);
		IntervalIndexMap.Add(Record.ComponentName, NewIdx);
		OwnedComponentsForRecord.Add(MeshComp);
		return true;
	}
	return false;
}

bool URecordComponent::IsComponentSupported(UMeshComponent* MeshComp) const
{
	const UClass* ComponentClass = MeshComp->GetClass();

	const bool bIsSupported =
		ComponentClass == UStaticMeshComponent::StaticClass() ||
		ComponentClass == USkeletalMeshComponent::StaticClass() ||
		ComponentClass == UGroomComponent::StaticClass();

	if (!bIsSupported)
	{
		return false;
	}

	if (RecordOptions.RequiredTag != NAME_None && !MeshComp->ComponentHasTag(RecordOptions.RequiredTag))
	{
		return false;
	}
	if (RecordOptions.ExcludedTag != NAME_None && MeshComp->ComponentHasTag(RecordOptions.ExcludedTag))
	{
		return false;
	}
	return true;
}

FString URecordComponent::CreateUniqueComponentName(const UActorComponent* Component)
{
	FString ComponentName = FString::Printf(TEXT("%s_%u"), *Component->GetName(), Component->GetUniqueID());
	return ComponentName;
}
