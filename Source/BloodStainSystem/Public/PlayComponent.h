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
	TArray<FComponentInterval*>    Intervals;
	TUniquePtr<FIntervalTreeNode>  Left, Right;
};

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

	/** 한 쌍의 Frame(Prev, Next)과 Alpha 를 받아 Mesh 에 적용합니다. */
	void ApplyComponentTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const;
	void ApplySkeletalBoneTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const;

private:
	/** FComponentRecord 데이터로부터 메시 컴포넌트를 생성, 등록, 부착하는 헬퍼 함수 */
	USceneComponent* CreateComponentFromRecord(const FComponentRecord& Record, const TMap<FString, TObjectPtr<UObject>>& AssetCache) const;

	void SeekFrame(int32 FrameIndex);
	static TUniquePtr<FIntervalTreeNode> BuildIntervalTree(const TArray<FComponentInterval*>& InComponentIntervals);
	static void QueryIntervalTree(FIntervalTreeNode* Node, int32 FrameIndex, TArray<FComponentInterval*>& OutComponentIntervals);
	
protected:
	FRecordHeaderData RecordHeaderData;
	FRecordActorSaveData ReplayData;
	FBloodStainPlaybackOptions PlaybackOptions;

	UPROPERTY(BlueprintReadOnly, Category = "BloodStain")
	FGuid PlaybackKey;
		
	/** 재생 시작 시점 */
	float PlaybackStartTime = 0.f;

	/** 현재 프레임 인덱스 */
	int32 CurrentFrame = 0;

	UPROPERTY()
	TMap<FString, TObjectPtr<USceneComponent>> ReconstructedComponents;
	
	/* Interval Tree root
	 * Used to quickly find components that overlap with a given time range.
	 */
	TUniquePtr<FIntervalTreeNode> IntervalRoot;
};
