/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
#include "Components/ActorComponent.h"
#include "PlayComponent.generated.h"

struct FIntervalTreeNode
{
	int32 Center;
	TArray<FComponentActiveInterval*>    Intervals;
	TUniquePtr<FIntervalTreeNode>  Left, Right;
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

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Initialize(FGuid PlaybackKey, const FRecordHeaderData& InRecordHeaderData, const FRecordActorSaveData& InReplayData, const FBloodStainPlaybackOptions& InPlaybackOptions);
	
	void FinishReplay() const;
	
	FGuid GetPlaybackKey() const;

protected:

	/** Calculate Playback State & Current Time.
	 * @return false - if Playback is end */
	bool CalculatePlaybackTime(float& OutElapsedTime);

	/** Update Replay Frame by Calculated Time & Apply Interpolation */
	void UpdatePlaybackToTime(float ElapsedTime);
	
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
	
protected:
	FRecordHeaderData RecordHeaderData;
	FRecordActorSaveData ReplayData;
	UPROPERTY(BlueprintReadWrite, Category = "BloodStain|Playback")
	FBloodStainPlaybackOptions PlaybackOptions;

	UPROPERTY(BlueprintReadOnly, Category = "BloodStain|Playback")
	FGuid PlaybackKey;
	
	float PlaybackStartTime = 0.f;

	int32 CurrentFrame = 0;

	UPROPERTY()
	TMap<FString, TObjectPtr<USceneComponent>> ReconstructedComponents;
	
	/* Interval Tree root
	 * Used to quickly find components that overlap with a given time range.
	 */
	TUniquePtr<FIntervalTreeNode> IntervalRoot;
};
