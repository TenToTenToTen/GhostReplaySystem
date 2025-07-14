// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayComponent.h"
#include "BloodStainSubsystem.h"
#include "BloodStainSystem.h"
#include "ReplayActor.h"
#include "Animation/AnimSequence.h"
#include "Stats/Stats2.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Animation/Skeleton.h"

// 스탯 그룹, 스탯 한번만 정의
DECLARE_STATS_GROUP(TEXT("BloodStain"), STATGROUP_BloodStain, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("ApplyBoneTransforms"), STAT_ApplyBoneTransforms, STATGROUP_BloodStain);

// Sets default values for this component's properties
UPlayComponent::UPlayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UPlayComponent::BeginPlay()
{
	Super::BeginPlay();
}


// Called every frame
void UPlayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const TArray<FRecordFrame>& Frames = ReplayData.RecordedFrames;
	if (Frames.Num() < 2)
	{
		FinishReplay();
		return;
	}
	
	const float LastTime = Frames.Last().TimeStamp;
	// Compute elapsed time since start, scaled by playback rate
	float Elapsed = (static_cast<float>(GetWorld()->GetTimeSeconds()) - PlaybackStartTime) * ReplayOptions.PlaybackRate;

	if (ReplayOptions.bIsLooping)
	{
		// Loop playback within [0, LastTime)
		Elapsed = FMath::Fmod(Elapsed, LastTime);
		if (Elapsed < 0.0f || (ReplayOptions.PlaybackRate < 0 && Elapsed == 0.f))
		{
			Elapsed += LastTime;
		}
	}
	else
	{
		// If outside of range, finish replay
		if (ReplayOptions.PlaybackRate < 0)
		{
			if (Elapsed > 0.f || Elapsed < -LastTime)
			{
				FinishReplay();
				return;
			}
			Elapsed += LastTime;
		}
		else if (Elapsed < 0.0f || Elapsed > LastTime)
		{
			FinishReplay();
			return;
		}
	}

	const int32 PreviousFrame = CurrentFrame;
	
	// Determine the frame index that brackets Elapsed
	int32 Num = Frames.Num();
	int32 NewFrameIndex = 0;
	for (int32 i = 0; i < Num - 1; ++i)
	{
		if (Frames[i + 1].TimeStamp > Elapsed)
		{
			NewFrameIndex = i;
			break;
		}
		if (i == Num - 2)
		{
			NewFrameIndex = Num - 2;
		}
	}
	CurrentFrame = NewFrameIndex;
	if (PreviousFrame != CurrentFrame)
	{
		SeekFrame(CurrentFrame);
		// ApplyComponentChanges(ReplayData.RecordedFrames[CurrentFrame]);
	}
	// ApplyComponentChanges(ReplayData.RecordedFrames[CurrentFrame]);

// Sweep에 대한 처리 (Animation Notify와 유사)
	// for (int32 FrameToProcess = PreviousFrame + 1; FrameToProcess <= CurrentFrame; ++FrameToProcess)
	// {
	// 	// 이제 건너뛰는 프레임 없이 모든 이벤트가 처리됨!
	// 	ApplyComponentChanges(Frames[FrameToProcess]);
	// }

	// Interpolate between CurrentFrame and CurrentFrame+1
	const FRecordFrame& Prev = Frames[CurrentFrame];
	const FRecordFrame& Next = Frames[CurrentFrame + 1];
	const float Alpha = (Next.TimeStamp - Prev.TimeStamp > KINDA_SMALL_NUMBER)
		? FMath::Clamp((Elapsed - Prev.TimeStamp) / (Next.TimeStamp - Prev.TimeStamp), 0.0f, 1.0f)
		: 1.0f;
	ApplyComponentTransforms(Prev, Next, Alpha);
	ApplySkeletalBoneTransforms(Prev, Next, Alpha);
}

