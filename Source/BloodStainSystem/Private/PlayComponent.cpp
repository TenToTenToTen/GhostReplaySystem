/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/



#include "PlayComponent.h"
#include "BloodStainSubsystem.h"
#include "BloodStainSystem.h"
#include "ReplayActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GhostAnimInstance.h"

DECLARE_CYCLE_STAT(TEXT("PlayComp TickComponent"), STAT_PlayComponent_TickComponent, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp Initialize"), STAT_PlayComponent_Initialize, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp FinishReplay"), STAT_PlayComponent_FinishReplay, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp ApplyComponentTransforms"), STAT_PlayComponent_ApplyComponentTransforms, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp ApplySkeletalBoneTransforms"), STAT_PlayComponent_ApplySkeletalBoneTransforms, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp ApplyComponentChanges"), STAT_PlayComponent_ApplyComponentChanges, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp CreateComponentFromRecord"), STAT_PlayComponent_CreateComponentFromRecord, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp SeekFrame"), STAT_PlayComponent_SeekFrame, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp BuildIntervalTree"), STAT_PlayComponent_BuildIntervalTree, STATGROUP_BloodStain);
DECLARE_CYCLE_STAT(TEXT("PlayComp QueryIntervalTree"), STAT_PlayComponent_QueryIntervalTree, STATGROUP_BloodStain);


UPlayComponent::UPlayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPlayComponent::Initialize(FGuid InPlaybackKey, const FRecordHeaderData& InRecordHeaderData, const FRecordActorSaveData& InReplayData, const FBloodStainPlaybackOptions& InPlaybackOptions)
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_Initialize);
	ReplayActor = GetOwner();
	PlaybackKey = InPlaybackKey;
	RecordHeaderData = InRecordHeaderData;

	ReplayData = InReplayData;
    PlaybackOptions = InPlaybackOptions;

    PlaybackStartTime = GetWorld()->GetTimeSeconds();
    CurrentFrame      = PlaybackOptions.PlaybackRate > 0 ? 0 : ReplayData.RecordedFrames.Num() - 2;

	TSet<FString> UniqueAssetPaths;
	for (const FComponentActiveInterval& Interval : ReplayData.ComponentIntervals)
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

	// Iterate through the collected unique paths to pre-load assets and store them in the cache.
	for (const FString& Path : UniqueAssetPaths)
	{
		// Using FSoftObjectPath allows loading without needing to distinguish between UStaticMesh, USkeletalMesh, UMaterialInterface, etc.
		FSoftObjectPath AssetRef(Path);
		UObject* LoadedAsset = AssetRef.TryLoad();
		if (LoadedAsset)
		{
			AssetCache.Add(Path, LoadedAsset);
		}
		else
		{
			// StaticLoadObject can also be useful for loading specific types like Blueprint classes.
			// SoftObjectPath covers most cases.
			UE_LOG(LogBloodStain, Warning, TEXT("Initialize: Failed to pre-load asset at path: %s"), *Path);
		}
	}
    
	UE_LOG(LogBloodStain, Log, TEXT("Pre-loaded %d unique assets."), AssetCache.Num());
	
	ReconstructedComponents.Empty();
	for (const FComponentActiveInterval& Interval : ReplayData.ComponentIntervals)
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

	SkelInfos.Reset();
	for (auto& [ComponentName, Component] : ReconstructedComponents)
	{
		if (USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(Component))
		{
			SkelInfos.Emplace(Sk, *ComponentName);
		}
	}
	
	// Initialize the Interval Tree for querying active components at a specific point(frame) in time.
	TArray<FComponentActiveInterval*> Ptrs;
	for (FComponentActiveInterval& I : ReplayData.ComponentIntervals)
	{
		// I.EndFrame = FMath::Clamp(I.EndFrame, 0, ReplayData.RecordedFrames.Num() - 1);
		Ptrs.Add(&I);			
	}
	IntervalRoot = BuildIntervalTree(Ptrs);
	SeekFrame(0);
	
	bIsInitialized = true;
}

void UPlayComponent::FinishReplay() const
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_FinishReplay);
	
	// Request termination from the subsystem.
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UBloodStainSubsystem* Sub = GI->GetSubsystem<UBloodStainSubsystem>())
			{
				// The owner is an AReplayActor.
				if (AReplayActor* RA = Cast<AReplayActor>(GetOwner()))
				{
					Sub->StopReplayPlayComponent(RA);
				}
			}
		}
	}
}

