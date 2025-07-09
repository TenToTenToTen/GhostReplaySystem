// Fill out your copyright notice in the Description page of Project Settings.


#include "RecordComponent.h"

#include "BloodStainSystem.h"
#include "GhostData.h"
#include "Camera/CameraComponent.h"

//DECLARE_STATS_GROUP(TEXT("BloodStain"), STATGROUP_BloodStain, STATCAT_Advanced);
//DECLARE_CYCLE_STAT(TEXT("RecordTickComponent"), STAT_RecordCompTick, STATGROUP_BloodStain);
//DECLARE_CYCLE_STAT(TEXT("SaveQueueFrames"), STAT_FrameQueueSave, STATGROUP_BloodStain);

URecordComponent::URecordComponent()
	: CurrentFrameIndex(0)
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
	//SCOPE_CYCLE_COUNTER(STAT_RecordCompTick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
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
		for (USceneComponent*& SceneComp : RecordComponents)
		{
			FString ComponentName = FString::Printf(TEXT("%s_%u"), *SceneComp->GetName(), SceneComp->GetUniqueID());
			if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(SceneComp))
			{
				FBoneComponentSpace ComponentBaseTransforms(SkelComp->GetComponentSpaceTransforms());
				NewFrame.SkeletalMeshBoneTransforms.Add(ComponentName, ComponentBaseTransforms);
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

void URecordComponent::Initialize(const FBloodStainRecordOptions& InOptions)
{
	// 1) 전체 구조체 복사
	RecordOptions = InOptions;
	MaxRecordFrames = FMath::CeilToInt(RecordOptions.MaxRecordTime / RecordOptions.SamplingInterval);
	uint32 CapacityPlusOne = FMath::Max<uint32>(MaxRecordFrames + 1, 2);
	FrameQueuePtr = MakeUnique<TCircularQueue<FRecordFrame>>(CapacityPlusOne);
	
	// 2) 타임스탬프 리셋
	// TimeSinceLastRecord = 0.f;
	if (UWorld* World = GetWorld())
	{
		StartTime = GetWorld()->GetTimeSeconds();
	}
	
	GhostSaveData.InitialComponentStructure.Empty(); // 초기화
	GhostSaveData.RecordOptions = InOptions;
	// 현재 액터와 모든 하위 액터에서 메시 컴포넌트를 수집하는 헬퍼 함수
	CollectMeshComponents();

	ComponentIntervals.Empty();
	for (const FComponentRecord& Rec : GhostSaveData.InitialComponentStructure)
	{
		FComponentInterval I = {Rec, 0, INT32_MAX};
		int32 NewIdx = ComponentIntervals.Add(I);
		IntervalIndexMap.Add(Rec.ComponentName, NewIdx);
	}
}

void URecordComponent::CollectMeshComponents()
{
    if (AActor* Owner = GetOwner())
    {
        GhostSaveData.InitialComponentStructure.Empty();
        RecordComponents.Empty();

        // 1. Owner 액터 자체의 메시 컴포넌트 수집
        TArray<UMeshComponent*> OwnerMeshComponents;
        Owner->GetComponents<UMeshComponent>(OwnerMeshComponents, true);

    	// GetComponents의 순서가 보장되는지 불분명함. 기준이 어떤지 정확히 파악하고 수정이 필요할 수 있음.
    	for (UMeshComponent* MeshComp : OwnerMeshComponents)
    	{
    		if (Cast<USkeletalMeshComponent>(MeshComp) || Cast<UStaticMeshComponent>(MeshComp))
    		{
    			GhostSaveData.SpawnPointComponentName = FString::Printf(TEXT("%s_%u"), *MeshComp->GetName(), MeshComp->GetUniqueID());
    			break;
    		}
    	}
        for (UMeshComponent* MeshComp : OwnerMeshComponents)
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
		            GhostSaveData.InitialComponentStructure.Add(Record);
		            RecordComponents.Add(MeshComp);
            	}
            }
        }

        // 2. Owner 액터에 부착된 모든 액터 (하위의 하위까지 재귀적으로)의 메시 컴포넌트 수집
        TArray<AActor*> AllAttachedActors;
        // 세 번째 인자를 true로 설정하여 재귀적으로 모든 하위 액터를 가져옵니다.
        Owner->GetAttachedActors(AllAttachedActors, true, true); 

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
						GhostSaveData.InitialComponentStructure.Add(Record);
						RecordComponents.Add(MeshComp);
					}
                }
            }
        }
        UE_LOG(LogTemp, Warning, TEXT("Collected %d mesh components for %s."), GhostSaveData.InitialComponentStructure.Num(), *Owner->GetName());
    }
}