void UPlayComponent::Initialize(FGuid InPlaybackKey, const FRecordActorSaveData& InReplayData, const FBloodStainRecordOptions& InReplayOptions)
{
    ReplayData = InReplayData;
	PlaybackKey = InPlaybackKey;
    ReplayOptions = InReplayOptions;

    PlaybackStartTime = GetWorld()->GetTimeSeconds();
    CurrentFrame      = ReplayOptions.PlaybackRate > 0 ? 0 : ReplayData.RecordedFrames.Num() - 2;

	ReconstructedComponents.Empty();
	for (FComponentInterval& Interval : ReplayData.ComponentIntervals)
	{
		if (USceneComponent* NewComp = CreateComponentFromRecord(Interval.Meta))
		{
			NewComp->SetVisibility(false);
			NewComp->SetActive(false);
			ReconstructedComponents.Add(Interval.Meta.ComponentName, NewComp);
			UE_LOG(LogBloodStain, Log, TEXT("Initialize: Component Added - %s"), *Interval.Meta.ComponentName);
		}
		else
		{
			UE_LOG(LogBloodStain, Warning, TEXT("Initialize: Failed to create comp from interval: %s"), *Interval.Meta.ComponentName);
		}
	}
	
	/* 특정 점에 걸치는 Alive component 쿼리용 Interval Tree 초기화 */
	TArray<FComponentInterval*> Ptrs;
	for (auto& I : ReplayData.ComponentIntervals)
	{
		// I.EndFrame = FMath::Clamp(I.EndFrame, 0, ReplayData.RecordedFrames.Num() - 1);
		Ptrs.Add(&I);			
	}
	IntervalRoot = BuildIntervalTree(Ptrs);
	SeekFrame(0);

	// int32 eventSum = 0;
	// for (int32 i = 0; i < ReplayData.RecordedFrames.Num(); ++i)
	// {
	// 	const FRecordFrame& Frame = ReplayData.RecordedFrames[i];
	// 	if (Frame.AddedComponents.Num() > 0)
	// 	{
	// 		eventSum += Frame.AddedComponents.Num();
	// 		UE_LOG(LogBloodStain, Log, TEXT("PlayComponent::Initialize(): Frame %d has %d added components"), i, Frame.AddedComponents.Num());
	// 	}
	// }
	// if (eventSum == 0)
	// {
	// 	UE_LOG(LogBloodStain, Log, TEXT("PlayComponent::Initialize(): No added components found in replay data!"));
	// }
	
	// for (const FComponentRecord& Record : ReplayData.InitialComponentStructure)
	// {
	// 	if (USceneComponent* NewComponent = CreateComponentFromRecord(Record))
	// 	{
	// 		ReconstructedComponents.Add(Record.ComponentName, NewComponent);
	// 	}
	// 	else
	// 	{
	// 		UE_LOG(LogBloodStain, Warning, TEXT("RecordComponent::Initialize(): Failed to create component from record: %s"), *Record.ComponentName);
	// 	}
	// }
	

    // Generate animation sequences if not already present
    // if (AnimSequences.Num() == 0)
    // {
    //     ConvertFrameToAnimSequence();
    // }
    //
    // // Play animations on skeletal components
    // for (auto& Pair : ReconstructedComponents)
    // {
    //     const FString& CompName = Pair.Key;
    //     if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Pair.Value))
    //     {
    //         if (UAnimSequence* Seq = AnimSequences.FindRef(CompName))
    //         {
	   //          SkelComp->PlayAnimation(Seq, ReplayOptions.bIsLooping);
	   //          SkelComp->SetPlayRate(ReplayOptions.PlaybackRate);
    //         	if (ReplayOptions.PlaybackRate < 0)
    //         	{
    //         		SkelComp->SetPosition(Seq->GetPlayLength(), false);
    //         	}
    //         }
    //     }
    // }
}

void UPlayComponent::FinishReplay()
{
	// Subsystem에 종료 요청
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UBloodStainSubsystem* Sub = GI->GetSubsystem<UBloodStainSubsystem>())
			{
				// Owner는 AReplayActor
				if (AReplayActor* RA = Cast<AReplayActor>(GetOwner()))
				{
					Sub->StopReplayPlayComponent(RA);
				}
			}
		}
	}
}

void UPlayComponent::ApplyComponentTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const
{
	//SCOPE_CYCLE_COUNTER(STAT_ApplyBoneTransforms);
	for (const auto& Pair : Next.ComponentTransforms)
	{
		const FString& ComponentName = Pair.Key;
		const FTransform& NextT = Pair.Value; // NextT는 월드 트랜스폼

		if (USceneComponent* TargetComponent = ReconstructedComponents.FindRef(ComponentName))
		{
			if (const FTransform* PrevT = Prev.ComponentTransforms.Find(ComponentName))
			{
				FVector Loc = FMath::Lerp(PrevT->GetLocation(), NextT.GetLocation(), Alpha);
				FQuat Rot = FQuat::Slerp(PrevT->GetRotation(), NextT.GetRotation(), Alpha);
				FVector Scale = FMath::Lerp(PrevT->GetScale3D(), NextT.GetScale3D(), Alpha);

				FTransform InterpT(Rot, Loc, Scale);
				TargetComponent->SetWorldTransform(InterpT);
			}
			else
			{
				TargetComponent->SetWorldTransform(NextT);
			}
		}
	}
}

