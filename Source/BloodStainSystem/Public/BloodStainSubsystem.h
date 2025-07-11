// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BloodActor.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GhostData.h"
#include "BloodStainFileOptions.h" 
#include "BloodStainSubsystem.generated.h"

class UPlayComponent;
class URecordComponent;
struct FBloodStainRecordOptions;
struct FBloodStainReplayOptions;

UCLASS(Config=Game)
class BLOODSTAINSYSTEM_API UBloodStainSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
public:
	/** 녹화 시작 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	bool StartRecording(AActor* TargetActor, const FBloodStainRecordOptions& Options);

	// /** 녹화 중단 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void StopRecording(AActor* TargetActor);
	
	/** 재생 시작 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool StartReplay(AActor* TargetActor, const FRecordSavedData& Data);

	/** 재생 중단 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	void StopReplay(AActor* TargetActor);

	// 순수 파일 로드 (UI나 Blueprint에서 직접 호출해도 OK)
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool LoadRecordingData(const FString& FileName, FRecordSavedData& OutData);
	
	// 3) 파일 바로 리플레이 시작 (1) + (2) 조합)
	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	bool StartReplayFromFile(AActor* TargetActor, const FString& FileName);

	UFUNCTION(BlueprintCallable, Category="BloodStain|Replay")
	void SetDefalutMaterial(UMaterialInterface* InMaterial)
	{
		GhostMaterial = InMaterial;
	}
	
	UMaterialInterface* GetDefaultMaterial()
	{
		return GhostMaterial;
	}

public:
	/** 혈흔 액터 생성 + 서브시스템에 등록 */
	UFUNCTION(BlueprintCallable, Category="BloodStain")
	ABloodActor* SpawnBloodStain(const FVector& Location, const FRotator& Rotation, const FString& FileName);

	/** 재생 끝난 혈흔 액터 해제 (액터 파괴 또는 풀링 전용 메타콜) */
	void RemoveBloodStain(ABloodActor* StainActor);

	UFUNCTION(BlueprintCallable, Category="BloodStain")
	void SpawnAllBloodStain();

public:
	/* Notify Attached / Detached Component Events */
	/**
	* @brief 지정된 액터의 컴포넌트 부착을 기록 시스템에 알립니다.
	* @param TargetActor 기록 중인 액터
	* @param NewComponent 새로 부착된 메시 컴포넌트
	*/
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void NotifyComponentAttached(AActor* TargetActor, UMeshComponent* NewComponent);

	/**
	 * @brief 지정된 액터의 컴포넌트 탈착을 기록 시스템에 알립니다.
	 * @param TargetActor 기록 중인 액터
	 * @param DetachedComponent 탈착된 메시 컴포넌트
	 */
	UFUNCTION(BlueprintCallable, Category="BloodStain|Record")
	void NotifyComponentDetached(AActor* TargetActor, UMeshComponent* DetachedComponent);

public:
	/** 파일 저장·로드 옵션 (Quantization, Compression, Checksum 등) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category="File IO")
	FBloodStainFileOptions FileSaveOptions;

	UFUNCTION(BlueprintCallable, Category="BloodStain|File")
	void SetFileSaveOptions(const FBloodStainFileOptions& InOptions);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BloodStain|Record")
	float LineTraceLength = 500.f;
	
protected:
	/** 플레이어 사망 시 스폰할 BloodStainActor 클래스 */
	UPROPERTY(EditDefaultsOnly, Category="BloodStain|Pool")
	TSubclassOf<AActor> BloodStainActorClass;
	
private:
	/** 현재 녹화 중인 RecordComponent들 */
	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<URecordComponent>> ActiveRecorders;
	
	/** 현재 재생 중인 ReplayComponent들 */
	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<UPlayComponent>> ActiveReplayers;

	/** 에디터나 시작 시 불러올 리플레이 파일 목록 */
	UPROPERTY(VisibleAnywhere, Category="BloodStain|Cache")
	TArray<FString> AvailableRecordings;
	
	/** 캐싱된 리플레이 데이터 */
	UPROPERTY()
	TMap<FString, FRecordSavedData> CachedRecordings;

	/** 현재 월드에 존재하는 혈흔 액터 리스트 */
	UPROPERTY()
	TArray<TWeakObjectPtr<ABloodActor>> ActiveBloodStains;
	

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GhostMaterial;
};
