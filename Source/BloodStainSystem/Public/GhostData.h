// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OptionTypes.h"
#include "GhostData.generated.h"

class AReplayActor;

USTRUCT()
struct FBloodStainRecordGroup
{
	GENERATED_BODY()

	// UPROPERTY()
	// FName ReplayDataName;
	
	UPROPERTY()
	FTransform SpawnPointTransform;
	
	UPROPERTY()
	FBloodStainRecordOptions RecordOptions;
	
	UPROPERTY()
	TMap<AActor*, class URecordComponent*> ActiveRecorders;
};

USTRUCT()
struct FBloodStainPlaybackGroup
{
	GENERATED_BODY()

	UPROPERTY()
	TSet<TObjectPtr<AReplayActor>> ActiveReplayers;
};

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
// 각 컴포넌트의 정보를 담을 구조체
USTRUCT(BlueprintType)
struct FComponentRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	FString ComponentName; // 컴포넌트 이름 (재생 시 찾거나 생성할 때 사용)
	
	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	FString ComponentClassPath; // 컴포넌트 클래스 경로 (예: "/Script/Engine.StaticMeshComponent")

	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	FString AssetPath; // 메시 컴포넌트의 경우, 사용된 메시 에셋의 경로 (예: "/Game/Meshes/MyStaticMesh.MyStaticMesh")

	UPROPERTY(BlueprintReadWrite, Category = "BloodStain")
	TArray<FString> MaterialPaths;

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

/** 컴포넌트의 생명 주기 [생성 프레임, 소멸 프레임] 저장 */
USTRUCT()
struct FComponentInterval
{
	GENERATED_BODY()

	/** 컴포넌트 메타데이터 */
	UPROPERTY()
	FComponentRecord Meta;

	/** 부착된 프레임 인덱스(포함) */
	UPROPERTY()
	int32 StartFrame = 0;

	/** 탈착된 프레임 인덱스(비포함) */
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


// 본 이름과 트랜스폼 맵을 래핑하는 새로운 USTRUCT
USTRUCT(BlueprintType)
struct FBoneComponentSpace
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TArray<FTransform> BoneTransforms;

	friend FArchive& operator<<(FArchive& Ar, FBoneComponentSpace& BoneComponentSpace)
	{
		Ar << BoneComponentSpace.BoneTransforms;
		return Ar;
	}
};

USTRUCT(BlueprintType)
struct FRecordFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	float TimeStamp;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TMap<FString, FTransform> ComponentTransforms;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TMap<FString, FBoneComponentSpace> SkeletalMeshBoneTransforms;

	/* Added Components list in this frame */
	UPROPERTY()
	TArray<FComponentRecord> AddedComponents;

	/* Removed Components list in this frame */
	UPROPERTY()
	TArray<FString> RemovedComponentNames;

	/* Original Frame Index from the Recorded Data */
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


USTRUCT(BlueprintType)
struct FRecordActorSaveData
{
	GENERATED_BODY()

	UPROPERTY()
	FName ComponentName;

	UPROPERTY()
	TArray<FComponentInterval> ComponentIntervals;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "BloodStain")
	TArray<FRecordFrame> RecordedFrames;

	
	friend FArchive& operator<<(FArchive& Ar, FRecordActorSaveData& Data)
	{
		Ar << Data.ComponentName;
		Ar << Data.ComponentIntervals;
		Ar << Data.RecordedFrames;
		return Ar;
	}
};

USTRUCT(BlueprintType)
struct FRecordHeaderData
{
	GENERATED_BODY()
	
	UPROPERTY()
	FName GroupName;
	
	/** BloodStain Spawn Transform */
	UPROPERTY()
	FTransform SpawnPointTransform;
	
	UPROPERTY()
	FBloodStainRecordOptions RecordOptions;
		
	friend FArchive& operator<<(FArchive& Ar, FRecordHeaderData& Data)
	{
		Ar << Data.GroupName;
		Ar << Data.SpawnPointTransform;
		Ar << Data.RecordOptions;
		return Ar;
	}
};

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