bool UPlayComponent::CalculatePlaybackTime(float& OutElapsedTime)
{
	const TArray<FRecordFrame>& Frames = ReplayData.RecordedFrames;
	constexpr int32 MinFramesRequired = 2;
	if (Frames.Num() < MinFramesRequired)
	{
		return false;
	}

	const float Duration = RecordHeaderData.TotalLength;
	if (Duration <= 0.f)
	{
		return false;
	}

	static float Epsilon = FLT_EPSILON;  

	// Calculate elapsed time based on the current world time
	OutElapsedTime = (GetWorld()->GetTimeSeconds() - PlaybackStartTime) * PlaybackOptions.PlaybackRate;

	if (PlaybackOptions.bIsLooping)
	{
		// Looping playback: wrap the time to the [0, Duration) range.
		OutElapsedTime = FMath::Fmod(OutElapsedTime, Duration);
		if (OutElapsedTime < -Epsilon)
		{
			OutElapsedTime += Duration;
		}
	}
	else
	{
		// Single playback: check if the time is out of bounds.
		// For reverse playback, values start as negative, so add Duration to map to the [0, Duration] range.
		if (PlaybackOptions.PlaybackRate < 0.f)
		{
			OutElapsedTime += Duration;
		}

		if (OutElapsedTime < -Epsilon || OutElapsedTime > Duration)
		{
			return false;
		}
	}

	return true;
}

void UPlayComponent::UpdatePlaybackToTime(float ElapsedTime)
{
	const bool bShouldBeHidden = ReplayData.RecordedFrames.IsEmpty() || 
							 ElapsedTime < ReplayData.RecordedFrames[0].TimeStamp || 
							 ElapsedTime > ReplayData.RecordedFrames.Last().TimeStamp;
	ReplayActor->SetActorHiddenInGame(bShouldBeHidden);

	if (bShouldBeHidden)
	{
		return;
	}
	
	const TArray<FRecordFrame>& Frames = ReplayData.RecordedFrames;
	const int32 PreviousFrame = CurrentFrame;

	// Find the correct frame index for the current time using a binary search.
	const int32 UpperBoundIndex = Algo::UpperBoundBy(Frames, ElapsedTime, [](const FRecordFrame& Frame) {
		return Frame.TimeStamp;
	});
	const int32 NewFrameIndex = FMath::Clamp(UpperBoundIndex - 1, 0, Frames.Num() - 2);
	
	CurrentFrame = NewFrameIndex;
	if (PreviousFrame != CurrentFrame)
	{
		// Only handle component activation/deactivation when the frame index changes.
		SeekFrame(CurrentFrame);
	}

	// Interpolate between the current and next frames, then apply the transforms.
	const FRecordFrame& Prev = Frames[CurrentFrame];
	const FRecordFrame& Next = Frames[CurrentFrame + 1];
    
	const float FrameDuration = Next.TimeStamp - Prev.TimeStamp;
	const float Alpha = (FrameDuration > KINDA_SMALL_NUMBER)
		? FMath::Clamp((ElapsedTime - Prev.TimeStamp) / FrameDuration, 0.0f, 1.0f)
		: 1.0f;
	
	ApplyComponentTransforms(Prev, Next, Alpha);
	ApplySkeletalBoneTransforms(Prev, Next, Alpha);
}

void UPlayComponent::ApplyMaterial(UMaterialInterface* InMaterial) const
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[UPlayComponent::ApplyMaterial] Failed Owner is null"));
		return;
	}

	TSet<FString> UniqueAssetPaths;
	for (const FComponentActiveInterval& Interval : ReplayData.ComponentIntervals)
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

	// Iterate through the collected unique paths to pre-load assets and store them in the cache.
	for (const FString& Path : UniqueAssetPaths)
	{
		// Using FSoftObjectPath allows loading without needing to distinguish between UStaticMesh, USkeletalMesh, UMaterialInterface, etc.
		FSoftObjectPath AssetRef(Path);
		UObject* LoadedAsset = AssetRef.TryLoad();
		if (LoadedAsset)
		{
			AssetCache.Add(Path, LoadedAsset);
		}
		else
		{
			// StaticLoadObject can also be useful for loading specific types like Blueprint classes.
			// SoftObjectPath covers most cases.
			UE_LOG(LogBloodStain, Warning, TEXT("Initialize: Failed to pre-load asset at path: %s"), *Path);
		}
	}
    
	UE_LOG(LogBloodStain, Log, TEXT("Pre-loaded %d unique assets."), AssetCache.Num());
	
	for (const FComponentActiveInterval& Interval : ReplayData.ComponentIntervals)
	{
		const FComponentRecord& Record = Interval.Meta;

		UMeshComponent* MeshComponent = FindObject<UMeshComponent>(Owner, *Record.ComponentName);
		
		// Apply materials in order.
		for (int32 MatIndex = 0; MatIndex < Record.MaterialPaths.Num(); ++MatIndex)
		{
			// Force the ghost material if the option is enabled
			if (InMaterial)
			{
				MeshComponent->SetMaterial(MatIndex, InMaterial);
				continue; // Move to the next material slot.
			}

			// If the original material path is not empty and bUseGhostMaterial is false.
			if (!Record.MaterialPaths[MatIndex].IsEmpty())
			{
				// Get the material directly from the cache instead of using StaticLoadObject.
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
			
				// Check if there are saved dynamic parameters for the current material index.
				if (Record.MaterialParameters.Contains(MatIndex))
				{
					UMaterialInstanceDynamic* DynMaterial = MeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(MatIndex, OriginalMaterial);

					if (DynMaterial)
					{
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
					// If no parameters are saved, apply the original material directly.
					MeshComponent->SetMaterial(MatIndex, OriginalMaterial);
				}
			}
		}
	}
}