void UPlayComponent::ApplySkeletalBoneTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const
{
	
	for (const auto& Pair : ReconstructedComponents)
	{
		if (UPoseableMeshComponent* PoseableComp = Cast<UPoseableMeshComponent>(Pair.Value))
		{
			const FString& ComponentName = Pair.Key;
			
			const FBoneComponentSpace* PrevBones = Prev.SkeletalMeshBoneTransforms.Find(ComponentName);
			const FBoneComponentSpace* NextBones = Next.SkeletalMeshBoneTransforms.Find(ComponentName);

			if (PrevBones && NextBones)
			{
				const int32 NumBones = FMath::Min(PrevBones->BoneTransforms.Num(), NextBones->BoneTransforms.Num());
                
				// 모든 뼈대에 대해 보간 및 적용
				for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
				{
					const FTransform& PrevT = PrevBones->BoneTransforms[BoneIndex];
					const FTransform& NextT = NextBones->BoneTransforms[BoneIndex];
					
					FTransform InterpT;
					InterpT.SetLocation(FMath::Lerp(PrevT.GetLocation(), NextT.GetLocation(), Alpha));
					InterpT.SetRotation(FQuat::Slerp(PrevT.GetRotation(), NextT.GetRotation(), Alpha));
					InterpT.SetScale3D(FMath::Lerp(PrevT.GetScale3D(), NextT.GetScale3D(), Alpha));
					
					PoseableComp->BoneSpaceTransforms[BoneIndex] = InterpT;
				}
				PoseableComp->MarkRefreshTransformDirty();
			}
		}
	}
}

void UPlayComponent::ConvertFrameToAnimSequence()
{
    const TArray<FRecordFrame>& Frames = ReplayData.RecordedFrames;
    if (Frames.Num() < 2)
    {
        UE_LOG(LogBloodStain, Warning, TEXT("Not enough frames to generate animation."));
        return;
    }

    // Iterate over all reconstructed components and generate sequences for skeletal meshes
    for (auto& Pair : ReconstructedComponents)
    {
        const FString& CompName = Pair.Key;
        USceneComponent* SceneComp = Pair.Value;
        USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(SceneComp);
        if (!SkeletalComp || !SkeletalComp->GetSkeletalMeshAsset())
        {
            continue;
        }

        USkeletalMesh* Mesh = SkeletalComp->GetSkeletalMeshAsset();
        USkeleton* Skeleton = Mesh->GetSkeleton();
        if (!Skeleton)
        {
            UE_LOG(LogBloodStain, Warning, TEXT("SkeletalMesh %s has no skeleton."), *CompName);
            continue;
        }

        const int32 NumFrames = Frames.Num() - 1;
        const float SequenceLength = Frames.Last().TimeStamp;
        const FReferenceSkeleton& SkeletonRef = Skeleton->GetReferenceSkeleton();
        const FReferenceSkeleton& MeshRef     = Mesh->GetRefSkeleton();
        const int32 NumBones = SkeletonRef.GetNum();

        // Create a new animation sequence
        UAnimSequence* AnimSeq = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transient);
        AnimSeq->SetSkeleton(Skeleton);
        AnimSeq->SetPreviewMesh(Mesh);

#if WITH_EDITOR
        IAnimationDataController& Controller = AnimSeq->GetController();
        Controller.InitializeModel();
        Controller.OpenBracket(FText::FromString(FString::Printf(TEXT("Generate_%s"), *CompName)));

    	const float FrameRate = 1.0f / ReplayOptions.SamplingInterval;
        Controller.SetFrameRate(FFrameRate(FrameRate, 1));
        Controller.SetNumberOfFrames(NumFrames);

        // Build tracks for each bone
        for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
        {
            const FName BoneName = SkeletonRef.GetBoneName(BoneIndex);
            const int32 MeshBoneIndex = MeshRef.FindBoneIndex(BoneName);
            if (MeshBoneIndex == INDEX_NONE)
            {
                UE_LOG(LogBloodStain, Warning, TEXT("Bone %s not found in mesh ref skeleton."), *BoneName.ToString());
                continue;
            }

            TArray<FVector3f> PosKeys; PosKeys.Reserve(NumFrames);
            TArray<FQuat4f>   RotKeys; RotKeys.Reserve(NumFrames);
            TArray<FVector3f> ScaleKeys;ScaleKeys.Reserve(NumFrames);

            // Populate key arrays
            for (int32 FrameIdx = 0; FrameIdx < NumFrames; ++FrameIdx)
            {
                const FRecordFrame& Frame = Frames[FrameIdx];
                const FBoneComponentSpace* BoneSpace = Frame.SkeletalMeshBoneTransforms.Find(CompName);
                FTransform LocalT = FTransform::Identity;

                if (BoneSpace && BoneSpace->BoneTransforms.IsValidIndex(MeshBoneIndex))
                {
                    // Compute local transform relative to parent
                    if (BoneIndex == 0)
                    {
                        LocalT = BoneSpace->BoneTransforms[MeshBoneIndex];
                    }
                    else
                    {
                        const int32 ParentIndex = SkeletonRef.GetParentIndex(BoneIndex);
                        const FName ParentName = SkeletonRef.GetBoneName(ParentIndex);
                        const int32 MeshParentIndex = MeshRef.FindBoneIndex(ParentName);
                        if (BoneSpace->BoneTransforms.IsValidIndex(MeshParentIndex))
                        {
                            LocalT = BoneSpace->BoneTransforms[MeshBoneIndex]
                                     .GetRelativeTransform(BoneSpace->BoneTransforms[MeshParentIndex]);
                        }
                        else
                        {
                            LocalT = BoneSpace->BoneTransforms[MeshBoneIndex];
                        }
                    }
                }

                PosKeys.Add((FVector3f)LocalT.GetTranslation());
                RotKeys.Add((FQuat4f)LocalT.GetRotation());
                ScaleKeys.Add((FVector3f)LocalT.GetScale3D());
            }

            Controller.AddBoneCurve(BoneName);
            Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys);
        }
        Controller.NotifyPopulated();
        Controller.CloseBracket();
