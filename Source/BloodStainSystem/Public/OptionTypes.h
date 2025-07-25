/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "OptionTypes.generated.h"

/** @brief Recording options for the BloodStain system.
 * 
 *	includes settings for maximum recording duration, sampling interval, and replay options.
 */
USTRUCT(BlueprintType)
struct FBloodStainRecordOptions
{
	GENERATED_BODY()

	/**
	 * The name of the recording group to which all actors will be added.
	 * If NAME_None, the default group is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Record")
	FName RecordingGroupName = NAME_None;

	/** Record File Name. If a file already exists, override */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Record")
	FName FileName = NAME_None;

	/** BloodStain GamePlayTags. This is stored in FRecordHeaderData */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Record")
	FGameplayTagContainer Tags;

	/** Maximum recording duration in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Record")
	float MaxRecordTime = 5.f;

	/** Interval between samples in seconds (default = 0.1, ~10fps) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Record")
	float SamplingInterval = 0.1f;

	/** If true, track mesh attachment changes in record component's tick */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bTrackAttachmentChanges = true;

	/** Save immediately if all recording actors in group is empty */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bSaveImmediatelyIfGroupEmpty = false;
	
	friend FArchive& operator<<(FArchive& Ar, FBloodStainRecordOptions& Data)
	{
		Ar << Data.RecordingGroupName;
		Ar << Data.FileName;

		FGameplayTagContainer::StaticStruct()->SerializeItem(Ar, &Data.Tags, nullptr);
		
		Ar << Data.MaxRecordTime;
		Ar << Data.SamplingInterval;
		Ar << Data.bTrackAttachmentChanges;
		Ar << Data.bSaveImmediatelyIfGroupEmpty;
		return Ar;
	}
};


/** @brief Playback options for the BloodStain system
 * 
 *	includes settings for playback speed, looping behavior, and material usage.
 */
USTRUCT(BlueprintType)
struct FBloodStainPlaybackOptions
{
	GENERATED_BODY()

	/** Playback speed ratio (1.0 = real-time, negative for reverse) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	float PlaybackRate = 1.0f;

	/** If true, loop playback after completion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bIsLooping = false;

	/** If true, use ghost material instead of original recorded one */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Replay")
	bool bUseGhostMaterial = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bUseGhostMaterial", EditConditionHides))
	UMaterialInterface* GroupGhostMaterial = nullptr;

	
	friend FArchive& operator<<(FArchive& Ar, FBloodStainPlaybackOptions& Data)
	{
		Ar << Data.PlaybackRate;
		Ar << Data.bIsLooping;
		Ar << Data.bUseGhostMaterial;
		Ar << Data.GroupGhostMaterial;
		return Ar;
	}
};