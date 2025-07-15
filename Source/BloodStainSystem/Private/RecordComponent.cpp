// Fill out your copyright notice in the Description page of Project Settings.


#include "RecordComponent.h"

#include "BloodStainRecordDataUtils.h"
#include "BloodStainSubsystem.h"
#include "BloodStainSystem.h"
#include "GhostData.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/GameInstance.h"
DECLARE_CYCLE_STAT(TEXT("RecordComp TickComponent"), STAT_RecordComponent_TickComponent, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp Initialize"), STAT_RecordComponent_Initialize, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp CollectMeshComponents"), STAT_RecordComponent_CollectMeshComponents, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp SaveQueuedFrames"), STAT_RecordComponent_SaveQueuedFrames, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp OnComponentAttached"), STAT_RecordComponent_OnComponentAttached, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp OnComponentDetached"), STAT_RecordComponent_OnComponentDetached, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp FillMaterialData"), STAT_RecordComponent_FillMaterialData, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp CreateRecordFromMesh"), STAT_RecordComponent_CreateRecordFromMesh, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp HandleAttachedChanges"), STAT_RecordComponent_HandleAttachedChanges, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("RecordComp HandleAttachedChangesByBit"), STAT_RecordComponent_HandleAttachedChangesByBit, STATGROUP_BloodStain);

URecordComponent::URecordComponent()
	: StartTime(0), MaxRecordFrames(0), CurrentFrameIndex(0)
{
	PrimaryComponentTick.bCanEverTick = true;
}

// Called when the game starts
void URecordComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void URecordComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_TickComponent); 
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 자동 탈부착 기계 빼고 싶으면 빼세요.
	if (RecordOptions.bTrackAttachmentChanges)
	{
		// HandleAttachedActorChanges();
		HandleAttachedActorChangesByBit();
	}
	
	TimeSinceLastRecord += DeltaTime;
	if (TimeSinceLastRecord >= RecordOptions.SamplingInterval)
	{
		FRecordFrame NewFrame;
		NewFrame.FrameIndex = CurrentFrameIndex++;
		NewFrame.TimeStamp = GetWorld()->GetTimeSeconds() - StartTime;

		// 새로 추가된 컴포넌트 중 기록대상에 추가안된 pending list 적용
		if (PendingAddedComponents.Num() > 0)
		{
			NewFrame.AddedComponents = PendingAddedComponents;
			PendingAddedComponents.Empty();
		}
		if (PendingRemovedComponentNames.Num() > 0)
		{
			NewFrame.RemovedComponentNames = PendingRemovedComponentNames;
			PendingRemovedComponentNames.Empty();
		}
		
		// InitialComponentStructure에 있는 컴포넌트들만 월드 트랜스폼을 기록
		for (TObjectPtr<USceneComponent>& SceneComp : RecordComponents)
		{
			FString ComponentName = FString::Printf(TEXT("%s_%u"), *SceneComp->GetName(), SceneComp->GetUniqueID());
			if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(SceneComp))
			{
				FBoneComponentSpace LocalBaseTransforms(SkeletalComp->GetBoneSpaceTransforms());
				NewFrame.SkeletalMeshBoneTransforms.Add(ComponentName, LocalBaseTransforms);
			}
			NewFrame.ComponentTransforms.Add(ComponentName, SceneComp->GetComponentTransform()); // 월드 트랜스폼 저장
		}

		/* If there is no space left, discard oldest frame */
		if (FrameQueuePtr->IsFull())
		{
			FRecordFrame Discard;
			FrameQueuePtr->Dequeue(Discard);
		}
		FrameQueuePtr->Enqueue(NewFrame);
		
		// GhostSaveData.RecordedFrames.Add(Frame);
		TimeSinceLastRecord = 0.0f;
	}
}

void URecordComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// In General, this is Stable
	if (EndPlayReason == EEndPlayReason::Type::Destroyed)
	{
		GetWorld()->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>()->StopRecordComponent(this);
	}
}

void URecordComponent::Initialize(const FName& InGroupName, const FBloodStainRecordOptions& InOptions)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_Initialize); 
	// 1) 전체 구조체 복사
	RecordOptions = InOptions;
	GroupName = InGroupName;
	
	MaxRecordFrames = FMath::CeilToInt(RecordOptions.MaxRecordTime / RecordOptions.SamplingInterval);
	uint32 CapacityPlusOne = FMath::Max<uint32>(MaxRecordFrames + 1, 2);
	FrameQueuePtr = MakeUnique<TCircularQueue<FRecordFrame>>(CapacityPlusOne);
	
	// 2) 타임스탬프 리셋
	// TimeSinceLastRecord = 0.f;
	if (UWorld* World = GetWorld())
	{
		StartTime = World->GetTimeSeconds();
	}
	
	// 현재 액터와 모든 하위 액터에서 메시 컴포넌트를 수집하는 헬퍼 함수
	CollectMeshComponents();
}

