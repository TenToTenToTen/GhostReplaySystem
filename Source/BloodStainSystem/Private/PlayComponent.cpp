/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/



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

DECLARE_CYCLE_STAT(TEXT("PlayComp TickComponent"), STAT_PlayComponent_TickComponent, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp Initialize"), STAT_PlayComponent_Initialize, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp FinishReplay"), STAT_PlayComponent_FinishReplay, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp ApplyComponentTransforms"), STAT_PlayComponent_ApplyComponentTransforms, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp ApplySkeletalBoneTransforms"), STAT_PlayComponent_ApplySkeletalBoneTransforms, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp ConvertFrameToAnimSequence"), STAT_PlayComponent_ConvertFrameToAnimSequence, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp ApplyComponentChanges"), STAT_PlayComponent_ApplyComponentChanges, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp CreateComponentFromRecord"), STAT_PlayComponent_CreateComponentFromRecord, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp SeekFrame"), STAT_PlayComponent_SeekFrame, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp BuildIntervalTree"), STAT_PlayComponent_BuildIntervalTree, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp QueryIntervalTree"), STAT_PlayComponent_QueryIntervalTree, STATGROUP_BloodStain);


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
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_TickComponent); 
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const TArray<FRecordFrame>& Frames = ReplayData.RecordedFrames;
	if (Frames.Num() < 2)
	{
		FinishReplay();
		return;
	}
	
	const float LastTime = Frames.Last().TimeStamp;
	// Compute elapsed time since start, scaled by playback rate
	float Elapsed = (static_cast<float>(GetWorld()->GetTimeSeconds()) - PlaybackStartTime) * PlaybackOptions.PlaybackRate;

	if (PlaybackOptions.bIsLooping)
	{
		// Loop playback within [0, LastTime)
		Elapsed = FMath::Fmod(Elapsed, LastTime);
		if (Elapsed < 0.0f || (PlaybackOptions.PlaybackRate < 0 && Elapsed == 0.f))
		{
			Elapsed += LastTime;
		}
	}
	else
	{
		// If outside of range, finish replay
		if (PlaybackOptions.PlaybackRate < 0)
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
	}
	
	// Interpolate between CurrentFrame and CurrentFrame+1
	const FRecordFrame& Prev = Frames[CurrentFrame];
	const FRecordFrame& Next = Frames[CurrentFrame + 1];
	const float Alpha = (Next.TimeStamp - Prev.TimeStamp > KINDA_SMALL_NUMBER)
		? FMath::Clamp((Elapsed - Prev.TimeStamp) / (Next.TimeStamp - Prev.TimeStamp), 0.0f, 1.0f)
		: 1.0f;
	ApplyComponentTransforms(Prev, Next, Alpha);
	ApplySkeletalBoneTransforms(Prev, Next, Alpha);
}

void UPlayComponent::Initialize(FGuid InPlaybackKey, const FRecordHeaderData& InRecordHeaderData, const FRecordActorSaveData& InReplayData, const FBloodStainPlaybackOptions& InPlaybackOptions)
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_Initialize); 
	PlaybackKey = InPlaybackKey;
	RecordHeaderData = InRecordHeaderData;
    ReplayData = InReplayData;
    PlaybackOptions = InPlaybackOptions;

    PlaybackStartTime = GetWorld()->GetTimeSeconds();
    CurrentFrame      = PlaybackOptions.PlaybackRate > 0 ? 0 : ReplayData.RecordedFrames.Num() - 2;

	TSet<FString> UniqueAssetPaths;
	for (const FComponentInterval& Interval : ReplayData.ComponentIntervals)
	{
		if (!Interval.Meta.AssetPath.IsEmpty())
		{
			UniqueAssetPaths.Add(Interval.Meta.AssetPath);
		}
		for (const FString& MaterialPath : Interval.Meta.MaterialPaths)
		{
			if (!MaterialPath.IsEmpty())
			{
				UniqueAssetPaths.Add(MaterialPath);
			}
		}
	}

	TMap<FString, TObjectPtr<UObject>> AssetCache;

	// 3. 수집된 고유 경로들을 순회하며 에셋을 미리 로드하고 캐시에 저장
	for (const FString& Path : UniqueAssetPaths)
	{
		// FSoftObjectPath를 사용하면 UStaticMesh, USkeletalMesh, UMaterialInterface 등 타입을 구분할 필요 없이 로드 가능
		FSoftObjectPath AssetRef(Path);
		UObject* LoadedAsset = AssetRef.TryLoad();
		if (LoadedAsset)
		{
			AssetCache.Add(Path, LoadedAsset);
		}
		else
		{
			// StaticLoadObject는 블루프린트 클래스 같은 특정 타입을 로드할 때도 유용합니다.
			// 여기서는 SoftObjectPath로 대부분 커버 가능합니다.
			UE_LOG(LogBloodStain, Warning, TEXT("Initialize: Failed to pre-load asset at path: %s"), *Path);
		}
	}
    
	UE_LOG(LogBloodStain, Log, TEXT("Pre-loaded %d unique assets."), AssetCache.Num());
	
	ReconstructedComponents.Empty();
	for (FComponentInterval& Interval : ReplayData.ComponentIntervals)
	{
		if (USceneComponent* NewComp = CreateComponentFromRecord(Interval.Meta, AssetCache))
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
}