#endif
        // Store the generated sequence
        AnimSequences.Add(CompName, AnimSeq);
    }

    UE_LOG(LogBloodStain, Log, TEXT("Converted %d skeletal components to AnimSequences."), AnimSequences.Num());
}

void UPlayComponent::ApplyComponentChanges(const FRecordFrame& Frame)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Owner is null in ApplyComponentChanges."));
		return;
	}

	// 1. 컴포넌트 추가
	for (const FComponentRecord& Record : Frame.AddedComponents)
	{
		// 이미 해당 이름의 컴포넌트가 있다면 중복 생성을 방지
		const FString& Name = Record.ComponentName;
		if (ReconstructedComponents.Contains(Name))
		{
			UE_LOG(LogBloodStain, Warning, TEXT("ApplyComponentChanges %s already exists, skipping."), *Record.ComponentName);
			continue;
		}

		if (TObjectPtr<USceneComponent> NewComponent = CreateComponentFromRecord(Record))
		{
			ReconstructedComponents.Add(Name, NewComponent);
			UE_LOG(LogBloodStain, Log, TEXT("ApplyComponentChanges: Component Added - %s"), *Record.ComponentName);
		}
		else
		{
			UE_LOG(LogBloodStain, Warning, TEXT("ApplyComponentChanges: Failed to create comp %s"), *Record.ComponentName);
		}
	}

	// 2. 컴포넌트 제거 처리
	for (const FString& ComponentName : Frame.RemovedComponentNames)
	{
		// 맵에서 제거할 컴포넌트를 찾습니다.
		if (TObjectPtr<USceneComponent>* CompPtr = ReconstructedComponents.Find(ComponentName))
		{
			if (TObjectPtr<USceneComponent>CompToDestroy = *CompPtr)
			{
				// 컴포넌트가 유효하면 파괴합니다.
				if (IsValid(CompToDestroy))
				{
					CompToDestroy->DestroyComponent();
				}
			}
			// 맵에서 해당 항목을 제거합니다.
			ReconstructedComponents.Remove(ComponentName);
			UE_LOG(LogBloodStain, Log, TEXT("Replay: Component Removed - %s"), *ComponentName);
		}
	}
}

FGuid UPlayComponent::GetPlaybackKey() const
{
	return PlaybackKey;
}

/**
 * @brief FComponentRecord를 기반으로 메시 컴포넌트를 생성하고 월드에 등록합니다.
 * @param Record 생성할 컴포넌트의 정보
 * @return 성공 시 생성된 컴포넌트, 실패 시 nullptr
 */	