void URecordComponent::CollectMeshComponents()
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_CollectMeshComponents);
    if (AActor* Owner = GetOwner())
    {
    	ComponentIntervals.Empty();
        RecordComponents.Empty();

        // 1. Owner 액터 자체의 메시 컴포넌트 수집
        TArray<UMeshComponent*> OwnerMeshComponents;
        Owner->GetComponents<UMeshComponent>(OwnerMeshComponents);

    	// GetComponents의 순서가 보장되는지 불분명함. 기준이 어떤지 정확히 파악하고 수정이 필요할 수 있음.
    	for (UMeshComponent* MeshComp : OwnerMeshComponents)
    	{
    		if (Cast<UCameraProxyMeshComponent>(MeshComp))
    		{
    			continue;
    		}
    		if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(MeshComp))
    		{
    			if (SkeletalMeshComp->GetSkeletalMeshAsset())
    			{
	    			GhostSaveData.ComponentName = FName(FString::Printf(TEXT("%s_%u"), *SkeletalMeshComp->GetName(), SkeletalMeshComp->GetUniqueID()));
    				break;
    			}
    		}
    		else if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(MeshComp))
    		{
    			GhostSaveData.ComponentName = FName(FString::Printf(TEXT("%s_%u"), *StaticMeshComp->GetName(), StaticMeshComp->GetUniqueID()));
    			break;
    		}
    	}
    	
        for (UMeshComponent* MeshComp : OwnerMeshComponents)
        {
            if (Cast<UCameraProxyMeshComponent>(MeshComp))
            {
                continue;
            }

        	if (Cast<USkeletalMeshComponent>(MeshComp) || Cast<UStaticMeshComponent>(MeshComp))
        	{
        		FComponentRecord Record;
        		if (CreateRecordFromMeshComponent(MeshComp, Record))
        		{
        			FComponentInterval Interval = {Record, 0, INT32_MAX};
        			int32 NewIdx = ComponentIntervals.Add(Interval);
        			IntervalIndexMap.Add(Record.ComponentName, NewIdx);
        			RecordComponents.Add(MeshComp);
        		}
        	}
        }

        // 2. Owner 액터에 부착된 모든 액터 (하위의 하위까지 재귀적으로)의 메시 컴포넌트 수집
        TArray<AActor*> AllAttachedActors;
        // 세 번째 인자를 true로 설정하여 재귀적으로 모든 하위 액터를 가져옵니다.
        Owner->GetAttachedActors(AllAttachedActors, true, true);
    	PreviousAttachedActors = AllAttachedActors;
    	
        for (AActor* AttachedActor : AllAttachedActors)
        {
            TArray<UMeshComponent*> AttachedActorMeshComponents;
            AttachedActor->GetComponents<UMeshComponent>(AttachedActorMeshComponents);

            for (UMeshComponent* MeshComp : AttachedActorMeshComponents)
            {
                if (Cast<UCameraProxyMeshComponent>(MeshComp))
                {
                    continue;
                }
            	
				if (Cast<UStaticMeshComponent>(MeshComp) || Cast<USkeletalMeshComponent>(MeshComp))
                {
					FComponentRecord Record;
					if (CreateRecordFromMeshComponent(MeshComp, Record))
					{
						FComponentInterval Interval = {Record, 0, INT32_MAX};
						int32 NewIdx = ComponentIntervals.Add(Interval);
						IntervalIndexMap.Add(Record.ComponentName, NewIdx);
						RecordComponents.Add(MeshComp);
					}
                }
            }
        }
        UE_LOG(LogBloodStain, Warning, TEXT("Collected %d mesh components for %s."), RecordComponents.Num(), *Owner->GetName());
    }
}

bool URecordComponent::SaveQueuedFrames()
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_SaveQueuedFrames);
	return BloodStainRecordDataUtils::CookQueuedFrames(RecordOptions.SamplingInterval, FrameQueuePtr.Get(), GhostSaveData, ComponentIntervals);
}

