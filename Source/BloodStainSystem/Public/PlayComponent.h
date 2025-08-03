/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
#include "OptionTypes.h"
#include "Components/ActorComponent.h"
#include "PlayComponent.generated.h"

class USkeletalMeshComponent;

struct FIntervalTreeNode
{
	int32 Center;
	TArray<FComponentActiveInterval*>    Intervals;
	TUniquePtr<FIntervalTreeNode>  Left, Right;
};

USTRUCT()
struct FSkelReplayInfo
{
	GENERATED_BODY()

	FSkelReplayInfo() = default;
	FSkelReplayInfo(USkeletalMeshComponent* InComp, const FString& InKey)
	  : Component(InComp)
	  , ComponentName(InKey)
	{}

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Component = nullptr;
	
	FString	ComponentName;
};

/**
 * Component attached to the Actor during Playback.
 * Attach by UBloodStainSubsystem::StartReplayByBloodStain, UBloodStainSubsystem::StartReplayFromFile
 * Detach by Stop Replay - Destroy, UBloodStainSubSystem::StopReplay UBloodStainSubSystem::StopReplayPlayComponent, etc.
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class BLOODSTAINSYSTEM_API UPlayComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UPlayComponent();
	
	void Initialize(FGuid PlaybackKey, const FRecordHeaderData& InRecordHeaderData, const FRecordActorSaveData& InReplayData, const FBloodStainPlaybackOptions& InPlaybackOptions);
	
	void FinishReplay() const;
	
	/** Calculate Playback State & Current Time.
	 * @return false - if Playback is end */
	bool CalculatePlaybackTime(float& OutElapsedTime);

	/** Update Replay Frame by Calculated Time & Apply Interpolation */
	void UpdatePlaybackToTime(float ElapsedTime);

	/**
	 * @param InMaterial if null, use OriginalMaterial (If OriginalMaterial is null, do not Apply)  
	 */
	UFUNCTION(BlueprintCallable, Category = "BloodStain|Playback", meta=(BlueprintPure = false))
	void ApplyMaterial(UMaterialInterface* InMaterial) const;
	
public:
	FGuid GetPlaybackKey() const;

	FRecordActorSaveData GetReplayData() const { return ReplayData; }

	void SetPlaybackStartTime(const float StartTime) { PlaybackStartTime = StartTime; }
	
protected:
	/** Apply Interpolation to Component between Two Frames */
	void ApplyComponentTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const;
	
	/** Apply Interpolation to Skeletal Bone between Two Frames */
	void ApplySkeletalBoneTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const;

private:
	/** Create & Attach, Register Component From FComponentRecord Data*/
	USceneComponent* CreateComponentFromRecord(const FComponentRecord& Record, const TMap<FString, TObjectPtr<UObject>>& AssetCache) const;

	void SeekFrame(int32 FrameIndex);
	
	static TUniquePtr<FIntervalTreeNode> BuildIntervalTree(const TArray<FComponentActiveInterval*>& InComponentIntervals);
	static void QueryIntervalTree(FIntervalTreeNode* Node, int32 FrameIndex, TArray<FComponentActiveInterval*>& OutComponentIntervals);

public:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "BloodStain|Playback")
	FRecordHeaderData RecordHeaderData;
	
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = "BloodStain|Playback")
	FBloodStainPlaybackOptions PlaybackOptions;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "BloodStain|Playback")
	FGuid PlaybackKey;

protected:
	UPROPERTY()
	FRecordActorSaveData ReplayData;

	UPROPERTY()
	TMap<FString, TObjectPtr<USceneComponent>> ReconstructedComponents;

	UPROPERTY()
	TObjectPtr<AActor> ReplayActor;

	UPROPERTY()
	TArray<FSkelReplayInfo> SkelInfos;

	/* Interval Tree root
	 * Used to quickly find components that overlap with a given time range. */
	TUniquePtr<FIntervalTreeNode> IntervalRoot;
	
	float PlaybackStartTime = 0.f;

	int32 CurrentFrame = 0;
};
