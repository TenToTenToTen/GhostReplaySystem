/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "OptionTypes.h"
#include "GhostData.generated.h"

class AReplayActor;
class URecordComponent;

/** @brief Recording group for one or more actors, saved as a single file.
 * 
 *	manages the spawn point, recording options, and active recorders.
 */
USTRUCT()
struct FBloodStainRecordGroup
{
	GENERATED_BODY()

	/** Transform at which this group will be spawned for Replay */
	UPROPERTY()
	FTransform SpawnPointTransform;

	/** Recording options applied to this group */
	UPROPERTY()
	FBloodStainRecordOptions RecordOptions;

	/** Map of actors currently being recorded to their URecordComponent instances */
	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<URecordComponent>> ActiveRecorders;
};

/** @brief Playback group: tracks active replay actors for a single replay session.
 */
USTRUCT()
struct FBloodStainPlaybackGroup
{
	GENERATED_BODY()

	/** Set of currently active replay actors */
	UPROPERTY()
	TSet<TObjectPtr<AReplayActor>> ActiveReplayers;
};

/** @brief Material parameters: serializes stored vector and scalar parameters per slot 
 * 
 *	Used to store material parameters for each mesh component in the recorded data.
 *	Can be extended to include other parameter types as needed.
 */
USTRUCT()
struct FMaterialParameters
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FName, FLinearColor> VectorParams;

	UPROPERTY()
	TMap<FName, float> ScalarParams;
	
	friend FArchive& operator<<(FArchive& Ar, FMaterialParameters& Params)
	{
		Ar << Params.VectorParams;
		Ar << Params.ScalarParams;
		return Ar;
	}
};

/** @brief Metadata for components added or removed during recording
 * 
 */
USTRUCT(BlueprintType)
struct FComponentRecord
{
	GENERATED_BODY()

	/** Component name, used to find or create the component on replay */
	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	FString ComponentName;

	/** Component class path, e.g., "/Script/Engine.StaticMeshComponent" */
	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	FString ComponentClassPath;

	/** Asset path for mesh components, e.g., "/Game/Meshes/MyStaticMesh.MyStaticMesh" */
	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	FString AssetPath;

	/** Array of material slot paths applied to this component */
	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	TArray<FString> MaterialPaths;

	/** Map of slot index to saved material parameters */
	UPROPERTY()
	TMap<int32, FMaterialParameters> MaterialParameters;
	
	friend FArchive& operator<<(FArchive& Ar, FComponentRecord& ComponentRecord)
	{
		Ar << ComponentRecord.ComponentName;
		Ar << ComponentRecord.ComponentClassPath;
		Ar << ComponentRecord.AssetPath;
		Ar << ComponentRecord.MaterialPaths;
		Ar << ComponentRecord.MaterialParameters;
		return Ar;
	}
};

/** @brief Component interval: stores lifecycle [startFrame, endFrame) for a component
 * 
 */
USTRUCT()
struct FComponentInterval
{
	GENERATED_BODY()

	/** Metadata for this component */
	UPROPERTY()
	FComponentRecord Meta;

	/** Frame index at which this component was attached (inclusive) */
	UPROPERTY()
	int32 StartFrame = 0;

	/** Frame index at which this component was detached (exclusive) */
	UPROPERTY()
	int32 EndFrame = INT32_MAX;
	
	bool operator==(const FComponentInterval& Other) const
	{
		return Meta.ComponentName == Other.Meta.ComponentName;
	}

	friend FArchive& operator<<(FArchive& Ar, FComponentInterval& Interval)
	{
		Ar << Interval.Meta;
		Ar << Interval.StartFrame;
		Ar << Interval.EndFrame;
		return Ar;
	}
};

/** @brief Array of Local-space transforms for all bones in a skeletal mesh component
 */
USTRUCT(BlueprintType)
struct FBoneComponentSpace
{
	GENERATED_BODY()

	/** Array of transforms for each bone in local component space */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TArray<FTransform> BoneTransforms;

	friend FArchive& operator<<(FArchive& Ar, FBoneComponentSpace& BoneComponentSpace)
	{
		Ar << BoneComponentSpace.BoneTransforms;
		return Ar;
	}
};

/** @brief Data recorded for a single frame, including transforms and component events
 *
 * Contains all transforms for components and skeletal meshes attached in a single actor
 * as well as added/removed components.
 */
USTRUCT(BlueprintType)
struct FRecordFrame
{
	GENERATED_BODY()
	FRecordFrame()
		:TimeStamp(0.f)
		,FrameIndex(0)
	{
	}

	/** Timestamp of this frame in seconds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	float TimeStamp;

	/** Map of components' name to their transforms at this frame */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TMap<FString, FTransform> ComponentTransforms;

	/** Map of skeletal mesh components' names to their bone transforms */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TMap<FString, FBoneComponentSpace> SkeletalMeshBoneTransforms;

	/** Components added on this frame */
	UPROPERTY()
	TArray<FComponentRecord> AddedComponents;