void URecordComponent::OnComponentAttached(UMeshComponent* NewComponent)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_OnComponentAttached);
	if (!IsValid(NewComponent)) return;
	if (Cast<UCameraProxyMeshComponent>(NewComponent)) return; // 기존 필터링 유지
	
	const FString ComponentName = FString::Printf(TEXT("%s_%u"), *NewComponent->GetName(), NewComponent->GetUniqueID());
	if (IntervalIndexMap.Contains(ComponentName))
	{
		// If it's already registered, do nothing
		return;
	}
	
	// 1. 기록할 컴포넌트 목록에 즉시 추가하여 다음 틱부터 트랜스폼을 기록
	RecordComponents.Add(NewComponent);

	// 2. '추가' 이벤트를 기록하기 위해 ComponentRecord 생성 및 Pending 목록에 추가
	FComponentRecord Record;

	if (CreateRecordFromMeshComponent(NewComponent, Record))
	{
		FComponentInterval I = {Record, CurrentFrameIndex, INT32_MAX};
		int32 NewIdx = ComponentIntervals.Add(I);
		IntervalIndexMap.Add(I.Meta.ComponentName, NewIdx);
		// UE_LOG(LogBloodStain, Log, TEXT("OnComponentAttached: %s added to intervals"), *Record.ComponentName);
	}

	PendingAddedComponents.Add(Record);	
	// UE_LOG(LogBloodStain, Log, TEXT("OnComponentAttached: %s Attached and pending for record"), *Record.ComponentName);
}

void URecordComponent::OnComponentDetached(UMeshComponent* DetachedComponent)
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_OnComponentDetached);
	if (!DetachedComponent)
	{
		return;
	}

	// 1. 기록할 컴포넌트 목록에서 제거하여 더 이상 트랜스폼을 기록하지 않음
	RecordComponents.Remove(DetachedComponent);

	// 2. '제거' 이벤트를 기록하기 위해 고유 이름만 Pending 목록에 추가
	const FString ComponentName = FString::Printf(TEXT("%s_%u"), *DetachedComponent->GetName(), DetachedComponent->GetUniqueID());
	PendingRemovedComponentNames.Add(ComponentName);
	
	// 3. 맵에서 인덱스 찾아서 EndFrame 갱신 (O(log N))
	if (const int32* Idx = IntervalIndexMap.Find(ComponentName))
	{
		ComponentIntervals[*Idx].EndFrame = CurrentFrameIndex;
		IntervalIndexMap.Remove(ComponentName);
	}
	// UE_LOG(LogBloodStain, Log, TEXT("Component Detached and pending for record: %s"), *ComponentName);
	
}

bool URecordComponent::ShouldRecord()
{
	return true;
}

/**
 * UMeshComponent로부터 FComponentRecord를 생성합니다.
 * @param InMeshComponent 정보를 추출할 메시 컴포넌트
 * @param OutRecord 생성된 레코드 정보를 담을 구조체
 * @return 성공적으로 레코드를 생성했으면 true를 반환합니다.
 */
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
				// MID인 경우 -> 부모를 가져와서 경로 저장, 이후 파라미터로 복구
				UMaterialInterface* ParentMaterial = DynamicMaterial->Parent;
				OutRecord.MaterialPaths.Add(ParentMaterial ? ParentMaterial->GetPathName() : TEXT(""));

				// MID 동적 파라미터 저장
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
				//  MID가 아닌 경우 디스크에 저장된 에셋 사용
				OutRecord.MaterialPaths.Add(Material->GetPathName());
			}
		}
		else
		{
			OutRecord.MaterialPaths.Add(TEXT(""));
		}
	}
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

	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogBloodStain, Warning, TEXT("CreateRecordFromMeshComponent: Component %s has no valid mesh asset."), *InMeshComponent->GetName());
		return false;
	}

	if (TSharedPtr<FComponentRecord> CachedRecord = MetaDataCache.FindRef(AssetPath))
	{
		OutRecord = *CachedRecord;
	}
	else
	{
		TSharedPtr<FComponentRecord> NewRecord = MakeShared<FComponentRecord>();
		NewRecord->AssetPath = AssetPath;
		FillMaterialData(InMeshComponent, *NewRecord);
		NewRecord->ComponentClassPath = InMeshComponent->GetClass()->GetPathName();

		MetaDataCache.Add(AssetPath, NewRecord);
		OutRecord = *NewRecord;
	}

	OutRecord.ComponentName = FString::Printf(TEXT("%s_%u"), *InMeshComponent->GetName(), InMeshComponent->GetUniqueID());
	return true;
}