void UPlayComponent::FinishReplay()
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_FinishReplay); 
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
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_ApplyComponentTransforms);
	
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
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_ApplySkeletalBoneTransforms);
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


FGuid UPlayComponent::GetPlaybackKey() const
{
	return PlaybackKey;
}

/**
 * @brief FComponentRecord를 기반으로 메시 컴포넌트를 생성하고 월드에 등록합니다.
 * @param Record 생성할 컴포넌트의 정보
 * @return 성공 시 생성된 컴포넌트, 실패 시 nullptr
 */	
USceneComponent* UPlayComponent::CreateComponentFromRecord(const FComponentRecord& Record, const TMap<FString, TObjectPtr<UObject>>& AssetCache)
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_CreateComponentFromRecord);
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
        // AssetRef.TryLoad() 대신 캐시에서 바로 가져옵니다.
        if (const TObjectPtr<UObject>* FoundAsset = AssetCache.Find(Record.AssetPath))
        {
            if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(NewComponent))
            {
                StaticMeshComp->SetStaticMesh(Cast<UStaticMesh>(*FoundAsset));
                StaticMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            else if (UPoseableMeshComponent* PoseableMeshComp = Cast<UPoseableMeshComponent>(NewComponent))
            {
                PoseableMeshComp->SetSkinnedAssetAndUpdate(Cast<USkeletalMesh>(*FoundAsset));
                PoseableMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
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
		if ((PlaybackOptions.bUseGhostMaterial || Record.MaterialPaths[MatIndex].IsEmpty()) && DefaultMaterial)
		{
			NewMeshComponent->SetMaterial(MatIndex, DefaultMaterial);
			continue; // 다음 머티리얼 슬롯으로 넘어감
		}

		// 원본 머티리얼 경로가 비어있지 않은 경우
		if (!Record.MaterialPaths[MatIndex].IsEmpty())
		{
			// StaticLoadObject 대신 캐시에서 바로 가져옵니다.
			UMaterialInterface* OriginalMaterial = nullptr;
			if (const TObjectPtr<UObject>* FoundMaterial = AssetCache.Find(Record.MaterialPaths[MatIndex]))
			{
				OriginalMaterial = Cast<UMaterialInterface>(*FoundMaterial);
			}

			if (!OriginalMaterial)
			{
				UE_LOG(LogBloodStain, Warning, TEXT("Failed to find pre-loaded material: %s"), *Record.MaterialPaths[MatIndex]);
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

	// UE_LOG(LogBloodStain, Log, TEXT("Replay: Component Created - %s"), *Record.PrimaryComponentName);
	
	return NewComponent;
}

void UPlayComponent::SeekFrame(int32 FrameIndex)
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_SeekFrame);
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
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_BuildIntervalTree);
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
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_QueryIntervalTree);
	if (!Node)
	{
		return;
	}

	// 1. 이 노드의 리스트에서 커버되는 구간 수집
	for (auto* I : Node->Intervals)
	{
		if (I->StartFrame <= FrameIndex && FrameIndex < I->EndFrame)
			Out.Add(I);
	}

	// 2. 좌/우 서브트리 결정
	if (FrameIndex < Node->Center)
	{
		QueryIntervalTree(Node->Left.Get(), FrameIndex, Out);		
	}
	else if (FrameIndex > Node->Center)
	{
		QueryIntervalTree(Node->Right.Get(), FrameIndex, Out);		
	}
}