bool URecordComponent::SaveQueuedFrames()
{
    FRecordFrame First;
    if (!FrameQueuePtr->Peek(First))
    {
        UE_LOG(LogBloodStain, Warning, TEXT("No frames to save in RecordComponent for %s"), *GetOwner()->GetName());
        return false;
    }

	const int32 FirstIndex = First.FrameIndex;
    const float BaseTime = First.TimeStamp;

    // 1. 원본 프레임들 복사 + 시간 정규화
    TArray<FRecordFrame> RawFrames;
    FRecordFrame Tmp;
    while (FrameQueuePtr->Dequeue(Tmp))
    {
        Tmp.TimeStamp -= BaseTime;
        RawFrames.Add(MoveTemp(Tmp));
    }

    if (RawFrames.Num() < 2)
    {
        UE_LOG(LogBloodStain, Warning, TEXT("Not enough raw frames to interpolate."));
        return false;
    }

    // 2. 보간 프레임 설정
    const float FrameInterval = RecordOptions.SamplingInterval;
    const int32 NumInterpFrames = FMath::FloorToInt(RawFrames.Last().TimeStamp / FrameInterval);

    GhostSaveData.RecordedFrames.Empty(NumInterpFrames + 1);

    for (int32 i = 0; i <= NumInterpFrames; ++i)
    {
        float TargetTime = i * FrameInterval;

        // 3. 보간을 위한 A, B 프레임 찾기
        int32 PrevIndex = 0;
        while (PrevIndex + 1 < RawFrames.Num() && RawFrames[PrevIndex + 1].TimeStamp < TargetTime)
        {
            ++PrevIndex;
        }

        const FRecordFrame& A = RawFrames[PrevIndex];
        const FRecordFrame& B = RawFrames[PrevIndex + 1];
        float Alpha = (TargetTime - A.TimeStamp) / (B.TimeStamp - A.TimeStamp);

        FRecordFrame NewFrame;
        NewFrame.TimeStamp = TargetTime;
    	NewFrame.AddedComponents = A.AddedComponents;
    	NewFrame.RemovedComponentNames = A.RemovedComponentNames;

        // 4. ComponentTransform 보간
        for (const auto& Pair : A.ComponentTransforms)
        {
            const FString& Name = Pair.Key;
            const FTransform& TA = Pair.Value;
            const FTransform* TBPtr = B.ComponentTransforms.Find(Name);
            if (!TBPtr) continue;

            const FTransform& TB = *TBPtr;
            FTransform Interp = FTransform(
                FQuat::Slerp(TA.GetRotation(), TB.GetRotation(), Alpha),
                FMath::Lerp(TA.GetLocation(), TB.GetLocation(), Alpha),
                FMath::Lerp(TA.GetScale3D(), TB.GetScale3D(), Alpha)
            );
            NewFrame.ComponentTransforms.Add(Name, Interp);
        }

        // 5. BoneTransform 보간
        for (const auto& BonePairA : A.SkeletalMeshBoneTransforms)
        {
            const FString& CompName = BonePairA.Key;
            const FBoneComponentSpace& BoneA = BonePairA.Value;

            if (const FBoneComponentSpace* BoneB = B.SkeletalMeshBoneTransforms.Find(CompName))
            {
                const int32 BoneCount = FMath::Min(BoneA.BoneTransforms.Num(), BoneB->BoneTransforms.Num());
                FBoneComponentSpace InterpBone;
                InterpBone.BoneTransforms.SetNum(BoneCount);

                for (int32 j = 0; j < BoneCount; ++j)
                {
                    const FTransform& TA = BoneA.BoneTransforms[j];
                    const FTransform& TB = BoneB->BoneTransforms[j];
                    InterpBone.BoneTransforms[j] = FTransform(
                        FQuat::Slerp(TA.GetRotation(), TB.GetRotation(), Alpha),
                        FMath::Lerp(TA.GetLocation(), TB.GetLocation(), Alpha),
                        FMath::Lerp(TA.GetScale3D(), TB.GetScale3D(), Alpha)
                    );
                }

                NewFrame.SkeletalMeshBoneTransforms.Add(CompName, InterpBone);
            }
        }

        GhostSaveData.RecordedFrames.Add(MoveTemp(NewFrame));
    }

	/* Construct Initial Component Structure based on Total Component event Data */
	BuildInitialComponentStructure(FirstIndex, GhostSaveData.RecordedFrames.Num());
	
    return true;
}