	/** Component names removed on this frame */
	UPROPERTY()
	TArray<FString> RemovedComponentNames;

	/** Original frame index from the recorded data */
	UPROPERTY()
	int32 FrameIndex;
	
	friend FArchive& operator<<(FArchive& Ar, FRecordFrame& Frame)
	{
		Ar << Frame.TimeStamp;
		Ar << Frame.ComponentTransforms;
		Ar << Frame.SkeletalMeshBoneTransforms;
		Ar << Frame.AddedComponents;
		Ar << Frame.RemovedComponentNames;
		Ar << Frame.FrameIndex;
		return Ar;
	}
};

/** @brief Location range: min/max position, used only for Standard_Low quantization */
USTRUCT()
struct FLocRange
{
	GENERATED_BODY()
	
	FLocRange()
			: PosMin(FVector::ZeroVector)
			, PosMax(FVector::ZeroVector)
	{}
	
	UPROPERTY()
	FVector PosMin;

	UPROPERTY()
	FVector PosMax;

	friend FArchive& operator<<(FArchive& Ar, FLocRange& R)
	{
		Ar << R.PosMin << R.PosMax;
		return Ar;
	}
};

/** @brief Scale range: min/max scale, used only for Standard_Low quantization */
USTRUCT()
struct FScaleRange
{
	GENERATED_BODY()

	FScaleRange()
		:ScaleMin(1.f, 1.f, 1.f)
		,ScaleMax(1.f, 1.f, 1.f)
	{
	}
	
	UPROPERTY()
	FVector ScaleMin;

	UPROPERTY()
	FVector ScaleMax;

	friend FArchive& operator<<(FArchive& Ar, FScaleRange& R)
	{
		Ar << R.ScaleMin << R.ScaleMax;
		return Ar;
	}
};

/** @brief Actor save data: stores all recording info for one actor, separating component vs. bone transform ranges
 *
 *  Tracks all mesh components under this actor including attached actors.
 */
USTRUCT(BlueprintType)
struct FRecordActorSaveData
{
	GENERATED_BODY()

	/** Name of the primary (root) component for this actor */
	UPROPERTY()
	FName PrimaryComponentName;

	/** Lifecycle intervals for each component */
	UPROPERTY()
	TArray<FComponentInterval> ComponentIntervals;

	/** Combined min/max location for all components on this actor */
	UPROPERTY()
	FLocRange ComponentRanges;

	/** Combined min/max scale for all components on this actor */
	UPROPERTY()
	FScaleRange ComponentScaleRanges; 

	/** Per-skeletal-mesh-component min/max location ranges for all its bones */
	UPROPERTY()
	TMap<FString, FLocRange> BoneRanges;

	/** Per-skeletal-mesh-component min/max scale ranges for all its bones */
	UPROPERTY()
	TMap<FString, FScaleRange> BoneScaleRanges;

	/** All recorded frames containing component transforms, bone transforms, and events */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TArray<FRecordFrame> RecordedFrames;

	
	friend FArchive& operator<<(FArchive& Ar, FRecordActorSaveData& Data)
	{
		Ar << Data.PrimaryComponentName;
		Ar << Data.ComponentIntervals;
		Ar << Data.ComponentRanges;
		Ar << Data.ComponentScaleRanges;
		Ar << Data.BoneRanges;
		Ar << Data.BoneScaleRanges;
		Ar << Data.RecordedFrames;
		return Ar;
	}
};

/** @brief Header for a recording session, storing metadata about the group */
USTRUCT(BlueprintType)
struct FRecordHeaderData
{
	GENERATED_BODY()

	FRecordHeaderData()
		: MaxRecordTime(5.f)
		, SamplingInterval(0.1f)
	{}
	
	UPROPERTY()
	FName GroupName;
	
	/** Transform at which the group will be spawned*/
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "BloodStain")
	FTransform SpawnPointTransform;
	
	/** Maximum recording duration in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BloodStain|Header")
	float MaxRecordTime;

	/** Sampling interval between frames in seconds (0.1 sec - 10fps) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BloodStain|Header")
	float SamplingInterval;
		
	friend FArchive& operator<<(FArchive& Ar, FRecordHeaderData& Data)
	{
		Ar << Data.GroupName;
		Ar << Data.SpawnPointTransform;
		Ar << Data.MaxRecordTime;
		Ar << Data.SamplingInterval;
		return Ar;
	}
};

/** @brief Total Save data containing header and per-actor recordings */
USTRUCT(BlueprintType)
struct FRecordSaveData
{
	GENERATED_BODY()

	UPROPERTY()
	FRecordHeaderData Header;
	
	UPROPERTY()
	TArray<FRecordActorSaveData> RecordActorDataArray;
	
	bool IsValid() const
	{
		if (RecordActorDataArray.Num() == 0)
		{
			return false;
		}		
		return true;		
	}
	
	friend FArchive& operator<<(FArchive& Ar, FRecordSaveData& Data)
	{
		Ar << Data.Header;
		Ar << Data.RecordActorDataArray;
		return Ar;
	}
};
