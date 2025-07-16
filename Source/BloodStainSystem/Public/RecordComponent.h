/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
#include "Components/ActorComponent.h"
#include "OptionTypes.h"
#include "Containers/CircularQueue.h"
#include "RecordComponent.generated.h"

class UMeshComponent;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class BLOODSTAINSYSTEM_API URecordComponent : public UActorComponent
{
	friend class UReplayTerminatedActorManager;
	GENERATED_BODY()

public:	
	URecordComponent();
	
protected:
	virtual void BeginPlay() override;

public:	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 이 컴포넌트의 모든 설정을 한 번에 초기화 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void Initialize(const FName& InGroupName, const FBloodStainRecordOptions& InOptions);
	void CollectMeshComponents();
	bool SaveQueuedFrames();
	
public:
	/* Called when a new component attached to the owner */
	void OnComponentAttached(UMeshComponent* NewComponent);

	/* Called when a component detached from the owner */
	void OnComponentDetached(UMeshComponent* DetachedComponent);
	
	virtual bool ShouldRecord();

	FRecordActorSaveData GetGhostSaveData() { return GhostSaveData; }
	int32 GetCurrentFrameIndex() const { return CurrentFrameIndex;}

	FName GetRecordGroupName() const { return GroupName; }

protected:

	UPROPERTY()
	FName GroupName = NAME_None;
	
	/** 녹화 옵션 원본을 그대로 저장 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Record")
	FBloodStainRecordOptions RecordOptions;

	float TimeSinceLastRecord = 0.0f;
	float StartTime;
	FRecordActorSaveData GhostSaveData;
	UPROPERTY()
	TSet<TObjectPtr<USceneComponent>> RecordComponents;
	
	

private:
	/** Create FComponentRecord Data from meshcomponent */
	bool CreateRecordFromMeshComponent(UMeshComponent* InMeshComponent, FComponentRecord& OutRecord);
	void FillMaterialData(const UMeshComponent* InMeshComponent, FComponentRecord& OutRecord);
	/** Records All frames up to MaxFrames */
	TUniquePtr<TCircularQueue<FRecordFrame>> FrameQueuePtr;
	int32 MaxRecordFrames;

	/*
	 * Component Event Tracker 
	 */
	
	/** Attached / Detached Component list to record in next frame */
	TArray<FComponentRecord> PendingAddedComponents;
	TArray<FString> PendingRemovedComponentNames;

	
	/** Component Intervals for each component, used to track when components were attached/detached */
	UPROPERTY()
	TArray<FComponentInterval> ComponentIntervals;

	/** 이름 → ComponentIntervals 인덱스 맵 (Detach 시 O(log N) 접근용) */
	TMap<FString, int32> IntervalIndexMap;
	
	/** Current Frame Index */
	int32 CurrentFrameIndex = 0;

	/** Previous Attached Actors */
	TArray<TObjectPtr<AActor>> PreviousAttachedActors;

	/** Checks for newly attached or detached actors since the last frame and updates the recording state accordingly. */
	void HandleAttachedActorChanges();

	void HandleAttachedActorChangesByBit();

private:
	TMap<FString, TSharedPtr<FComponentRecord>> MetaDataCache;
	TMap<TObjectPtr<AActor>, int32 > AttachedActorIndexMap;
	TArray<TObjectPtr<AActor>> AttachedIndexToActor;
	TBitArray<> PrevAttachedBits;
	TBitArray<> CurAttachedBits;
};