void URecordComponent::BuildInitialComponentStructure(int32 FirstFrameIndex, int32 NumSavedFrames)
{
	ComponentIntervals.Sort([](auto& A, auto& B) {
		return A.EndFrame < B.EndFrame;
	});

	// StartIdx : first index where EndFrame > FirstFrameIndex 
	int32 StartIdx = Algo::UpperBoundBy(ComponentIntervals, FirstFrameIndex, &FComponentInterval::EndFrame);
	// Sorted[0..StartIdx-1] 은 FirstFrameIndex 이전에 이미 탈착된 구간

	// 3) 그 이후 구간만 순회하며 StartFrame ≤ FirstFrameIndex 필터
	GhostSaveData.InitialComponentStructure.Empty();
	for (int32 i = StartIdx; i < ComponentIntervals.Num(); ++i)
	{
		FComponentInterval& I = ComponentIntervals[i];
		// if (I.StartFrame <= FirstFrameIndex)
		// [StartFrame, EndFrame] 구간을 [0, NumSavedFrames) 구간으로 변환
		{
			GhostSaveData.InitialComponentStructure.Add(I.Meta);
			I.StartFrame = FMath::Max(0, I.StartFrame - FirstFrameIndex);
			if (I.EndFrame == INT32_MAX)
			{
				I.EndFrame = NumSavedFrames;
			}
			else
			{
				I.EndFrame = FMath::Min(I.EndFrame - FirstFrameIndex, NumSavedFrames);
			}
			
			GhostSaveData.ComponentIntervals.Add(I);
			UE_LOG(LogBloodStain, Log, TEXT("BuildInitialComponentStructure: %s added to initial structure"), *I.Meta.ComponentName);
		}
	}
}

void URecordComponent::OnComponentAttached(UMeshComponent* NewComponent)
{
	if (!NewComponent || !IsValid(NewComponent)) return;
	if (Cast<UCameraProxyMeshComponent>(NewComponent)) return; // 기존 필터링 유지

	// 1. 기록할 컴포넌트 목록에 즉시 추가하여 다음 틱부터 트랜스폼을 기록
	RecordComponents.AddUnique(NewComponent);

	// 2. '추가' 이벤트를 기록하기 위해 ComponentRecord 생성 및 Pending 목록에 추가
	FComponentRecord Record;

	if (CreateRecordFromMeshComponent(NewComponent, Record))
	{
		FComponentInterval I = {Record, CurrentFrameIndex, INT32_MAX};
		if (!ComponentIntervals.Contains(I))
		{
			int32 NewIdx = ComponentIntervals.Add(I);
			IntervalIndexMap.Add(I.Meta.ComponentName, NewIdx);
			UE_LOG(LogBloodStain, Log, TEXT("OnComponentAttached: %s added to intervals"), *Record.ComponentName);
		}		
	}

	PendingAddedComponents.Add(Record);	
	UE_LOG(LogBloodStain, Log, TEXT("OnComponentAttached: %s Attached and pending for record"), *Record.ComponentName);
}

void URecordComponent::OnComponentDetached(UMeshComponent* DetachedComponent)
{
	if (!DetachedComponent || !IsValid(DetachedComponent)) return;

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
	
	UE_LOG(LogBloodStain, Log, TEXT("Component Detached and pending for record: %s"), *ComponentName);
}

/**
 * UMeshComponent로부터 FComponentRecord를 생성합니다.
 * @param InMeshComponent 정보를 추출할 메시 컴포넌트
 * @param OutRecord 생성된 레코드 정보를 담을 구조체
 * @return 성공적으로 레코드를 생성했으면 true를 반환합니다.
 */
bool URecordComponent::CreateRecordFromMeshComponent(UMeshComponent* InMeshComponent, FComponentRecord& OutRecord)
{
	if (!InMeshComponent || !IsValid(InMeshComponent))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("CreateRecordFromMeshComponent: Invalid or null mesh component provided."));
		return false;
	}

	OutRecord.ComponentName = FString::Printf(TEXT("%s_%u"), *InMeshComponent->GetName(), InMeshComponent->GetUniqueID());
	OutRecord.ComponentClassPath = InMeshComponent->GetClass()->GetPathName();

	if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(InMeshComponent))
	{
		if (UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh())
		{
			OutRecord.AssetPath = StaticMesh->GetPathName();
			TArray<UMaterialInterface*> Materials;
			StaticMeshComp->GetUsedMaterials(Materials);
			for (UMaterialInterface* Mat : Materials)
			{
				OutRecord.MaterialPaths.Add(Mat ? Mat->GetPathName() : TEXT(""));
			}
			return true;
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(InMeshComponent))
	{
		if (USkeletalMesh* SkeletalMesh = SkeletalMeshComp->GetSkeletalMeshAsset())
		{
			OutRecord.AssetPath = SkeletalMesh->GetPathName();
			TArray<UMaterialInterface*> Materials;
			SkeletalMeshComp->GetUsedMaterials(Materials);
			for (UMaterialInterface* Mat : Materials)
			{
				OutRecord.MaterialPaths.Add(Mat ? Mat->GetPathName() : TEXT(""));
			}
			UE_LOG(LogBloodStain, Log, TEXT("CreateRecordFromMeshComponent: Created record for SkeletalMeshComponent %s with asset %s."), *InMeshComponent->GetName(), *OutRecord.AssetPath);
			return true;
		}
	}
	UE_LOG(LogBloodStain, Warning, TEXT("CreateRecordFromMeshComponent: Failed to create record from mesh component %s."), *InMeshComponent->GetName());
	return false;
}