USceneComponent* UPlayComponent::CreateComponentFromRecord(const FComponentRecord& Record)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("CreateComponentFromRecord failed: Owner is null."));
		return nullptr;
	}

	// 1. FComponentRecord로부터 컴포넌트 클래스를 로드합니다.
	UClass* ComponentClass = FindObject<UClass>(nullptr, *Record.ComponentClassPath);
	if (!ComponentClass ||
		!(ComponentClass->IsChildOf(UStaticMeshComponent::StaticClass()) || ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass())))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Failed to load or invalid component class: %s"), *Record.ComponentClassPath);
		return nullptr;
	}

	// 2. Owner 액터에 새 컴포넌트를 생성합니다.
	USceneComponent* NewComponent = nullptr;
	
	if (ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
	{
		NewComponent = NewObject<UPoseableMeshComponent>(Owner, UPoseableMeshComponent::StaticClass(), FName(*Record.ComponentName));
	}
	else if (ComponentClass->IsChildOf(UStaticMeshComponent::StaticClass()))
	{
		NewComponent = NewObject<UStaticMeshComponent>(Owner, ComponentClass, FName(*Record.ComponentName));
	}
	else
	{
		return nullptr;
	}
	if (!NewComponent) return nullptr;

	UMeshComponent* NewMeshComponent = Cast<UMeshComponent>(NewComponent);
	if (!NewMeshComponent)
	{
		NewComponent->DestroyComponent();
		return nullptr;
	}
	
	if (!Record.AssetPath.IsEmpty())
	{
		FSoftObjectPath AssetRef(Record.AssetPath);
		if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(NewComponent))
		{
			StaticMeshComp->SetStaticMesh(Cast<UStaticMesh>(AssetRef.TryLoad()));
			StaticMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else if (UPoseableMeshComponent* PoseableMeshComp = Cast<UPoseableMeshComponent>(NewComponent))
		{
			PoseableMeshComp->SetSkinnedAssetAndUpdate(Cast<USkeletalMesh>(AssetRef.TryLoad()));
			PoseableMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	UMaterialInterface* DefaultMaterial = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UBloodStainSubsystem* Sub = GI->GetSubsystem<UBloodStainSubsystem>())
			{
				DefaultMaterial = Sub->GetDefaultMaterial();
			}
		}
	}
	// 3-2. 머티리얼을 순서대로 적용합니다.
	for (int32 MatIndex = 0; MatIndex < Record.MaterialPaths.Num(); ++MatIndex)
	{
		// 옵션에 따라 고스트 머티리얼을 강제 적용하는 경우 (기존 로직)
		if ((ReplayOptions.bUseGhostMaterial || Record.MaterialPaths[MatIndex].IsEmpty()) && DefaultMaterial)
		{
			NewMeshComponent->SetMaterial(MatIndex, DefaultMaterial);
			continue; // 다음 머티리얼 슬롯으로 넘어감
		}

		// 원본 머티리얼 경로가 비어있지 않은 경우
		if (!Record.MaterialPaths[MatIndex].IsEmpty())
		{
			// 원본 머티리얼 애셋을 로드합니다.
			UMaterialInterface* OriginalMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *Record.MaterialPaths[MatIndex]));
			if (!OriginalMaterial)
			{
				UE_LOG(LogBloodStain, Warning, TEXT("Failed to load material: %s"), *Record.MaterialPaths[MatIndex]);
				continue;
			}
			
			// 현재 머티리얼 인덱스에 해당하는 저장된 동적 파라미터가 있는지 확인합니다.
			if (Record.MaterialParameters.Contains(MatIndex))
			{
				// 파라미터가 있다면, 원본 머티리얼을 부모로 하는 MID를 생성
				UMaterialInstanceDynamic* DynMaterial = NewMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(MatIndex, OriginalMaterial);

				if (DynMaterial)
				{
					// 저장된 파라미터 값들을 가져와서 새로 만든 MID에 하나씩 적용
					const FMaterialParameters& SavedParams = Record.MaterialParameters[MatIndex];
					
					for (const auto& Pair : SavedParams.VectorParams)
					{
						DynMaterial->SetVectorParameterValue(Pair.Key, Pair.Value);
					}
					for (const auto& Pair : SavedParams.ScalarParams)
					{
						DynMaterial->SetScalarParameterValue(Pair.Key, Pair.Value);
					}
					UE_LOG(LogBloodStain, Log, TEXT("Restored dynamic material for component %s at index %d"), *Record.ComponentName, MatIndex);
				}
			}
			else
			{
				// 저장된 파라미터가 없다면,  원본 머티리얼을 그대로 적용
				NewMeshComponent->SetMaterial(MatIndex, OriginalMaterial);
			}
		}
	}

	// 4. 컴포넌트를 등록하고 루트에 부착합니다.
	NewComponent->RegisterComponent();
	NewComponent->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

	// UE_LOG(LogBloodStain, Log, TEXT("Replay: Component Created - %s"), *Record.ComponentName);
	
	return NewComponent;
}