void URecordComponent::HandleAttachedActorChanges()
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_HandleAttachedChanges); 
	// 이전 틱과 Attached된 액터 비교하고 컴포넌트 탈부착
	TArray<AActor*> CurrentAttachedActors;
	AActor* Owner = GetOwner();
	Owner->GetAttachedActors(CurrentAttachedActors, true, true);
	if (PreviousAttachedActors != CurrentAttachedActors)
	{
		TSet<AActor*> PreviousSet(PreviousAttachedActors);
		TSet<AActor*> CurrentSet(CurrentAttachedActors);

		TSet<AActor*> AddedActorsSet = CurrentSet.Difference(PreviousSet);
		TArray<AActor*> AddedActors(AddedActorsSet.Array());

		if (AddedActors.Num() > 0)
		{
			// TODO: 새로 추가된 액터(AddedActors)에 대한 처리 로직
			// 예: 녹화 대상에 추가, 초기 정보 기록 등
			for (AActor* AttachedActor : AddedActors)
			{
				TArray<UMeshComponent*> MeshComps;
				AttachedActor->GetComponents<UMeshComponent>(MeshComps);
				for (UMeshComponent* MeshComp : MeshComps)
				{
					OnComponentAttached(MeshComp);
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("%d a actor(s) have been added."), AddedActors.Num());
		}

		TSet<AActor*> RemovedActorsSet = PreviousSet.Difference(CurrentSet);
		TArray<AActor*> RemovedActors(RemovedActorsSet.Array());

		if (RemovedActors.Num() > 0)
		{
			for (AActor* DetachedActor : RemovedActors)
			{
				TArray<UMeshComponent*> MeshComps;
				DetachedActor->GetComponents<UMeshComponent>(MeshComps);
				for (UMeshComponent* MeshComp : MeshComps)
				{
					OnComponentDetached(MeshComp);
				}
			}
			// TODO: 사라진 액터(RemovedActors)에 대한 처리 로직
			// 예: 녹화 대상에서 제거, 소멸 정보 기록 등
			UE_LOG(LogTemp, Warning, TEXT("%d a actor(s) have been removed."), RemovedActors.Num());
		}

		PreviousAttachedActors = CurrentAttachedActors;
	}
}

void URecordComponent::HandleAttachedActorChangesByBit()
{
	SCOPE_CYCLE_COUNTER(STAT_RecordComponent_HandleAttachedChangesByBit); 
	TArray<AActor*> CurActors;
	if (AActor* Owner = GetOwner())
	{
		Owner->GetAttachedActors(CurActors, true, true);
	}

	auto EnsureMapping = [&](AActor* Actor) {
		if (!AttachedActorIndexMap.Contains(Actor))
		{
			int32 NewIndex = AttachedIndexToActor.Add(Actor);
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

	TBitArray<> Diff = TBitArray<>::BitwiseXOR(CurAttachedBits, PrevAttachedBits, EBitwiseOperatorFlags::MaxSize);
	TBitArray<> Added = TBitArray<>::BitwiseAND(Diff, CurAttachedBits, EBitwiseOperatorFlags::MaxSize);
	TBitArray<> Removed = TBitArray<>::BitwiseAND(Diff, PrevAttachedBits, EBitwiseOperatorFlags::MaxSize);


	for (int32 Bit = Added.FindFrom(true, 0); Bit != INDEX_NONE; Bit = Added.FindFrom(true, Bit + 1))
	{
		AActor* NewActor = AttachedIndexToActor[Bit];
		TArray<UMeshComponent*> MeshComps;
		NewActor->GetComponents<UMeshComponent>(MeshComps);
		for (UMeshComponent* MeshComp : MeshComps)
		{
			OnComponentAttached(MeshComp);
		}
	}

	for (int32 Bit = Removed.FindFrom(true, 0); Bit != INDEX_NONE; Bit = Removed.FindFrom(true, Bit + 1))
	{
		AActor* GoneActor = AttachedIndexToActor[Bit];
		TArray<UMeshComponent*> MeshComps;
		GoneActor->GetComponents<UMeshComponent>(MeshComps);
		for (UMeshComponent* MeshComp : MeshComps)
		{
			OnComponentDetached(MeshComp);
		}
	}
	
	PrevAttachedBits = CurAttachedBits;
	PreviousAttachedActors = MoveTemp(CurActors);
}