bool UPlayComponent::IsTickable() const
{
	return bIsInitialized;
}

FGuid UPlayComponent::GetPlaybackKey() const
{
	return PlaybackKey;
}

void UPlayComponent::ApplyComponentTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_ApplyComponentTransforms);

	// Interpolate transforms for all components in the current frame in world space.
	for (const auto& Pair : Next.ComponentTransforms)
	{
		const FString& ComponentName = Pair.Key;
		const FTransform& NextT = Pair.Value;

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

	for (const FSkelReplayInfo& Info : SkelInfos)
	{
		const FBoneComponentSpace* PrevBones = Prev.SkeletalMeshBoneTransforms.Find(Info.ComponentName);
		const FBoneComponentSpace* NextBones = Next.SkeletalMeshBoneTransforms.Find(Info.ComponentName);
		if (!PrevBones || !NextBones)
		{
			continue;
		}

		const int32 NumBones = FMath::Min(PrevBones->BoneTransforms.Num(),NextBones->BoneTransforms.Num());
		if (NumBones == 0)
		{
			continue;
		}

		TArray<FTransform> OutPose;
		OutPose.SetNumUninitialized(NumBones);

		for (int32 i = 0; i < NumBones; ++i)
		{
			const FTransform& P = PrevBones->BoneTransforms[i];
			const FTransform& N = NextBones->BoneTransforms[i];

			OutPose[i].SetTranslation(FMath::Lerp(P.GetLocation(), N.GetLocation(), Alpha));
			OutPose[i].SetRotation(FQuat::FastLerp(P.GetRotation(), N.GetRotation(), Alpha).GetNormalized());
			OutPose[i].SetScale3D(FMath::Lerp(P.GetScale3D(), N.GetScale3D(), Alpha));
		}

		if (auto* GhostAnim = Cast<UGhostAnimInstance>(Info.Component->GetAnimInstance()))
		{
			GhostAnim->SetTargetPose(OutPose);
		}
	}
}


/**
 * @brief Creates a mesh component based on an FComponentRecord and registers it with the world.
 * @param Record Information about the component to be created.
 * @return The created component on success, nullptr on failure.
 */	