void UPlayComponent::SeekFrame(int32 FrameIndex)
{
	if (FrameIndex < 0 || FrameIndex >= ReplayData.RecordedFrames.Num())
	{
		UE_LOG(LogBloodStain, Warning, TEXT("SeekToFrame: TargetFrame %d is out of bounds."), FrameIndex);
		return;
	}

	TArray<FComponentInterval*> AliveComps;
	QueryIntervalTree(IntervalRoot.Get(), FrameIndex, AliveComps);

	// TSet으로 변환하여 빠른 조회를 위함 (O(1) 평균 시간 복잡도)
	TSet<FString> AliveComponentNames;
	for (const FComponentInterval* Interval : AliveComps)
	{
		AliveComponentNames.Add(Interval->Meta.ComponentName);
	}

	// 2. 미리 생성된 모든 컴포넌트를 순회하며 상태를 업데이트합니다.
	for (auto& Pair : ReconstructedComponents)
	{
		const FString& ComponentName = Pair.Key;
		USceneComponent* Component = Pair.Value;

		if (!Component) continue;

		// 현재 프레임에 활성화되어야 하는지 확인
		const bool bShouldBeActive = AliveComponentNames.Contains(ComponentName);
		const bool bIsCurrentlyActive = Component->IsVisible();

		// 상태가 변경되어야 할 때만 함수를 호출하여 불필요한 비용을 줄입니다.
		if (bShouldBeActive != bIsCurrentlyActive)
		{
			Component->SetVisibility(bShouldBeActive);
			Component->SetActive(bShouldBeActive);
		}
	}
}

/*
 * 균형 이진 트리 전제. 각 중점 왼쪽과 오른쪽에 위치한 Interval list 분류 및 트리 구성
 */
TUniquePtr<FIntervalTreeNode> UPlayComponent::BuildIntervalTree(TArray<FComponentInterval*>& List)
{
	if (List.Num() == 0)
	{
		return nullptr;		
	}

	// 1) 중점(center) 결정: 모든 start/end의 중간값
	TArray<int32> Endpoints;
	for (auto* I : List)
	{
		Endpoints.Add(I->StartFrame);
		Endpoints.Add(I->EndFrame);
	}
	Endpoints.Sort();
	int32 Mid = Endpoints[Endpoints.Num()/2];

	// 2) 현재 노드에 걸치는 구간 분류
	TArray<FComponentInterval*> LeftList, RightList;
	TUniquePtr<FIntervalTreeNode> Node = MakeUnique<FIntervalTreeNode>();
	Node->Center = Mid;
	for (auto* I : List)
	{
		/* 현재 Mid에 겹치는 Interval만 추가, 겹치지 않는 것은 좌우 Node로 분류*/
		if (I->EndFrame < Mid)
			LeftList.Add(I);
		else if (I->StartFrame > Mid)
			RightList.Add(I);
		else
			Node->Intervals.Add(I);
	}

	// 3) 재귀
	Node->Left  = BuildIntervalTree(LeftList);
	Node->Right = BuildIntervalTree(RightList);
	return Node;
}

void UPlayComponent::QueryIntervalTree(FIntervalTreeNode* Node, int32 FrameIndex, TArray<FComponentInterval*>& Out)
{
	if (!Node) return;

	// 1. 이 노드의 리스트에서 커버되는 구간 수집
	for (auto* I : Node->Intervals)
	{
		if (I->StartFrame <= FrameIndex && FrameIndex < I->EndFrame)
			Out.Add(I);
	}

	// 2. 좌/우 서브트리 결정
	if (FrameIndex < Node->Center)
		QueryIntervalTree(Node->Left.Get(), FrameIndex, Out);
	else if (FrameIndex > Node->Center)
		QueryIntervalTree(Node->Right.Get(), FrameIndex, Out);
}