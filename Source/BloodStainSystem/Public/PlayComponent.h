// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
#include "OptionTypes.h"
#include "Components/ActorComponent.h"
#include "PlayComponent.generated.h"

class UAnimSequence;
class UWorld;
class USkeletalMeshComponent;
class UGameInstance;
class USkeletalMesh;
class USkeleton;
class UStaticMeshComponent;
class UMeshComponent;
class UMaterialInterface;

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
	// Sets default values for this component's properties
	UPlayComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Initialize(FGuid PlaybackKey, const FRecordHeaderData& InRecordHeaderData, const FRecordActorSaveData& InReplayData, const FBloodStainPlaybackOptions& InPlaybackOptions);
	void ApplySkeletalBoneTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const;
	void FinishReplay();
	
	FGuid GetPlaybackKey() const;

protected:

	/** 한 쌍의 Frame(Prev, Next)과 Alpha 를 받아 Mesh 에 적용합니다. */
	void ApplyComponentTransforms(const FRecordFrame& Prev, const FRecordFrame& Next, float Alpha) const;

	void ConvertFrameToAnimSequence();

	// /** 현재 프레임에 추가 / 삭제되는 컴포넌트 변경사항을 적용합니다 */
	// void ApplyComponentChanges(const FRecordFrame& Frame);
private:
	/** FComponentRecord 데이터로부터 메시 컴포넌트를 생성, 등록, 부착하는 헬퍼 함수 */
	USceneComponent* CreateComponentFromRecord(const FComponentRecord& Record, const TMap<FString, TObjectPtr<UObject>>& AssetCache);
	
protected:
	FRecordHeaderData RecordHeaderData;
	FRecordActorSaveData ReplayData;
	FBloodStainPlaybackOptions PlaybackOptions;

	UPROPERTY(BlueprintReadOnly, Category = "BloodStain")
	FGuid PlaybackKey;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TMap<FString, TObjectPtr<UAnimSequence>> AnimSequences;
	
	/** 재생 시작 시점 */
	float PlaybackStartTime = 0.f;

	/** 현재 프레임 인덱스 */
	int32 CurrentFrame = 0;

	UPROPERTY()
	TMap<FString, TObjectPtr<USceneComponent>> ReconstructedComponents;

private:
	
	/* Interval Tree root
	 * Used to quickly find components that overlap with a given time range.
	 */
	TUniquePtr<FIntervalTreeNode> IntervalRoot;

	void SeekFrame(int32 FrameIndex);
	TUniquePtr<FIntervalTreeNode> BuildIntervalTree(TArray<FComponentInterval*>& List);
	void QueryIntervalTree(FIntervalTreeNode* Node, int32 FrameIndex, TArray<FComponentInterval*>& Out);
};