USceneComponent* UPlayComponent::CreateComponentFromRecord(const FComponentRecord& Record, const TMap<FString, TObjectPtr<UObject>>& AssetCache) const
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_CreateComponentFromRecord);
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("CreateComponentFromRecord failed: Owner is null."));
		return nullptr;
	}

	// Load the component class from the FComponentRecord.
	UClass* ComponentClass = FindObject<UClass>(nullptr, *Record.ComponentClassPath);
	if (!ComponentClass ||
		!(ComponentClass->IsChildOf(UStaticMeshComponent::StaticClass()) || ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass())))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Failed to load or invalid component class: %s"), *Record.ComponentClassPath);
		return nullptr;
	}

	// Create a new component on the Owner actor.
	USceneComponent* NewComponent = nullptr;
	
	if (ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
	{
		USkeletalMeshComponent* SkeletalComp = NewObject<USkeletalMeshComponent>(Owner, USkeletalMeshComponent::StaticClass(), FName(*Record.ComponentName));
		SkeletalComp->SetAnimInstanceClass(UGhostAnimInstance::StaticClass());
		SkeletalComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		NewComponent = SkeletalComp;

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
    	// Get the asset directly from the cache instead of using AssetRef.TryLoad().
        if (const TObjectPtr<UObject>* FoundAsset = AssetCache.Find(Record.AssetPath))
        {
            if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(NewComponent))
            {
                StaticMeshComp->SetStaticMesh(Cast<UStaticMesh>(*FoundAsset));
                StaticMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            else if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(NewComponent))
            {
                SkeletalMeshComp->SetSkinnedAssetAndUpdate(Cast<USkeletalMesh>(*FoundAsset));
                SkeletalMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
        }
    }

	UMaterialInterface* TargetMaterial = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UBloodStainSubsystem* Sub = GI->GetSubsystem<UBloodStainSubsystem>())
			{
				TargetMaterial = Sub->GetDefaultMaterial();
			}
		}
	}

	if (PlaybackOptions.GroupGhostMaterial != nullptr)
	{
		TargetMaterial = PlaybackOptions.GroupGhostMaterial;
	}
	
	// Apply materials in order.
	for (int32 MatIndex = 0; MatIndex < Record.MaterialPaths.Num(); ++MatIndex)
	{
		// Force the ghost material if the option is enabled
		if ((PlaybackOptions.bUseGhostMaterial || Record.MaterialPaths[MatIndex].IsEmpty()) && TargetMaterial)
		{
			NewMeshComponent->SetMaterial(MatIndex, TargetMaterial);
			continue; // Move to the next material slot.
		}

		// If the original material path is not empty and bUseGhostMaterial is false.
		if (!Record.MaterialPaths[MatIndex].IsEmpty())
		{
			// Get the material directly from the cache instead of using StaticLoadObject.
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
			
			// Check if there are saved dynamic parameters for the current material index.
			if (Record.MaterialParameters.Contains(MatIndex))
			{
				UMaterialInstanceDynamic* DynMaterial = NewMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(MatIndex, OriginalMaterial);

				if (DynMaterial)
				{
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
				// If no parameters are saved, apply the original material directly.
				NewMeshComponent->SetMaterial(MatIndex, OriginalMaterial);
			}
		}
	}

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

	TArray<FComponentActiveInterval*> AliveComps;
	QueryIntervalTree(IntervalRoot.Get(), FrameIndex, AliveComps);

	TSet<FString> AliveComponentNames;
	for (const FComponentActiveInterval* Interval : AliveComps)
	{
		AliveComponentNames.Add(Interval->Meta.ComponentName);
	}

	// Iterate through all pre-created components and update their state.
	for (auto& Pair : ReconstructedComponents)
	{
		const FString& ComponentName = Pair.Key;
		USceneComponent* Component = Pair.Value;

		if (!Component) continue;

		// Check if the component should be active at the current frame.
		const bool bShouldBeActive = AliveComponentNames.Contains(ComponentName);
		const bool bIsCurrentlyActive = Component->IsVisible();

		// Only call functions if the state needs to change.
		if (bShouldBeActive != bIsCurrentlyActive)
		{
			Component->SetVisibility(bShouldBeActive);
			Component->SetActive(bShouldBeActive);
		}
	}
}

/**
 * Assumes a balanced binary tree. Classifies intervals into left/right of the center and builds the tree.
 */
TUniquePtr<FIntervalTreeNode> UPlayComponent::BuildIntervalTree(const TArray<FComponentActiveInterval*>& InComponentIntervals)
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_BuildIntervalTree);
	if (InComponentIntervals.Num() == 0)
	{
		return nullptr;		
	}

	// Determine the center point of the intervals as the median.
	TArray<int32> Endpoints;
	for (FComponentActiveInterval* I : InComponentIntervals)
	{
		Endpoints.Add(I->StartFrame);
		Endpoints.Add(I->EndFrame);
	}
	Endpoints.Sort();
	int32 Mid = Endpoints[Endpoints.Num()/2];

	TArray<FComponentActiveInterval*> LeftList, RightList;
	TUniquePtr<FIntervalTreeNode> Node = MakeUnique<FIntervalTreeNode>();
	Node->Center = Mid;
	for (FComponentActiveInterval* I : InComponentIntervals)
	{
		// Only add intervals that overlap the Mid to this node; classify non-overlapping ones for left/right children.
		if (I->EndFrame < Mid)
		{
			LeftList.Add(I);			
		}
		else if (I->StartFrame > Mid)
		{
			RightList.Add(I);			
		}
		else
		{
			Node->Intervals.Add(I);			
		}
	}

	Node->Left  = BuildIntervalTree(LeftList);
	Node->Right = BuildIntervalTree(RightList);
	return Node;
}

void UPlayComponent::QueryIntervalTree(FIntervalTreeNode* Node, int32 FrameIndex, TArray<FComponentActiveInterval*>& OutComponentIntervals)
{
	SCOPE_CYCLE_COUNTER(STAT_PlayComponent_QueryIntervalTree);
	if (!Node)
	{
		return;
	}

	// Collect intervals from this node's list that cover the query point.
	for (auto* I : Node->Intervals)
	{
		if (I->StartFrame <= FrameIndex && FrameIndex < I->EndFrame)
			OutComponentIntervals.Add(I);
	}

	if (FrameIndex < Node->Center)
	{
		QueryIntervalTree(Node->Left.Get(), FrameIndex, OutComponentIntervals);		
	}
	else if (FrameIndex > Node->Center)
	{
		QueryIntervalTree(Node->Right.Get(), FrameIndex, OutComponentIntervals);		
	}
}