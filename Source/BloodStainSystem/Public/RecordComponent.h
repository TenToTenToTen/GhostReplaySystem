/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
#include "Components/ActorComponent.h"
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

	bool SaveQueuedFrames();
	
public:
	/* Called when a new component attached to the owner */
	void OnComponentAttached(UMeshComponent* NewComponent);

	/* Called when a component detached from the owner */
	void OnComponentDetached(UMeshComponent* DetachedComponent);
	
	FRecordActorSaveData GetGhostSaveData() { return GhostSaveData; }
	FName GetRecordGroupName() const { return GroupName; }

private:
	void CollectMeshComponents();

	/** Create FComponentRecord Data from meshcomponent */
	bool CreateRecordFromMeshComponent(UMeshComponent* InMeshComponent, FComponentRecord& OutRecord);

	void FillMaterialData(const UMeshComponent* InMeshComponent, FComponentRecord& OutRecord);
	
	/** Checks for newly attached or detached actors since the last frame and updates the recording state accordingly. */
	void HandleAttachedActorChanges();
	void HandleAttachedActorChangesByBit();

protected:

	UPROPERTY()
	FName GroupName = NAME_None;
	
	/** 녹화 옵션 원본을 그대로 저장 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Record")
	FBloodStainRecordOptions RecordOptions;

	FRecordActorSaveData GhostSaveData;
	
	float TimeSinceLastRecord = 0.0f;
	float StartTime;
	int32 CurrentFrameIndex = 0;
	int32 MaxRecordFrames;
	/** Records All frames up to MaxFrames */
	TUniquePtr<TCircularQueue<FRecordFrame>> FrameQueuePtr;
	
	/** Component Intervals for each component, used to track when components were attached/detached */
	UPROPERTY()
	TArray<FComponentInterval> ComponentIntervals;

	/** 이름 → ComponentIntervals 인덱스 맵 (Detach 시 O(log N) 접근용) */
	TMap<FString, int32> IntervalIndexMap;

	
	UPROPERTY()
	TSet<TObjectPtr<USceneComponent>> OwnedComponentsForRecord;
	
	/** Attached / Detached Component list to record in next frame */
	TArray<FComponentRecord> PendingAddedComponents;
	TArray<FString> PendingRemovedComponentNames;
	
	/** Previous Attached Actors */
	TArray<TObjectPtr<AActor>> PreviousAttachedActors;

private:
	TMap<FString, TSharedPtr<FComponentRecord>> MetaDataCache;
	TMap<TObjectPtr<AActor>, int32 > AttachedActorIndexMap;
	TArray<TObjectPtr<AActor>> AttachedIndexToActor;
	TBitArray<> PrevAttachedBits;
	TBitArray<> CurAttachedBits;
};
