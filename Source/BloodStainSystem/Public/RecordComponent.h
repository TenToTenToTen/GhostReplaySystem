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


/**
 * 
 */
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class BLOODSTAINSYSTEM_API URecordComponent : public UActorComponent
{
	friend class UReplayTerminatedActorManager;
	GENERATED_BODY()

public:	
	URecordComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Initialize(const FName& InGroupName, const FBloodStainRecordOptions& InOptions);

	// Cook Data from FrameQueue to GhostSaveData
	FRecordActorSaveData CookQueuedFrames();
	
public:
	/* Called when a new component attached to the owner */
	void OnComponentAttached(UMeshComponent* NewComponent);

	/* Called when a component detached from the owner */
	void OnComponentDetached(UMeshComponent* DetachedComponent);

	/** Recording group name */
	UFUNCTION(BlueprintCallable, Category="Record")
	FName GetRecordGroupName() const { return GroupName; }

private:
	/** Collect mesh components from the current actor and sub-actor */
	void CollectOwnedMeshComponents();

	/** Create FComponentRecord Data from mesh component */
	bool CreateRecordFromMeshComponent(UMeshComponent* InMeshComponent, FComponentRecord& OutRecord);

	/**
	 * Create FComponentRecord From UMeshComponent
	 * @param InMeshComponent Target Mesh Component
	 * @param OutRecord Created Record Struct
	 * @return return true if the record is created successfully.
	 */
	static void FillMaterialData(const UMeshComponent* InMeshComponent, FComponentRecord& OutRecord);
	
	/** Checks for newly attached or detached actors since the last frame and updates the recording state accordingly. */
	void HandleAttachedActorChangesByBit();

protected:

	UPROPERTY()
	FName GroupName = NAME_None;
	
	/** Record Option */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Record")
	FBloodStainRecordOptions RecordOptions;
	
	float StartTime;
	int32 MaxRecordFrames;
	
	int32 CurrentFrameIndex;
	float TimeSinceLastRecord;
	
	/** Records All frames up to MaxFrames */
	TUniquePtr<TCircularQueue<FRecordFrame>> FrameQueuePtr;

	/** Component currently owned */
	UPROPERTY()
	TSet<TObjectPtr<USceneComponent>> OwnedComponentsForRecord;
	
	/** Component Intervals for each component, used to track when components were attached/detached */
	UPROPERTY()
	TArray<FComponentActiveInterval> ComponentActiveIntervals;

	/**
	 * Key is FComponentActiveInterval::FComponentRecord::ComponentName
	 * O(log N) access when detaching
	 */
	TMap<FString, int32> IntervalIndexMap;
	
	/** Attached / Detached Component list to record in next frame */
	TArray<FComponentRecord> PendingAddedComponents;
	TArray<FString> PendingRemovedComponentNames;

private:
	FName PrimaryComponentName;
	
	TMap<FString, TSharedPtr<FComponentRecord>> MetaDataCache;
	TMap<TObjectPtr<AActor>, int32 > AttachedActorIndexMap;
	TArray<TObjectPtr<AActor>> AttachedIndexToActor;
	TBitArray<> PrevAttachedBits;
	TBitArray<> CurAttachedBits;
};
